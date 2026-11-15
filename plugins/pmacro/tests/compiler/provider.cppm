export module pmacro.compiler_test;

import std;
import pmacro;

auto helper_arguments(const pmacro::HelperInfo& helper,
                      std::string_view          owner,
                      std::string_view          first,
                      pmacro::Context&          context) -> bool {
    if (helper.owner != owner || ! helper.arguments || ! helper.source.spelling) return false;
    auto arguments = context.tokens(*helper.arguments);
    return arguments &&
           (first.empty() ? arguments->size() == 0
                          : arguments->size() != 0 && (*arguments)[0].spelling() == first);
}

[[pmacro::define("compiler-test")]]
auto helper_fields(pmacro::DeriveInput, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    auto first = context.declaration();
    auto again = context.declaration();
    if (! first || ! again || *first != *again) return std::unexpected(pmacro::Error::Failure);
    const auto& value  = **first;
    const auto* record = std::get_if<pmacro::RecordInfo>(&value.data);
    if (! record || record->fields.size() != 2 || value.helpers.size() != 1 ||
        ! helper_arguments(value.helpers[0], "compiler-test::helper_fields", "record", context))
        return std::unexpected(pmacro::Error::Failure);
    const auto& field = record->fields[0];
    const auto& spare = record->fields[1];
    if (field.helpers.size() != 2 || field.attributes.size() != 1 || spare.helpers.size() != 1 ||
        ! helper_arguments(field.helpers[0], "compiler-test::helper_fields", "default", context) ||
        ! helper_arguments(field.helpers[1], "compiler-test::helper_second", "rename", context) ||
        ! helper_arguments(spare.helpers[0], "compiler-test::helper_fields", "", context))
        return std::unexpected(pmacro::Error::Failure);
    const auto method = std::ranges::find(record->methods, "allows", &pmacro::MethodInfo::name);
    if (method == record->methods.end() || method->helpers.size() != 1 ||
        ! helper_arguments(method->helpers[0], "compiler-test::helper_fields", "method", context))
        return std::unexpected(pmacro::Error::Failure);
    const auto name = context.macro_identity() == "compiler-test::helper_fields"
                          ? "helper_fields_checked"
                          : "helper_second_checked";
    return context.parse(std::string("inline constexpr bool ") + name + " = true;");
}

[[pmacro::define("compiler-test")]]
auto helper_second(pmacro::DeriveInput input, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    return helper_fields(input, context);
}

auto check_helper_member(pmacro::Context& context) -> bool {
    auto snapshot = context.declaration();
    if (! snapshot) return false;
    if (const auto* record = std::get_if<pmacro::RecordInfo>(&(*snapshot)->data))
        return record->fields.size() == 1 && record->fields[0].helpers.size() == 1 &&
               helper_arguments(record->fields[0].helpers[0],
                                "compiler-test::helper_member",
                                "default",
                                context);
    const auto* enumeration = std::get_if<pmacro::EnumInfo>(&(*snapshot)->data);
    return enumeration && enumeration->enumerators.size() == 1 &&
           enumeration->enumerators[0].helpers.size() == 1 &&
           helper_arguments(enumeration->enumerators[0].helpers[0],
                            "compiler-test::helper_member",
                            "default",
                            context);
}

[[pmacro::define("compiler-test")]]
auto helper_member(pmacro::DeriveInput, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    if (! check_helper_member(context)) return std::unexpected(pmacro::Error::Failure);
    return context.stream();
}

[[pmacro::define("compiler-test")]]
auto helper_snapshot(pmacro::AttributeInput input, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    if (! check_helper_member(context)) return std::unexpected(pmacro::Error::Failure);
    return input.item;
}

