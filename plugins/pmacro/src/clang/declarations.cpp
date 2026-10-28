module;

#include <clang/AST/Attr.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/Expr.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Lex/Lexer.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/ADT/StringExtras.h>

module pmacro;

import std;

#include "declarations.hpp"

namespace pmacro::clang_bridge
{

auto next_source_identity() -> std::uint64_t {
    static std::atomic<std::uint64_t> next { 1 };
    return next.fetch_add(1, std::memory_order_relaxed);
}

SourceRegistry::SourceRegistry(clang::CompilerInstance& compiler)
    : compiler_(compiler), context_(static_cast<SourceContextId>(next_source_identity())) {
}

SourceRegistry::SourceRegistry(clang::CompilerInstance& compiler,
                               SourceRegistry&          parent,
                               std::uint32_t            input_offset,
                               Span                     input)
    : compiler_(compiler),
      context_(parent.context()),
      parent_(&parent),
      input_offset_(input_offset),
      input_(input) {
}

auto SourceRegistry::context() const -> SourceContextId {
    return context_;
}

DeclarationStore::DeclarationStore(clang::CompilerInstance& compiler, SourceRegistry& sources)
    : compiler_(compiler), sources_(sources) {
}

auto SourceRegistry::identify(clang::FileID file) const -> std::uint64_t {
    for (const auto& entry : files_)
        if (entry.second == file) return entry.first;
    const auto identity = next_source_identity();
    files_.emplace_back(identity, file);
    return identity;
}

auto SourceRegistry::file(Span span) const -> clang::FileID {
    for (const auto& entry : files_)
        if (entry.first == span.source()) return entry.second;
    return {};
}

auto SourceRegistry::file_span(clang::CharSourceRange range) const
    -> std::expected<Span, SourceError> {
    if (range.isInvalid()) return std::unexpected(SourceError::Unavailable);
    auto& manager = compiler_.getSourceManager();
    auto  begin = range.getBegin(), end = range.getEnd();
    if (! begin.isFileID() || ! end.isFileID() ||
        manager.getFileID(begin) != manager.getFileID(end))
        return std::unexpected(SourceError::NonContiguous);
    if (range.isTokenRange())
        end = clang::Lexer::getLocForEndOfToken(end, 0, manager, compiler_.getLangOpts());
    if (end.isInvalid()) return std::unexpected(SourceError::Unavailable);
    const auto begin_offset = manager.getFileOffset(begin), end_offset = manager.getFileOffset(end);
    const auto file    = manager.getFileID(begin);
    bool       invalid = false;
    const auto buffer  = manager.getBufferData(file, &invalid);
    if (invalid || buffer.size() > std::numeric_limits<std::uint32_t>::max() ||
        begin_offset > end_offset || end_offset > buffer.size())
        return std::unexpected(SourceError::Unavailable);
    if (parent_) {
        if (file != manager.getMainFileID() || begin_offset < input_offset_ ||
            end_offset - input_offset_ > input_.end() - input_.begin())
            return std::unexpected(SourceError::Unavailable);
        return Span(input_.source(),
                    input_.begin() + begin_offset - input_offset_,
                    input_.begin() + end_offset - input_offset_);
    }
    return Span(identify(file), begin_offset, end_offset);
}

auto SourceRegistry::spelling(clang::CharSourceRange range) const
    -> std::expected<Span, SourceError> {
    if (range.isInvalid()) return std::unexpected(SourceError::Unavailable);
    auto&      manager = compiler_.getSourceManager();
    const auto begin = range.getBegin(), end = range.getEnd();
    // Different expansion records can spell unrelated pieces of one declaration.
    if ((begin.isMacroID() || end.isMacroID()) &&
        (! begin.isMacroID() || ! end.isMacroID() ||
         manager.getFileID(begin) != manager.getFileID(end)))
        return std::unexpected(SourceError::NonContiguous);
    return file_span(clang::CharSourceRange(
        clang::SourceRange(manager.getSpellingLoc(begin), manager.getSpellingLoc(end)),
        range.isTokenRange()));
}

auto SourceRegistry::info(clang::SourceRange range, clang::SourceLocation anchor) const
    -> SourceInfo {
    SourceInfo result;
    result.context    = context_;
    result.spelling   = spelling(clang::CharSourceRange::getTokenRange(range));
    result.from_macro = range.getBegin().isMacroID() || range.getEnd().isMacroID();
    const auto expansion =
        clang::Lexer::makeFileCharRange(clang::CharSourceRange::getTokenRange(range),
                                        compiler_.getSourceManager(),
                                        compiler_.getLangOpts());
    result.expansion =
        range.isInvalid()
            ? std::expected<Span, SourceError>(std::unexpected(SourceError::Unavailable))
        : expansion.isInvalid()
            ? std::expected<Span, SourceError>(std::unexpected(SourceError::NonContiguous))
            : file_span(expansion);
    if (anchor.isInvalid()) anchor = range.getBegin();
    anchor          = compiler_.getSourceManager().getExpansionLoc(anchor);
    auto diagnostic = file_span(clang::CharSourceRange::getTokenRange(anchor, anchor));
    if (diagnostic) result.diagnostic = *diagnostic;
    return result;
}

auto SourceRegistry::attribute_span(const clang::AnnotateAttr& attribute) const
    -> std::optional<Span> {
    auto found = attribute_spans_.find(&attribute);
    if (found != attribute_spans_.end()) return found->second;
    auto calculate = [&]() -> std::optional<Span> {
        auto span = rewrite_span(attribute.getRange());
        if (! span.has_value()) return std::nullopt;
        const auto& registry = parent_ ? *parent_ : *this;
        const auto  id       = registry.file(*span);
        const auto  source   = registry.compiler_.getSourceManager().getBufferData(id);
        const auto  opening  = source.take_front(span->begin()).rfind("[[");
        const auto  closing  = source.find("]]", span->end());
        if (opening == llvm::StringRef::npos || closing == llvm::StringRef::npos) return span;
        const auto before = source.slice(opening + 2, span->begin());
        const auto after  = source.slice(span->end(), closing);
        if (before.trim().empty() && after.trim().empty()) {
            return pmacro::Span(span->source(),
                                static_cast<std::uint32_t>(opening),
                                static_cast<std::uint32_t>(closing + 2));
        }
        const auto next_comma = after.find(',');
        if (next_comma != llvm::StringRef::npos &&
            (before.trim().empty() || after.take_front(next_comma).trim().empty())) {
            auto end = next_comma + 1;
            while (end < after.size() && llvm::isSpace(after[end])) ++end;
            return pmacro::Span(
                span->source(), span->begin(), span->end() + static_cast<std::uint32_t>(end));
        }
        const auto previous_comma = before.rfind(',');
        if (previous_comma != llvm::StringRef::npos &&
            before.drop_front(previous_comma + 1).trim().empty()) {
            return pmacro::Span(span->source(),
                                static_cast<std::uint32_t>(opening + 2 + previous_comma),
                                span->end());
        }
        return span;
    };
    const auto span = calculate();
    attribute_spans_.emplace(&attribute, span);
    return span;
}

auto SourceRegistry::rewrite_span(clang::SourceRange range) const -> std::optional<Span> {
    if (range.getBegin().isMacroID() || range.getEnd().isMacroID()) return {};
    auto span = spelling(clang::CharSourceRange::getTokenRange(range));
    if (! span) return {};
    return *span;
}

auto SourceRegistry::main_span(std::uint32_t begin, std::uint32_t end) const -> Span {
    return Span(identify(compiler_.getSourceManager().getMainFileID()), begin, end);
}

auto SourceRegistry::valid(Span span) const -> bool {
    if (parent_) return parent_->valid(span);
    const auto id = file(span);
    if (id.isInvalid() || span.begin() > span.end()) return false;
    bool       invalid = false;
    const auto buffer  = compiler_.getSourceManager().getBufferData(id, &invalid);
    return ! invalid && span.end() <= buffer.size();
}

auto SourceRegistry::is_main(Span span) const -> bool {
    if (parent_) return parent_->is_main(span);
    return valid(span) && file(span) == compiler_.getSourceManager().getMainFileID();
}

auto SourceRegistry::location(Span span) const -> clang::SourceLocation {
    if (parent_) return parent_->location(span);
    if (! valid(span)) return {};
    return compiler_.getSourceManager()
        .getLocForStartOfFile(file(span))
        .getLocWithOffset(span.begin());
}

auto SourceRegistry::text(Span span) const -> std::optional<std::string> {
    if (parent_) return parent_->text(span);
    if (! valid(span)) return {};
    const auto buffer = compiler_.getSourceManager().getBufferData(file(span));
    return buffer.slice(span.begin(), span.end()).str();
}

auto SourceRegistry::fragment(clang::CharSourceRange range) const
    -> std::expected<CodeFragment, RepresentationError> {
    const auto span = spelling(range);
    if (! span) return std::unexpected(RepresentationError::Unavailable);
    auto source     = info(range.getAsRange());
    source.spelling = *span;
    if (! range.isTokenRange()) {
        const auto expanded = clang::Lexer::makeFileCharRange(
            range, compiler_.getSourceManager(), compiler_.getLangOpts());
        source.expansion = file_span(expanded);
    }
    return CodeFragment { *text(*span), CodeOrigin::Spelled, std::move(source) };
}

auto SourceRegistry::fragment_source(const CodeFragment& fragment, Span fallback) const
    -> std::expected<FragmentSource, FragmentError> {
    if (fragment.source.context != context_) return std::unexpected(FragmentError::ForeignSource);
    if (fragment.origin == CodeOrigin::Spelled) {
        if (! fragment.source.spelling || ! valid(*fragment.source.spelling) ||
            text(*fragment.source.spelling) != fragment.text)
            return std::unexpected(FragmentError::InvalidFragment);
        return FragmentSource { *fragment.source.spelling, true };
    }
    if (fragment.origin != CodeOrigin::Rendered)
        return std::unexpected(FragmentError::InvalidFragment);
    const auto anchor = fragment.source.diagnostic.value_or(fallback);
    if (! valid(anchor)) return std::unexpected(FragmentError::InvalidFragment);
    return FragmentSource { anchor, false };
}

auto DeclarationStore::attributes(const clang::Decl& declaration) -> const PreparedAttributes& {
    auto& prepared = declarations_[&declaration];
    if (prepared.attributes) return *prepared.attributes;
    PreparedAttributes result;
    for (const auto* attribute : declaration.attrs()) {
        if (const auto* annotation = llvm::dyn_cast<clang::AnnotateAttr>(attribute)) {
            const auto name = annotation->getAnnotation();
            if (name.starts_with("pmacro.helper:")) {
                HelperInfo helper;
                helper.owner  = name.drop_front(14).str();
                helper.source = sources_.info(attribute->getRange());
                if (annotation->args_size() == 2) {
                    const auto* begin =
                        llvm::dyn_cast<clang::IntegerLiteral>(*annotation->args_begin());
                    const auto* end =
                        llvm::dyn_cast<clang::IntegerLiteral>(*(annotation->args_begin() + 1));
                    if (begin && end)
                        helper.arguments = sources_.fragment(clang::CharSourceRange::getCharRange(
                            clang::SourceLocation::getFromRawEncoding(
                                begin->getValue().getZExtValue()),
                            clang::SourceLocation::getFromRawEncoding(
                                end->getValue().getZExtValue())));
                }
                result.helpers.push_back(std::move(helper));
                sources_.attribute_span(*annotation);
                result.helper_attributes.push_back({ annotation, result.helpers.back() });
                continue;
            }
            if (name.starts_with("pmacro.")) continue;
        }
        AttributeInfo value;
        if (const auto* scope = attribute->getScopeName())
            value.name = scope->getName().str() + "::";
        if (const auto* name = attribute->getSpelling()) value.name += name;
        value.source = sources_.info(attribute->getRange());
        value.syntax =
            sources_.fragment(clang::CharSourceRange::getTokenRange(attribute->getRange()));
        value.implicit = attribute->isImplicit();
        result.attributes.push_back(std::move(value));
    }
    prepared.attributes = std::move(result);
    return *prepared.attributes;
}

auto DeclarationStore::helpers(const clang::Decl& declaration)
    -> const std::vector<PreparedHelper>& {
    auto& prepared = declarations_[&declaration];
    if (prepared.helpers) return *prepared.helpers;
    std::vector<PreparedHelper> result;
    auto                        append = [&](const clang::Decl& item) {
        const auto& data = attributes(item);
        result.insert(result.end(), data.helper_attributes.begin(), data.helper_attributes.end());
    };
    append(declaration);
    if (const auto* context = llvm::dyn_cast<clang::DeclContext>(&declaration))
        for (const auto* child : context->decls()) {
            if (! child) continue;
            append(*child);
            if (const auto* function = llvm::dyn_cast<clang::FunctionDecl>(child))
                for (const auto* parameter : function->parameters())
                    if (parameter) append(*parameter);
        }
    prepared.helpers = std::move(result);
    return *prepared.helpers;
}

auto nameable_type(clang::QualType type) -> bool;

auto nameable_argument(const clang::TemplateArgument& argument) -> bool {
    if (argument.getKind() == clang::TemplateArgument::Type)
        return nameable_type(argument.getAsType());
    if (argument.getKind() == clang::TemplateArgument::Pack)
        for (const auto& item : argument.pack_elements())
            if (! nameable_argument(item)) return false;
    return true;
}

auto nameable_type(clang::QualType type) -> bool {
    const auto* value = type.getTypePtr();
    if (const auto* pointer = llvm::dyn_cast<clang::PointerType>(value))
        return nameable_type(pointer->getPointeeType());
    if (const auto* reference = llvm::dyn_cast<clang::ReferenceType>(value))
        return nameable_type(reference->getPointeeType());
    if (const auto* array = llvm::dyn_cast<clang::ArrayType>(value))
        return nameable_type(array->getElementType());
    if (const auto* paren = llvm::dyn_cast<clang::ParenType>(value))
        return nameable_type(paren->getInnerType());
    if (const auto* function = llvm::dyn_cast<clang::FunctionProtoType>(value)) {
        if (! nameable_type(function->getReturnType())) return false;
        for (auto parameter : function->param_types())
            if (! nameable_type(parameter)) return false;
    }
    if (const auto* specialization = llvm::dyn_cast<clang::TemplateSpecializationType>(value))
        for (const auto& argument : specialization->template_arguments())
            if (! nameable_argument(argument)) return false;
    if (const auto* tag = llvm::dyn_cast<clang::TagType>(value)) {
        if (! tag->getDecl()->getIdentifier()) return false;
        for (const clang::DeclContext* context = tag->getDecl()->getDeclContext(); context;
             context                           = context->getParent())
            if (const auto* record = llvm::dyn_cast<clang::RecordDecl>(context))
                if (! record->getIdentifier()) return false;
        if (const auto* specialization =
                llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(tag->getDecl()))
            for (const auto& argument : specialization->getTemplateArgs().asArray())
                if (! nameable_argument(argument)) return false;
    }
    return true;
}

auto DeclarationStore::type(clang::QualType value, SourceInfo source) const -> TypeInfo {
    TypeInfo result;
    result.source = std::move(source);
    if (value.isNull()) return result;
    auto policy                   = compiler_.getASTContext().getPrintingPolicy();
    policy.SuppressTagKeyword     = true;
    policy.SuppressUnwrittenScope = true;
    result.display                = value.getAsString(policy);
    result.dependent              = value->isDependentType();
    if (value->isUndeducedType()) {
        result.type_id = std::unexpected(RepresentationError::UndeducedType);
        return result;
    }
    if (result.dependent) {
        result.type_id = std::unexpected(RepresentationError::RequiresTemplateContext);
        return result;
    }
    if (! nameable_type(value)) {
        result.type_id = std::unexpected(RepresentationError::Unnameable);
        return result;
    }
    policy.SuppressScope      = false;
    policy.FullyQualifiedName = true;
    result.type_id =
        CodeFragment { value.getAsString(policy), CodeOrigin::Rendered, result.source };
    return result;
}

auto DeclarationStore::expression(const clang::Expr* expression) const -> ExpressionInfo {
    ExpressionInfo result;
    if (! expression) return result;
    result.source    = sources_.info(expression->getSourceRange());
    result.dependent = expression->isTypeDependent() || expression->isValueDependent();
    result.code =
        sources_.fragment(clang::CharSourceRange::getTokenRange(expression->getSourceRange()));
    if (! result.code && ! result.dependent) {
        std::string              text;
        llvm::raw_string_ostream out(text);
        expression->printPretty(out, nullptr, compiler_.getASTContext().getPrintingPolicy());
        out.flush();
        result.code = CodeFragment { std::move(text), CodeOrigin::Rendered, result.source };
    }
    return result;
}

auto DeclarationStore::function(const clang::FunctionDecl& declaration, FunctionForm form)
    -> FunctionInfo {
    const auto*  function = &declaration;
    FunctionInfo info;
    info.form = form;
    if (form == FunctionForm::Unavailable) {
        info.form = function->isDeletedAsWritten()             ? FunctionForm::Deleted
                    : function->isDefaulted()                  ? FunctionForm::Defaulted
                    : function->isThisDeclarationADefinition() ? FunctionForm::Definition
                                                               : FunctionForm::Declaration;
    }
    info.return_type =
        type(function->getReturnType(),
             sources_.info(function->getReturnTypeSourceRange(), function->getLocation()));
    info.language_linkage = function->getLanguageLinkage() == clang::CLanguageLinkage
                                ? LanguageLinkage::C
                                : LanguageLinkage::Cxx;
    info.variadic         = function->isVariadic();
    info.dependent  = function->isDependentContext() || function->getType()->isDependentType();
    info.template_  = function->getTemplatedKind() != clang::FunctionDecl::TK_NonTemplate;
    info.static_    = function->getStorageClass() == clang::SC_Static;
    info.inline_    = function->isInlineSpecified();
    info.constexpr_ = function->isConstexpr();
    info.consteval_ = function->isConsteval();
    if (const auto* prototype = function->getType()->getAs<clang::FunctionProtoType>()) {
        info.default_calling_convention = prototype->getCallConv() == clang::CC_C;
        info.exception.source =
            sources_.info(function->getExceptionSpecSourceRange(), function->getLocation());
        switch (prototype->getExceptionSpecType()) {
        case clang::EST_DependentNoexcept: info.exception.state = ExceptionState::Dependent; break;
        case clang::EST_Unevaluated:
        case clang::EST_Uninstantiated:
        case clang::EST_Unparsed: break;
        default:
            info.exception.state = prototype->isNothrow() ? ExceptionState::NonThrowing
                                                          : ExceptionState::PotentiallyThrowing;
            break;
        }
        if (const auto* condition = prototype->getNoexceptExpr())
            info.exception.condition = expression(condition);
    }
    for (const auto* parameter : function->parameters()) {
        ParameterInfo value;
        if (parameter->getIdentifier()) value.name = parameter->getNameAsString();
        value.source         = sources_.info(parameter->getSourceRange(), parameter->getLocation());
        value.declared_type  = type(parameter->getOriginalType(), value.source);
        value.type           = type(parameter->getType(), value.source);
        value.parameter_pack = parameter->isParameterPack();
        value.attributes     = attributes(*parameter).attributes;
        auto& argument       = value.default_argument;
        argument.inherited   = parameter->hasInheritedDefaultArg();
        if (parameter->hasDefaultArg()) {
            argument.state = DefaultArgumentState::Deferred;
            if (! parameter->hasUnparsedDefaultArg()) {
                const auto* expr = parameter->hasUninstantiatedDefaultArg()
                                       ? parameter->getUninstantiatedDefaultArg()
                                       : parameter->getDefaultArg();
                if (expr) {
                    argument.state      = DefaultArgumentState::Available;
                    argument.expression = expression(expr);
                }
            }
        }
        info.parameters.push_back(std::move(value));
    }
    return info;
}

auto DeclarationStore::build(const clang::Decl& declaration, FunctionForm form)
    -> std::expected<std::shared_ptr<const Declaration>, DeclarationError> {
    const auto* record      = llvm::dyn_cast<clang::RecordDecl>(&declaration);
    const auto* enumeration = llvm::dyn_cast<clang::EnumDecl>(&declaration);
    const auto* function    = llvm::dyn_cast<clang::FunctionDecl>(&declaration);
    if ((! record && ! enumeration && ! function) ||
        (function && (llvm::isa<clang::CXXMethodDecl>(function) || ! function->getIdentifier())))
        return std::unexpected(DeclarationError::UnsupportedDeclaration);
    if (declaration.isInvalidDecl()) return std::unexpected(DeclarationError::InvalidDeclaration);
    if ((record && ! record->isCompleteDefinition()) ||
        (enumeration && ! enumeration->isCompleteDefinition()))
        return std::unexpected(DeclarationError::IncompleteDefinition);
    auto        result = std::make_shared<Declaration>();
    const auto& named  = llvm::cast<clang::NamedDecl>(declaration);
    if (named.getIdentifier()) {
        result->name           = named.getNameAsString();
        result->qualified_name = named.getQualifiedNameAsString();
    }
    result->source     = sources_.info(declaration.getSourceRange(), declaration.getLocation());
    const auto& data   = attributes(declaration);
    result->attributes = data.attributes;
    result->helpers    = data.helpers;
    if (record) {
        RecordInfo info;
        info.kind      = record->isUnion()   ? RecordKind::Union
                         : record->isClass() ? RecordKind::Class
                                             : RecordKind::Struct;
        info.dependent = record->isDependentContext();
        if (const auto* cxx = llvm::dyn_cast<clang::CXXRecordDecl>(record)) {
            info.has_bases = cxx->getNumBases() != 0;
            for (const auto* child : cxx->decls()) {
                const auto* method = llvm::dyn_cast<clang::CXXMethodDecl>(child);
                if (const auto* wrapper = llvm::dyn_cast<clang::FunctionTemplateDecl>(child))
                    method = llvm::dyn_cast<clang::CXXMethodDecl>(wrapper->getTemplatedDecl());
                if (! method) continue;
                MethodInfo value;
                value.name             = method->getNameAsString();
                value.function         = this->function(*method, FunctionForm::Unavailable);
                value.function.static_ = method->isStatic();
                value.kind = llvm::isa<clang::CXXConstructorDecl>(method)  ? MethodKind::Constructor
                             : llvm::isa<clang::CXXDestructorDecl>(method) ? MethodKind::Destructor
                             : llvm::isa<clang::CXXConversionDecl>(method) ? MethodKind::Conversion
                             : method->isOverloadedOperator()              ? MethodKind::Operator
                                                                           : MethodKind::Ordinary;
                value.access = method->getAccess() == clang::AS_private     ? Access::Private
                               : method->getAccess() == clang::AS_protected ? Access::Protected
                                                                            : Access::Public;
                value.ref_qualifier =
                    method->getRefQualifier() == clang::RQ_LValue   ? RefQualifier::LValue
                    : method->getRefQualifier() == clang::RQ_RValue ? RefQualifier::RValue
                                                                    : RefQualifier::None;
                value.const_     = method->isConst();
                value.volatile_  = method->isVolatile();
                value.virtual_   = method->isVirtual();
                value.implicit   = method->isImplicit();
                value.source     = sources_.info(method->getSourceRange(), method->getLocation());
                const auto& data = attributes(*method);
                value.attributes = data.attributes;
                value.helpers    = data.helpers;
                info.methods.push_back(std::move(value));
            }
        }
        for (const auto* field : record->fields()) {
            FieldInfo value;
            if (field->getIdentifier()) value.name = field->getNameAsString();
            value.kind     = field->isAnonymousStructOrUnion() ? FieldKind::AnonymousRecord
                             : field->isUnnamedBitField()      ? FieldKind::UnnamedBitField
                                                               : FieldKind::Named;
            value.access   = field->getAccess() == clang::AS_private     ? Access::Private
                             : field->getAccess() == clang::AS_protected ? Access::Protected
                                                                         : Access::Public;
            value.mutable_ = field->isMutable();
            value.source   = sources_.info(field->getSourceRange(), field->getLocation());
            value.type     = type(field->getType(), value.source);
            if (field->isBitField()) value.bit_width = expression(field->getBitWidth());
            if (field->hasInClassInitializer())
                value.initializer = expression(field->getInClassInitializer());
            const auto& data = attributes(*field);
            value.attributes = data.attributes;
            value.helpers    = data.helpers;
            info.fields.push_back(std::move(value));
        }
        result->data = std::move(info);
    } else if (enumeration) {
        EnumInfo info;
        info.scoped                   = enumeration->isScoped();
        info.fixed_underlying_type    = enumeration->isFixed();
        info.explicit_underlying_type = enumeration->getIntegerTypeSourceInfo() != nullptr;
        info.underlying_type =
            type(enumeration->getIntegerType(), sources_.info(enumeration->getIntegerTypeRange()));
        for (const auto* item : enumeration->enumerators()) {
            EnumeratorInfo value;
            value.name   = item->getNameAsString();
            value.source = sources_.info(item->getSourceRange(), item->getLocation());
            if (item->getInitExpr()) value.initializer = expression(item->getInitExpr());
            const auto& data = attributes(*item);
            value.attributes = data.attributes;
            value.helpers    = data.helpers;
            info.enumerators.push_back(std::move(value));
        }
        result->data = std::move(info);
    } else {
        result->data = this->function(*function, form);
    }
    return result;
}

auto DeclarationStore::get(const clang::Decl& declaration, FunctionForm form)
    -> std::expected<std::shared_ptr<const Declaration>, DeclarationError> {
    auto& prepared = declarations_[&declaration];
    if (! prepared.snapshot) prepared.snapshot = build(declaration, form);
    return *prepared.snapshot;
}

} // namespace pmacro::clang_bridge