[[pmacro::define("compiler-test")]]
auto context_info(pmacro::DeriveInput input, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    auto declaration = context.declaration();
    if (! declaration || ! (*declaration)->name) return std::unexpected(pmacro::Error::Failure);
    const auto& value  = **declaration;
    const auto& name   = *value.name;
    const bool  nested = name == "Nested";
    const auto  scope =
        nested ? pmacro::DeclarationScope::Record : pmacro::DeclarationScope::Namespace;
    const auto export_ = name == "Direct" ? pmacro::AfterItemExport::Required
                         : name == "InExportNamespace" || name == "InExportBlock"
                             ? pmacro::AfterItemExport::Inherited
                             : pmacro::AfterItemExport::NotExported;
    if (value.scope != scope || value.after_item_export != export_) {
        context.diagnostic(pmacro::DiagnosticLevel::Error,
                           input.item_span,
                           "declaration context mismatch: " + name);
        return std::unexpected(pmacro::Error::Failure);
    }
    if (nested) return context.stream();
    return context.parse((export_ == pmacro::AfterItemExport::Required ? "export " : "") +
                         std::string("constexpr int from_") + name + "() { return 42; }");
}

[[pmacro::define("compiler-test")]]
auto replace(pmacro::AttributeInput, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    return context.parse("struct Value { long value; Value* next; };");
}

[[pmacro::define("compiler-test")]]
auto after(pmacro::DeriveInput, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    return context.parse(
        "using generated_choice = Choice; constexpr int generated() { return 42; }");
}

[[pmacro::define("compiler-test")]]
auto at_end(pmacro::DeriveInput, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    auto stream = context.parse(
        "using later_type = Later; constexpr int deferred() { return sizeof(Later); }");
    if (! stream || ! context.emit(pmacro::OutputLocation::NamespaceEnd, *stream))
        return std::unexpected(pmacro::Error::Failure);
    return context.stream();
}

[[pmacro::define("compiler-test")]]
auto import_function(pmacro::AttributeInput, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    return context.parse("extern \"C\" int raw_import(int value) { return value+1; } int "
                         "imported(int value) { return raw_import(value); }");
}

[[pmacro::define("compiler-test")]]
auto export_function(pmacro::AttributeInput, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    return context.parse("int exported(int value) { return value+1; } extern \"C\" int wrapper(int "
                         "value) { return exported(value); }");
}

[[pmacro::define("compiler-test")]]
auto inspect(pmacro::AttributeInput input, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    auto first  = context.declaration();
    auto second = context.declaration();
    if (! first || ! second || *first != *second) return std::unexpected(pmacro::Error::Failure);
    const auto* record = std::get_if<pmacro::RecordInfo>(&(*first)->data);
    if (! record || record->fields.size() != 2 || ! record->fields[0].type.type_id)
        return std::unexpected(pmacro::Error::Failure);
    auto type   = context.tokens(*record->fields[0].type.type_id);
    auto output = context.parse("using inspected_type = ");
    auto end    = context.parse(";");
    if (! type || ! output || ! end || ! output->append(*type) || ! output->append(*end) ||
        ! context.emit(pmacro::OutputLocation::AfterItem, *output))
        return std::unexpected(pmacro::Error::Failure);
    return input.item;
}

[[pmacro::define("compiler-test")]]
auto inspect_template(pmacro::AttributeInput input, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    auto value = context.declaration();
    if (! value) return std::unexpected(pmacro::Error::Failure);
    const auto* record = std::get_if<pmacro::RecordInfo>(&(*value)->data);
    if (! record || record->fields.size() != 1 || ! record->dependent ||
        ! record->fields[0].type.dependent)
        return std::unexpected(pmacro::Error::Failure);
    auto once = context.parse("inline constexpr int template_once = 1;");
    if (! once || ! context.emit(pmacro::OutputLocation::AfterItem, *once))
        return std::unexpected(pmacro::Error::Failure);
    return input.item;
}

[[pmacro::define("compiler-test")]]
auto rewrite(pmacro::AttributeInput input, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    if (input.arguments.size() != 1) return std::unexpected(pmacro::Error::InvalidArgument);
    const auto text = input.arguments[0].spelling();
    return context.parse(text.substr(1, text.size() - 2));
}

[[pmacro::define("compiler-test")]]
auto derive_generated(pmacro::AttributeInput, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    return context.parse(
        "struct [[pmacro::derive(\"compiler-test::after\")]] Generated { long value; };");
}

std::shared_ptr<const pmacro::Declaration> saved_snapshot;

[[pmacro::define("compiler-test")]]
auto first(pmacro::DeriveInput, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    auto declaration = context.declaration();
    if (! declaration) return std::unexpected(pmacro::Error::Failure);
    saved_snapshot = *declaration;
    auto after     = context.parse("inline constexpr int second_value = first_value + 1;");
    auto end       = context.parse("inline constexpr int first_end = sizeof(Later);");
    if (! after || ! end || ! context.emit(pmacro::OutputLocation::AfterItem, *after) ||
        ! context.emit(pmacro::OutputLocation::NamespaceEnd, *end))
        return std::unexpected(pmacro::Error::Failure);
    return context.parse("inline constexpr int first_value = 1;");
}

[[pmacro::define("compiler-test")]]
auto second(pmacro::DeriveInput, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    auto declaration = context.declaration();
    if (! declaration || *declaration != saved_snapshot)
        return std::unexpected(pmacro::Error::Failure);
    auto end = context.parse("inline constexpr int second_end = first_end + 1;");
    if (! end || ! context.emit(pmacro::OutputLocation::NamespaceEnd, *end))
        return std::unexpected(pmacro::Error::Failure);
    return context.parse("inline constexpr int third_value = second_value + 1;");
}

[[pmacro::define("compiler-test")]]
auto invalid_input(pmacro::AttributeInput, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    auto declaration = context.declaration();
    auto again       = context.declaration();
    if (declaration || again ||
        declaration.error() != pmacro::DeclarationError::InvalidDeclaration ||
        again.error() != declaration.error())
        return std::unexpected(pmacro::Error::Failure);
    return context.parse("struct Invalid {};");
}

[[pmacro::define("compiler-test")]]
auto no_namespace(pmacro::AttributeInput input, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    auto output = context.stream();
    if (! output) return std::unexpected(pmacro::Error::Failure);
    auto result = context.emit(pmacro::OutputLocation::NamespaceEnd, *output);
    if (result || result.error() != pmacro::Error::InvalidArgument)
        return std::unexpected(pmacro::Error::Failure);
    return input.item;
}

[[pmacro::define("compiler-test")]]
auto warning(pmacro::AttributeInput input, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    context.diagnostic(
        pmacro::DiagnosticLevel::Warning, input.item_span, "serialized plugin warning");
    return input.item;
}

[[pmacro::define("compiler-test")]]
auto fail_after(pmacro::AttributeInput, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    auto output = context.parse("static_assert(false, \"uncommitted-output\");");
    if (output) (void)context.emit(pmacro::OutputLocation::AfterItem, *output);
    return std::unexpected(pmacro::Error::Failure);
}

[[pmacro::define("compiler-test")]]
auto budget(pmacro::AttributeInput input, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    auto output = context.parse("static_assert(true, \"" + std::string(1024 * 1024, 'x') + "\");");
    if (! output) return std::unexpected(pmacro::Error::Failure);
    for (int i = 0; i < 17; ++i)
        if (! context.emit(pmacro::OutputLocation::AfterItem, *output))
            return std::unexpected(pmacro::Error::Failure);
    return input.item;
}

[[pmacro::define("compiler-test")]]
auto import_at_end(pmacro::DeriveInput, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    auto output = context.parse("namespace nested { import available; }");
    if (! output || ! context.emit(pmacro::OutputLocation::NamespaceEnd, *output))
        return std::unexpected(pmacro::Error::Failure);
    return context.stream();
}

[[pmacro::define("compiler-test")]]
auto function(pmacro::AttributeInput input, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    auto first  = context.declaration();
    auto second = context.declaration();
    auto fail   = [&]() -> std::expected<pmacro::TokenStream, pmacro::Error> {
        context.diagnostic(
            pmacro::DiagnosticLevel::Error, input.item_span, "function model mismatch");
        return std::unexpected(pmacro::Error::Failure);
    };
    if (! first || ! second || *first != *second) return fail();
    const auto* f = std::get_if<pmacro::FunctionInfo>(&(*first)->data);
    if (! f || ! (*first)->name || ! f->default_calling_convention) return fail();
    const auto& name = *(*first)->name;
    if (name == "defaults") {
        if (f->parameters.size() != 2 || f->parameters[0].name ||
            f->parameters[0].default_argument.state != pmacro::DefaultArgumentState::Available ||
            f->parameters[0].default_argument.expression->code->text != "2" ||
            f->exception.state != pmacro::ExceptionState::NonThrowing ||
            f->form != pmacro::FunctionForm::Declaration)
            return fail();
    } else if (name == "defined") {
        if (f->form != pmacro::FunctionForm::Definition || f->parameters[0].name != "value" ||
            f->return_type.type_id->text != "int" || ! f->inline_ || ! f->static_ ||
            f->exception.state != pmacro::ExceptionState::PotentiallyThrowing)
            return fail();
    } else if (name == "deduced") {
        if (f->return_type.type_id ||
            f->return_type.type_id.error() != pmacro::RepresentationError::UndeducedType)
            return fail();
    } else if (name == "adjusted") {
        if (f->parameters.size() != 2 || f->parameters[0].type.type_id->text != "int *" ||
            f->parameters[0].declared_type.type_id->text != "int[3]" ||
            f->parameters[1].declared_type.type_id->text != "int (double)")
            return fail();
    } else if (name == "dependent") {
        if (! f->template_ || ! f->dependent || ! f->parameters[0].type.dependent ||
            f->exception.state != pmacro::ExceptionState::Dependent)
            return fail();
    } else if (name == "linked") {
        if (f->language_linkage != pmacro::LanguageLinkage::C) return fail();
    } else if (name == "pack") {
        if (! f->parameters[0].parameter_pack) return fail();
    } else if (name == "inherited") {
        if (f->parameters[0].default_argument.state != pmacro::DefaultArgumentState::None)
            return fail();
    } else if (name == "zero") {
        if (! f->parameters.empty() || ! f->constexpr_) return fail();
    } else if (name == "complex") {
        if (f->parameters.size() != 2 || ! f->parameters[0].type.type_id || ! f->variadic)
            return fail();
    } else
        return fail();
    if (f->return_type.type_id && ! context.tokens(*f->return_type.type_id)) return fail();
    return input.item;
}

[[pmacro::define("compiler-test")]]
auto unsupported_function(pmacro::AttributeInput input, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    auto first  = context.declaration();
    auto second = context.declaration();
    if (first || second || first.error() != pmacro::DeclarationError::UnsupportedDeclaration ||
        first.error() != second.error())
        return std::unexpected(pmacro::Error::Failure);
    return input.item;
}

[[pmacro::define("compiler-test")]]
auto changed_function(pmacro::AttributeInput input, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    auto first  = context.declaration();
    auto second = context.declaration();
    if (first || second || first.error() != pmacro::DeclarationError::ChangedInput ||
        first.error() != second.error())
        return std::unexpected(pmacro::Error::Failure);
    return input.item;
}

auto check_function_input(pmacro::FunctionInput input, pmacro::Context& context) noexcept -> bool {
    auto first    = context.declaration();
    auto second   = context.declaration();
    auto site     = context.call_site();
    auto previous = input.attribute_span.begin();
    for (auto token : input.arguments) {
        auto span = token.span();
        if (span.source() != input.invocation_span.source() || span.begin() < previous ||
            span.begin() <= input.attribute_span.begin() ||
            span.end() >= input.attribute_span.end() ||
            span.begin() < input.invocation_span.begin() ||
            span.end() > input.invocation_span.end() || span.end() <= span.begin())
            return false;
        previous = span.end();
    }
    return ! first && ! second &&
           first.error() == pmacro::DeclarationError::UnsupportedDeclaration &&
           second.error() == first.error() && input.scope == pmacro::DeclarationScope::Namespace &&
           input.arguments.valid() && site && input.attribute_span.source() != 0 &&
           input.attribute_span.source() == input.invocation_span.source() &&
           input.invocation_span.begin() <= input.attribute_span.begin() &&
           input.invocation_span.end() > input.attribute_span.end() &&
           context.join(input.attribute_span, input.invocation_span) &&
           context.provider_identity() == "compiler-test-provider" &&
           ! context.target_triple().empty();
}

auto exact_token_spans(pmacro::TokenStream tokens,
                       pmacro::Span        root,
                       std::string_view    source) noexcept -> bool {
    auto previous = root.begin();
    for (auto token : tokens) {
        auto span = token.span();
        if (span.source() != root.source() || span.begin() < previous ||
            span.end() <= span.begin() || span.begin() < root.begin() || span.end() > root.end())
            return false;
        auto text = source.substr(span.begin() - root.begin(), span.end() - span.begin());
        if (token.kind() == pmacro::TokenKind::Group) {
            const auto delimiter = token.delimiter();
            const auto open      = delimiter == pmacro::Delimiter::Parenthesis ? '('
                                   : delimiter == pmacro::Delimiter::Brace     ? '{'
                                                                               : '[';
            const auto close     = delimiter == pmacro::Delimiter::Parenthesis ? ')'
                                   : delimiter == pmacro::Delimiter::Brace     ? '}'
                                                                               : ']';
            if (text.size() < 2 || text.front() != open || text.back() != close ||
                ! exact_token_spans(token.children(), root, source))
                return false;
            for (auto child : token.children())
                if (child.span().begin() <= span.begin() || child.span().end() >= span.end())
                    return false;
        } else if (text != token.spelling()) {
            return false;
        }
        previous = span.end();
    }
    return true;
}

[[pmacro::define("compiler-test")]]
auto span_model(pmacro::FunctionInput input, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    constexpr auto source = std::string_view {
        "marker, { key: (long), text: \"a,b){}[]\", values: [3, {inner: missing::Type}] }"
    };
    if (! check_function_input(input, context) || input.arguments.size() != 3)
        return std::unexpected(pmacro::Error::Failure);
    auto first = input.arguments[0].span();
    auto last  = input.arguments[2].span();
    auto root  = pmacro::Span { first.source(), first.begin(), last.end() };
    if (root.end() - root.begin() != source.size() ||
        ! exact_token_spans(input.arguments, root, source))
        return std::unexpected(pmacro::Error::Failure);
    return context.parse("inline constexpr bool exact_invoke_spans = true;");
}

[[pmacro::define("compiler-test")]]
auto function_diagnostic(pmacro::FunctionInput input, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    if (! check_function_input(input, context) || input.arguments.size() != 1 ||
        input.arguments[0].delimiter() != pmacro::Delimiter::Brace)
        return std::unexpected(pmacro::Error::Failure);
    auto fields = input.arguments[0].children();
    if (fields.size() != 3 || fields[0].spelling() != "missing_field")
        return std::unexpected(pmacro::Error::Failure);
    context.diagnostic(pmacro::DiagnosticLevel::Error, fields[0].span(), "invoke field diagnostic");
    return std::unexpected(pmacro::Error::Failure);
}

auto splice_source(pmacro::TokenStream scaffold,
                   pmacro::TokenStream type,
                   pmacro::TokenStream initializer,
                   pmacro::Context&    context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    auto output = context.stream();
    if (! output) return output;
    for (auto token : scaffold) {
        if (token.kind() == pmacro::TokenKind::Ident && token.spelling() == "TYPE_SLOT") {
            if (! output->append(type)) return std::unexpected(pmacro::Error::Failure);
        } else if (token.kind() == pmacro::TokenKind::Ident && token.spelling() == "INIT_SLOT") {
            if (! output->append(initializer)) return std::unexpected(pmacro::Error::Failure);
        } else if (token.kind() == pmacro::TokenKind::Group) {
            auto children = splice_source(token.children(), type, initializer, context);
            if (! children) return children;
            auto group = context.group(token.delimiter(), *children, token.span());
            if (! group || ! output->append(*group)) return std::unexpected(pmacro::Error::Failure);
        } else if (! output->append(token)) {
            return std::unexpected(pmacro::Error::Failure);
        }
    }
    return output;
}

[[pmacro::define("compiler-test")]]
auto spanned_derive(pmacro::DeriveInput input, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    auto snapshot = context.declaration();
    if (! snapshot) return std::unexpected(pmacro::Error::Failure);
    const auto* record = std::get_if<pmacro::RecordInfo>(&(*snapshot)->data);
    if (! record || record->fields.size() != 1 || ! record->fields[0].type.type_id ||
        ! record->fields[0].initializer || ! record->fields[0].initializer->code)
        return std::unexpected(pmacro::Error::Failure);
    const auto& field       = record->fields[0];
    auto        type        = context.tokens(*field.type.type_id);
    auto        initializer = context.tokens(*field.initializer->code);
    if (! type || ! initializer || type->size() == 0)
        return std::unexpected(pmacro::Error::Failure);
    const bool reject   = ! (*snapshot)->helpers.empty();
    auto       scaffold = context.parse(
        reject
            ? "using rejected = typename TYPE_SLOT::Missing;"
            : "using snapshot_type = TYPE_SLOT; inline constexpr auto snapshot_value = INIT_SLOT;",
        (*type)[0].span());
    if (! scaffold) return scaffold;
    auto output = splice_source(*scaffold, *type, *initializer, context);
    if (! output || ! context.emit(pmacro::OutputLocation::AfterItem, *output))
        return std::unexpected(pmacro::Error::Failure);
    return context.stream();
}

[[pmacro::define("compiler-test")]]
auto spanned_record(pmacro::FunctionInput input, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    if (input.arguments.size() != 5) return std::unexpected(pmacro::Error::InvalidArgument);
    auto type          = input.arguments[0].children();
    auto initializer   = input.arguments[2].children();
    auto invalid       = context.parse("ignored", pmacro::Span {});
    auto unbalanced    = context.parse("(", input.invocation_span);
    auto invalid_group = context.group(pmacro::Delimiter::None, type, input.invocation_span);
    if (invalid || invalid_group || unbalanced ||
        invalid.error() != pmacro::Error::InvalidArgument ||
        unbalanced.error() != pmacro::Error::InvalidArgument ||
        invalid_group.error() != pmacro::Error::InvalidArgument)
        return std::unexpected(pmacro::Error::Failure);
    auto cyclic = context.stream();
    if (! cyclic) return cyclic;
    auto recursive = context.group(pmacro::Delimiter::Brace, *cyclic, input.invocation_span);
    if (! recursive || cyclic->append(*recursive)) return std::unexpected(pmacro::Error::Failure);
    const bool reject = input.arguments[4].spelling() == "reject";
    auto       scaffold =
        context.parse(reject ? "struct [[pmacro::derive(\"compiler-test::spanned_derive\"), "
                               "pmacro::helper(\"compiler-test::spanned_derive\", reject)]] "
                               "Spanned { TYPE_SLOT value = INIT_SLOT; };"
                             : "struct [[pmacro::derive(\"compiler-test::spanned_derive\")]] "
                               "Spanned { TYPE_SLOT value = INIT_SLOT; };",
                      input.invocation_span);
    if (! scaffold) return scaffold;
    return splice_source(*scaffold, type, initializer, context);
}

[[pmacro::define("compiler-test")]]
auto spanned_output(pmacro::FunctionInput input, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    if (input.arguments.size() < 3) return std::unexpected(pmacro::Error::InvalidArgument);
    auto output = context.parse("using Spanned =", input.invocation_span);
    auto end    = context.parse(";", input.invocation_span);
    auto type   = context.stream();
    if (! output || ! end || ! type) return std::unexpected(pmacro::Error::Failure);
    for (std::size_t i = 2; i < input.arguments.size(); ++i)
        if (! type->append(input.arguments[i])) return std::unexpected(pmacro::Error::Failure);
    if (! output->append(*type) || ! output->append(*end))
        return std::unexpected(pmacro::Error::Failure);
    const auto mode = input.arguments[0].spelling();
    if (mode == "returned") return output;
    const auto location =
        mode == "after" ? pmacro::OutputLocation::AfterItem : pmacro::OutputLocation::NamespaceEnd;
    if (! context.emit(location, *output)) return std::unexpected(pmacro::Error::Failure);
    return context.stream();
}

[[pmacro::define("compiler-test")]]
auto declarations(pmacro::FunctionInput input, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    if (! check_function_input(input, context) ||
        context.macro_identity() != "compiler-test::declarations")
        return std::unexpected(pmacro::Error::Failure);
    return input.arguments;
}

[[pmacro::define("compiler-test")]]
auto function_context(pmacro::FunctionInput input, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    if (! check_function_input(input, context) || input.arguments.size() != 3 ||
        input.arguments[0].kind() != pmacro::TokenKind::Ident ||
        input.arguments[1].spelling() != ",")
        return std::unexpected(pmacro::Error::Failure);
    const auto name    = input.arguments[0].spelling();
    const auto mode    = input.arguments[2].spelling();
    const auto export_ = mode == "Required"    ? pmacro::AfterItemExport::Required
                         : mode == "Inherited" ? pmacro::AfterItemExport::Inherited
                                               : pmacro::AfterItemExport::NotExported;
    if (input.after_item_export != export_) return std::unexpected(pmacro::Error::Failure);
    return context.parse(
        std::string(export_ == pmacro::AfterItemExport::Required ? "export " : "") +
        "constexpr int invoked_" + std::string(name) + "() { return 42; }");
}

[[pmacro::define("compiler-test")]]
auto entity_input(pmacro::FunctionInput input, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    if (! check_function_input(input, context) || input.arguments.size() != 3 ||
        input.arguments[0].kind() != pmacro::TokenKind::Ident ||
        input.arguments[1].spelling() != "," ||
        input.arguments[2].delimiter() != pmacro::Delimiter::Brace)
        return std::unexpected(pmacro::Error::Failure);
    auto fields = input.arguments[2].children();
    if (fields.size() != 7 || fields[0].spelling() != "value" || fields[1].spelling() != ":" ||
        fields[2].delimiter() != pmacro::Delimiter::Parenthesis || fields[3].spelling() != "," ||
        fields[4].spelling() != "options" || fields[5].spelling() != ":" ||
        fields[6].delimiter() != pmacro::Delimiter::Bracket)
        return std::unexpected(pmacro::Error::Failure);
    auto options = fields[6].children();
    if (options.size() != 3 || options[0].spelling() != "1" || options[1].spelling() != "," ||
        options[2].delimiter() != pmacro::Delimiter::Brace)
        return std::unexpected(pmacro::Error::Failure);
    auto type = fields[2].children();
    if (type.size() != 1 || type[0].kind() != pmacro::TokenKind::Ident)
        return std::unexpected(pmacro::Error::Failure);
    return context.parse("struct " + std::string(input.arguments[0].spelling()) + " { " +
                         std::string(type[0].spelling()) + " value; };");
}

[[pmacro::define("compiler-test")]]
auto function_positions(pmacro::FunctionInput input, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    if (! check_function_input(input, context)) return std::unexpected(pmacro::Error::Failure);
    auto after = context.parse("inline constexpr int invoked_after = invoked_first + 1;");
    auto end   = context.parse("using invoked_later_type = Later; "
                               "constexpr int invoked_end() { return sizeof(Later); }");
    if (! after || ! end || ! context.emit(pmacro::OutputLocation::AfterItem, *after) ||
        ! context.emit(pmacro::OutputLocation::NamespaceEnd, *end))
        return std::unexpected(pmacro::Error::Failure);
    return context.parse("inline constexpr int invoked_first = 41;");
}

[[pmacro::define("compiler-test")]]
auto function_emit(pmacro::FunctionInput input, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    if (! context.emit(pmacro::OutputLocation::NamespaceEnd, input.arguments))
        return std::unexpected(pmacro::Error::Failure);
    return context.stream();
}

[[pmacro::define("compiler-test")]]
auto function_global_end(pmacro::FunctionInput, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    auto output = context.stream();
    if (! output) return std::unexpected(pmacro::Error::Failure);
    auto result = context.emit(pmacro::OutputLocation::NamespaceEnd, *output);
    if (result || result.error() != pmacro::Error::InvalidArgument)
        return std::unexpected(pmacro::Error::Failure);
    return output;
}

[[pmacro::define("compiler-test")]]
auto function_fail(pmacro::FunctionInput, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    auto output = context.parse("static_assert(false, \"uncommitted-invoke-output\");");
    if (output) {
        (void)context.emit(pmacro::OutputLocation::AfterItem, *output);
        (void)context.emit(pmacro::OutputLocation::NamespaceEnd, *output);
    }
    return std::unexpected(pmacro::Error::Failure);
}

[[pmacro::define("compiler-test")]]
auto function_budget(pmacro::FunctionInput, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    auto output = context.parse("static_assert(true, \"" + std::string(1024 * 1024, 'x') + "\");");
    if (! output) return std::unexpected(pmacro::Error::Failure);
    for (int i = 0; i < 17; ++i)
        if (! context.emit(pmacro::OutputLocation::AfterItem, *output))
            return std::unexpected(pmacro::Error::Failure);
    return context.stream();
}

[[pmacro::define("compiler-test")]]
auto function_loop(pmacro::FunctionInput, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    return context.parse("[[pmacro::invoke(\"compiler-test::function_loop\")]];");
}

[[pmacro::define("compiler-test")]]
auto function_depth(pmacro::FunctionInput input, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    if (input.arguments.size() != 1) return std::unexpected(pmacro::Error::InvalidArgument);
    auto value        = unsigned {};
    auto text         = input.arguments[0].spelling();
    auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc {} || end != text.data() + text.size())
        return std::unexpected(pmacro::Error::InvalidArgument);
    if (value == 0) return context.parse("inline constexpr int invoked_depth = 64;");
    return context.parse("[[pmacro::invoke(\"compiler-test::function_depth\", " +
                         std::to_string(value - 1) + ")]];");
}

[[pmacro::define("compiler-test")]]
auto invoke_from_derive(pmacro::DeriveInput, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    return context.parse("[[pmacro::invoke(\"compiler-test::declarations\", "
                         "inline constexpr int invoked_from_derive = 42;)]];");
}

static_assert(pmacro::host::macro_kind(pmacro::host::FunctionCallback { &declarations }) ==
              pmacro::host::MacroKind::Function);
static_assert(pmacro::host::macro_kind(pmacro::host::AttributeCallback { &replace }) ==
              pmacro::host::MacroKind::Attr);
static_assert(pmacro::host::macro_kind(pmacro::host::DeriveCallback { &after }) ==
              pmacro::host::MacroKind::Derive);
