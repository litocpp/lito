#pragma once

struct MacroControl {
    const clang::AnnotateAttr* attribute;
    std::string                arguments;
    bool                       derive {};
};

struct PreparedInput {
    clang::Decl*               declaration {};
    Span                       span;
    std::string                source;
    std::vector<clang::Token>  tokens;
    std::size_t                current {};
    std::optional<std::size_t> body;
    std::vector<MacroControl>  controls;
    clang::SourceLocation      end;
    clang::SourceLocation      namespace_end;
    std::optional<std::expected<std::shared_ptr<const Declaration>, DeclarationError>> snapshot;
    bool function_input_changed = false;
};

class Compilation;
thread_local std::unordered_map<clang::Sema*, Compilation*> compilations;

class Compilation final : public clang::SemaConsumer {
public:
    Compilation(clang::CompilerInstance& compiler, ExpansionOptions options)
        : compiler_(compiler),
          options_(std::move(options)),
          state_(compiler),
          definitions_(compiler, options_) {
        state_.declaration_query = [this](const clang::Decl& declaration) {
            auto found = prepared_.find(const_cast<clang::Decl*>(&declaration));
            if (llvm::isa<clang::FunctionDecl>(declaration) && found != prepared_.end()) {
                auto& input = found->second;
                if (! input.snapshot) {
                    if (input.function_input_changed)
                        input.snapshot = std::unexpected(DeclarationError::ChangedInput);
                    else {
                        const auto form =
                            input.body ? FunctionForm::Definition : FunctionForm::Declaration;
                        input.snapshot = state_.declaration(declaration, form);
                    }
                }
                return *input.snapshot;
            }
            const auto* record = llvm::dyn_cast<clang::CXXRecordDecl>(&declaration);
            if (! record || ! record->isBeingDefined() || found == prepared_.end())
                return state_.declaration(declaration);
            auto& input = found->second;
            if (! input.snapshot)
                input.snapshot = analyze_declaration(
                    compiler_, state_.sources(), declaration, clean_input(input), input.span);
            return *input.snapshot;
        };
    }

    auto InitializeSema(clang::Sema& sema) -> void override {
        sema_ = &sema;
        compilations.emplace(sema_, this);
    }
    auto ForgetSema() -> void override {
        compilations.erase(sema_);
        sema_ = nullptr;
    }

    auto prepare(clang::Decl& declaration) -> void {
        if (annotation(declaration, derive_annotation)) derivations_.insert(&declaration);
        if (compiler_.getDiagnostics().hasErrorOccurred() || prepared_.contains(&declaration))
            return;
        auto input = capture(declaration);
        if (! input) return;
        auto [entry, inserted] = prepared_.emplace(&declaration, std::move(*input));
        for (const auto& control : entry->second.controls)
            if (! control.derive) {
                expand_attribute(entry->second, *control.attribute);
                break;
            }
    }

    auto HandleTopLevelDecl(clang::DeclGroupRef group) -> bool override {
        for (auto* declaration : group) {
            if (! options_.provider.empty()) definitions_.observe(declaration);
            finish_declaration(*declaration);
        }
        if (options_.provider.empty()) return ! compiler_.getDiagnostics().hasErrorOccurred();
        auto text = definitions_.registrations();
        if (! text.empty()) {
            auto tokens = lex(text);
            tokens.push_back(parser().getCurToken());
            enter(std::move(tokens));
            parser().ConsumeAnyToken();
        }
        return ! compiler_.getDiagnostics().hasErrorOccurred();
    }

    auto HandleTagDeclDefinition(clang::TagDecl* declaration) -> void override {
        auto found = prepared_.find(declaration);
        if (found == prepared_.end() || ! completed_.insert(declaration).second) return;
        auto attributes = std::vector<const clang::AnnotateAttr*> {};
        for (auto* attribute : declaration->specific_attrs<clang::AnnotateAttr>())
            if (attribute->getAnnotation().starts_with(derive_annotation))
                attributes.push_back(attribute);
        if (attributes.empty()) return;
        auto&                           input = found->second;
        std::unordered_set<std::string> identities;
        for (const auto* attribute : attributes) {
            auto identity = attribute->getAnnotation().drop_front(derive_annotation.size()).str();
            if (! identities.insert(identity).second) {
                fail(attribute->getLocation(),
                     "a derive macro identity may appear only once on an item");
                return;
            }
        }
        for (const auto& helper : state_.helpers(*declaration))
            if (! identities.contains(helper.value.owner)) {
                fail(helper.attribute->getLocation(),
                     "'pmacro::helper' owner is not present in the enclosing derive group");
                return;
            }
        for (const auto& helper : state_.helpers(*declaration))
            owned_helpers_.insert(helper.attribute);
        std::vector<clang::Token> after;
        for (const auto* attribute : attributes) {
            auto output = invoke(input, *attribute, true);
            if (! output) return;
            append(after, lex(*output));
            for (const auto& [location, stream] : state_.take_outputs()) {
                auto text = state_.render(stream);
                if (! text) {
                    fail(attribute->getLocation(), "invalid appended TokenStream");
                    return;
                }
                if (location == OutputLocation::AfterItem)
                    append(after, lex(*text));
                else
                    insert_before(input.namespace_end, lex(*text));
            }
        }
        insert_after(input.end, std::move(after));
    }

    auto HandleTranslationUnit(clang::ASTContext&) -> void override {
        definitions_.validate();
        for (const auto* declaration : derivations_)
            if (! completed_.contains(declaration))
                fail(declaration->getLocation(),
                     "'pmacro::derive' requires a complete record, union, or enum definition");
    }

private:
    auto finish_declaration(clang::Decl& declaration) -> void {
        if (declaration.isFromASTFile()) return;
        if (! finished_.insert(&declaration).second) return;
        if (auto* templated = llvm::dyn_cast<clang::TemplateDecl>(&declaration))
            if (auto* item = templated->getTemplatedDecl()) finish_declaration(*item);
        if (auto* context = llvm::dyn_cast<clang::DeclContext>(&declaration))
            for (auto* member : context->decls()) finish_declaration(*member);
        if (auto* function = llvm::dyn_cast<clang::FunctionDecl>(&declaration))
            for (auto* parameter : function->parameters()) finish_declaration(*parameter);
        std::vector<clang::Attr*> retained;
        for (auto* attribute : declaration.attrs()) {
            auto* control = llvm::dyn_cast<clang::AnnotateAttr>(attribute);
            if (! control || ! control->getAnnotation().starts_with("pmacro.")) {
                retained.push_back(attribute);
                continue;
            }
            if (control->getAnnotation().starts_with(helper_annotation) &&
                ! owned_helpers_.contains(control))
                fail(control->getLocation(),
                     "'pmacro::helper' is not owned by an active derive invocation");
            if (control->getAnnotation().starts_with(define_annotation) &&
                options_.provider.empty())
                fail(control->getLocation(), "'pmacro::define' requires a provider compilation");
        }
        declaration.dropAttrs();
        for (auto* attribute : retained) declaration.addAttr(attribute);
    }
    auto parser() -> clang::Parser& {
        // Normal ParseAST registers its Parser as the completion handler for its lifetime.
        return *static_cast<clang::Parser*>(compiler_.getPreprocessor().getCodeCompletionHandler());
    }
    auto fail(clang::SourceLocation location, llvm::StringRef message) -> void {
        report(compiler_.getDiagnostics(), location, clang::DiagnosticsEngine::Error, message);
    }
    static auto annotation(const clang::Decl& declaration, llvm::StringRef prefix)
        -> const clang::AnnotateAttr* {
        for (auto* attribute : declaration.specific_attrs<clang::AnnotateAttr>())
            if (attribute->getAnnotation().starts_with(prefix)) return attribute;
        return nullptr;
    }
    static auto append(std::vector<clang::Token>& into, std::vector<clang::Token> tokens) -> void {
        into.insert(into.end(), tokens.begin(), tokens.end());
    }
    auto enter(std::vector<clang::Token> tokens) -> void {
        if (tokens.empty()) return;
        auto owned = std::make_unique<clang::Token[]>(tokens.size());
        std::copy(tokens.begin(), tokens.end(), owned.get());
        compiler_.getPreprocessor().EnterTokenStream(std::move(owned), tokens.size(), true, true);
    }
    auto raw(clang::SourceLocation begin) -> llvm::ArrayRef<clang::Token> {
        auto& manager          = compiler_.getSourceManager();
        auto  file             = manager.getFileID(begin);
        auto [entry, inserted] = files_.try_emplace(file.getHashValue());
        auto& tokens           = entry->second;
        if (inserted) {
            auto         buffer = manager.getBufferOrFake(file);
            auto         first  = manager.getLocForStartOfFile(file);
            clang::Lexer lexer(first,
                               compiler_.getLangOpts(),
                               buffer.getBufferStart(),
                               buffer.getBufferStart(),
                               buffer.getBufferEnd());
            for (;;) {
                clang::Token token;
                lexer.LexFromRawLexer(token);
                if (token.is(clang::tok::eof)) break;
                if (token.is(clang::tok::comment)) continue;
                if (token.is(clang::tok::raw_identifier))
                    compiler_.getPreprocessor().LookUpIdentifierInfo(token);
                tokens.push_back(token);
            }
        }
        auto first =
            std::lower_bound(tokens.begin(),
                             tokens.end(),
                             manager.getFileOffset(begin),
                             [&](const auto& token, unsigned offset) {
                                 return manager.getFileOffset(token.getLocation()) < offset;
                             });
        return llvm::ArrayRef<clang::Token>(tokens).drop_front(first - tokens.begin());
    }
    auto lex(llvm::StringRef text) -> std::vector<clang::Token> {
        auto& manager = compiler_.getSourceManager();
        auto  file =
            manager.createFileID(llvm::MemoryBuffer::getMemBufferCopy(text, "<pmacro-output>"));
        origins_[file.getHashValue()] = active_trace_;
        auto tokens                   = raw(manager.getLocForStartOfFile(file));
        return { tokens.begin(), tokens.end() };
    }
    auto namespace_end(clang::DeclContext* context) -> clang::SourceLocation {
        for (; context; context = context->getLexicalParent()) {
            auto* ns = llvm::dyn_cast<clang::NamespaceDecl>(context);
            if (! ns) continue;
            if (auto found = namespace_ends_.find(ns); found != namespace_ends_.end())
                return found->second;
            unsigned depth = 0;
            for (const auto& token : raw(ns->getBeginLoc())) {
                if (token.is(clang::tok::l_brace)) ++depth;
                if (token.is(clang::tok::r_brace) && depth && ! --depth) {
                    namespace_ends_.emplace(ns, token.getLocation());
                    return token.getLocation();
                }
            }
            break;
        }
        return {};
    }
    auto capture(clang::Decl& declaration) -> std::optional<PreparedInput> {
        auto& p = parser();
        if (llvm::isa<clang::FunctionDecl>(declaration) &&
            ! p.getCurToken().isOneOf(clang::tok::semi, clang::tok::l_brace)) {
            fail(declaration.getLocation(),
                 "function macro requires an unconsumed declaration or body");
            return {};
        }
        auto  begin   = declaration.getBeginLoc();
        auto& manager = compiler_.getSourceManager();
        for (const auto* attribute : declaration.specific_attrs<clang::AnnotateAttr>()) {
            auto span = state_.sources().attribute_span(*attribute);
            if (span) {
                auto location = state_.sources().location(*span);
                if (manager.isBeforeInTranslationUnit(location, begin)) begin = location;
            }
        }
        if (begin.isInvalid() || begin.isMacroID()) {
            fail(declaration.getLocation(), "cannot locate macro input");
            return {};
        }
        PreparedInput input;
        input.declaration   = &declaration;
        input.namespace_end = namespace_end(declaration.getLexicalDeclContext());
        auto tokens         = raw(begin);
        auto current        = std::find_if(tokens.begin(), tokens.end(), [&](const auto& t) {
            return t.getLocation() == p.getCurToken().getLocation();
        });
        if (current == tokens.end()) {
            fail(declaration.getLocation(), "macro input is not at a declaration boundary");
            return {};
        }
        if (llvm::isa<clang::FunctionDecl>(declaration)) {
            unsigned depth = 0;
            for (auto token = tokens.begin(); token != current; ++token) {
                if (token->isOneOf(clang::tok::l_paren, clang::tok::l_square, clang::tok::l_brace))
                    ++depth;
                if (token->isOneOf(
                        clang::tok::r_paren, clang::tok::r_square, clang::tok::r_brace) &&
                    depth)
                    --depth;
                if (token->is(clang::tok::semi) && ! depth) {
                    fail(declaration.getLocation(),
                         "function macro requires an unconsumed declaration or body");
                    return {};
                }
            }
        }
        input.current   = current - tokens.begin();
        unsigned parens = 0, brackets = 0, braces = 0;
        auto     end = input.current;
        for (; end < tokens.size(); ++end) {
            const auto& t = tokens[end];
            if (t.is(clang::tok::l_paren)) ++parens;
            if (t.is(clang::tok::r_paren) && parens) --parens;
            if (t.is(clang::tok::l_square)) ++brackets;
            if (t.is(clang::tok::r_square) && brackets) --brackets;
            if (t.is(clang::tok::l_brace)) {
                if (! parens && ! brackets && ! braces && ! input.body) input.body = end;
                ++braces;
            }
            if (t.is(clang::tok::r_brace) && braces) {
                --braces;
                if (! braces && ! parens && ! brackets &&
                    llvm::isa<clang::FunctionDecl>(declaration))
                    break;
            }
            if (t.is(clang::tok::semi) && ! parens && ! brackets && ! braces) break;
        }
        if (end == tokens.size()) {
            fail(begin, "unterminated macro input declaration");
            return {};
        }
        input.end = tokens[end].getLocation();
        input.tokens.assign(tokens.begin(), tokens.begin() + end + 1);
        auto span =
            state_.sources().spelling(clang::CharSourceRange::getTokenRange(begin, input.end));
        if (! span) {
            fail(begin, "macro input has no contiguous source range");
            return {};
        }
        input.span     = *span;
        input.source   = *state_.sources().text(*span);
        input.controls = controls(input);
        return input;
    }
    auto insert_before(clang::SourceLocation anchor, std::vector<clang::Token> output) -> void {
        if (output.empty()) return;
        if (anchor.isInvalid()) {
            fail({}, "macro output has no namespace target");
            return;
        }
        std::vector<clang::Token> replay;
        auto&                     pp = compiler_.getPreprocessor();
        for (;;) {
            clang::Token token;
            pp.Lex(token);
            if (token.is(clang::tok::eof)) {
                fail(anchor, "macro output target is no longer in the parser input");
                replay.push_back(token);
                break;
            }
            if (token.getLocation() == anchor) {
                append(replay, std::move(output));
                replay.push_back(token);
                break;
            }
            replay.push_back(token);
        }
        enter(std::move(replay));
    }
    auto insert_after(clang::SourceLocation anchor, std::vector<clang::Token> output) -> void {
        if (output.empty()) return;
        if (parser().getCurToken().getLocation() == anchor) {
            enter(std::move(output));
            return;
        }
        std::vector<clang::Token> replay;
        auto&                     pp = compiler_.getPreprocessor();
        for (;;) {
            clang::Token token;
            pp.Lex(token);
            replay.push_back(token);
            if (token.getLocation() == anchor) {
                append(replay, std::move(output));
                break;
            }
            if (token.is(clang::tok::eof)) {
                fail(anchor, "macro declaration end is no longer in the parser input");
                break;
            }
        }
        enter(std::move(replay));
    }
    auto controls(const PreparedInput& input) -> std::vector<MacroControl> {
        auto        result       = std::vector<MacroControl> {};
        const auto  limit        = input.body.value_or(input.tokens.size());
        const auto& tokens       = input.tokens;
        bool        in_attribute = false;
        for (std::size_t i = 0; i + 4 < limit; ++i) {
            if (tokens[i].is(clang::tok::l_square) && tokens[i + 1].is(clang::tok::l_square)) {
                in_attribute = true;
                ++i;
                continue;
            }
            if (tokens[i].is(clang::tok::r_square) && tokens[i + 1].is(clang::tok::r_square)) {
                in_attribute = false;
                ++i;
                continue;
            }
            if (! in_attribute) continue;
            auto spelling = [&](std::size_t at) {
                return compiler_.getPreprocessor().getSpelling(tokens[at]);
            };
            const auto kind = spelling(i + 2);
            if (spelling(i) != "pmacro" || ! tokens[i + 1].is(clang::tok::coloncolon) ||
                (kind != "attr" && kind != "derive") || ! tokens[i + 3].is(clang::tok::l_paren) ||
                ! tokens[i + 4].is(clang::tok::string_literal))
                continue;
            clang::StringLiteralParser literal(llvm::ArrayRef<clang::Token>(&tokens[i + 4], 1),
                                               compiler_.getPreprocessor());
            if (literal.hadError) continue;
            unsigned depth = 1;
            auto     end   = i + 4;
            for (++end; end < limit; ++end) {
                if (tokens[end].is(clang::tok::l_paren)) ++depth;
                if (tokens[end].is(clang::tok::r_paren) && ! --depth) break;
            }
            if (end == limit) continue;
            std::string arguments;
            if (i + 5 < end && tokens[i + 5].is(clang::tok::comma) && i + 6 < end) {
                arguments = clang::Lexer::getSourceText(
                                clang::CharSourceRange::getTokenRange(
                                    tokens[i + 6].getLocation(), tokens[end - 1].getLocation()),
                                compiler_.getSourceManager(),
                                compiler_.getLangOpts())
                                .str();
            }
            const bool derive = kind == "derive";
            for (auto at = i + 4; at < end; at += 2) {
                if (! tokens[at].is(clang::tok::string_literal)) break;
                clang::StringLiteralParser identity(llvm::ArrayRef<clang::Token>(&tokens[at], 1),
                                                    compiler_.getPreprocessor());
                if (identity.hadError) break;
                auto* attribute = clang::AnnotateAttr::Create(
                    compiler_.getASTContext(),
                    (derive ? derive_annotation : attr_annotation).str() +
                        identity.GetString().str(),
                    nullptr,
                    0,
                    clang::SourceRange(tokens[i].getLocation(), tokens[end].getLocation()));
                result.push_back({ attribute, arguments, derive });
                if (! derive) break;
            }
            i = end;
        }
        return result;
    }
    auto mask_control(std::string& text, Span input, const clang::AnnotateAttr& attribute) -> void {
        auto span = state_.sources().attribute_span(attribute);
        if (! span || span->source() != input.source() || span->begin() < input.begin() ||
            span->end() > input.end())
            return;
        for (auto i = span->begin(); i < span->end(); ++i)
            if (text[i - input.begin()] != '\n') text[i - input.begin()] = ' ';
    }
    auto definition_open(const std::vector<clang::Token>& tokens) -> std::optional<std::size_t> {
        unsigned parens = 0, brackets = 0;
        for (std::size_t i = 0; i < tokens.size(); ++i) {
            auto& t = tokens[i];
            if (t.is(clang::tok::l_paren)) ++parens;
            if (t.is(clang::tok::r_paren) && parens) --parens;
            if (t.is(clang::tok::l_square)) ++brackets;
            if (t.is(clang::tok::r_square) && brackets) --brackets;
            if (! parens && ! brackets && t.is(clang::tok::l_brace)) return i;
        }
        return {};
    }
    auto clean_input(const PreparedInput& input) -> std::string {
        auto text = input.source;
        for (const auto& control : input.controls)
            mask_control(text, input.span, *control.attribute);
        auto tokens = lex(text);
        for (std::size_t i = 0; i + 3 < tokens.size(); ++i)
            if (tokens[i].is(clang::tok::l_square) && tokens[i + 1].is(clang::tok::l_square) &&
                tokens[i + 2].is(clang::tok::r_square) && tokens[i + 3].is(clang::tok::r_square)) {
                auto&      manager = compiler_.getSourceManager();
                const auto begin   = manager.getFileOffset(tokens[i].getLocation());
                const auto end =
                    manager.getFileOffset(tokens[i + 3].getLocation()) + tokens[i + 3].getLength();
                for (auto at = begin; at < end; ++at)
                    if (text[at] != '\n') text[at] = ' ';
            }
        return text;
    }
    auto invoke(PreparedInput&             input,
                const clang::AnnotateAttr& attribute,
                bool                       derive,
                llvm::StringRef            argument_override = {}) -> std::optional<std::string> {
        const auto identity = attribute.getAnnotation().drop_front(derive ? derive_annotation.size()
                                                                          : attr_annotation.size());
        const auto split    = identity.find("::");
        if (split == llvm::StringRef::npos || split == 0 || split + 2 == identity.size() ||
            identity.drop_front(split + 2).contains("::")) {
            fail(attribute.getLocation(), "pmacro identity must be 'scope::macro-name'");
            return {};
        }
        auto lookup = state_.find(identity.take_front(split),
                                  identity.drop_front(split + 2),
                                  derive ? host::MacroKind::Derive : host::MacroKind::Attr);
        if (lookup.status != MacroLookupStatus::Found) {
            fail(attribute.getLocation(),
                 "pmacro identity is not provided with the required kind by the aggregate plugin");
            return {};
        }
        auto site = state_.sources().attribute_span(attribute);
        if (! site) {
            fail(attribute.getLocation(), "cannot locate macro attribute");
            return {};
        }
        auto item      = state_.token_stream(state_.make_stream(clean_input(input), input.span));
        auto arguments = argument_override;
        state_.enter_invocation(identity.take_front(split),
                                identity.drop_front(split + 2),
                                lookup.provider,
                                *site,
                                *input.declaration);
        state_.allow_namespace_output(input.namespace_end.isValid());
        const auto source_file =
            compiler_.getSourceManager().getFileID(state_.sources().location(input.span));
        active_trace_ = origins_[source_file.getHashValue()];
        active_trace_.emplace_back(identity.str(), attribute.getLocation());
        if (active_trace_.size() > 64) {
            state_.leave_invocation();
            fail(attribute.getLocation(), "proc-macro expansion recursion limit reached");
            for (const auto& [name, location] : active_trace_)
                report(compiler_.getDiagnostics(), location, clang::DiagnosticsEngine::Note, name);
            return {};
        }
        auto context = state_.context();
        auto result =
            derive
                ? std::get<host::DeriveCallback>(lookup.macro->callback)
                      .function({ item, *site, input.span }, context)
                : std::get<host::AttributeCallback>(lookup.macro->callback)
                      .function({ state_.token_stream(state_.make_stream(arguments, *site, false)),
                                  item,
                                  *site,
                                  input.span },
                                context);
        state_.leave_invocation();
        if (! result) {
            state_.take_outputs();
            fail(attribute.getLocation(), "pmacro provider returned an expansion error");
            return {};
        }
        if (state_.contains_module_declaration(*result)) {
            state_.take_outputs();
            fail(attribute.getLocation(), "pmacro output must not change module dependencies");
            return {};
        }
        auto text = state_.render(*result);
        if (! text)
            fail(attribute.getLocation(), "pmacro provider returned an invalid TokenStream");
        else if (! state_.accept_output(*text, attribute.getLocation()))
            return {};
        return text;
    }
    auto same(const clang::Token& left, const clang::Token& right) -> bool {
        return left.getKind() == right.getKind() &&
               compiler_.getPreprocessor().getSpelling(left) ==
                   compiler_.getPreprocessor().getSpelling(right);
    }
    auto expand_attribute(PreparedInput& input, const clang::AnnotateAttr& attribute) -> void {
        const auto original_end     = input.end;
        const auto original_body    = input.body;
        const auto original_current = input.current;
        const auto target_namespace = input.namespace_end;
        auto       original         = lex(clean_input(input));
        auto       head             = definition_open(original);
        auto       queue            = input.controls;
        std::erase_if(queue, [](const auto& control) {
            return control.derive;
        });
        if (queue.empty()) return;
        std::vector<clang::Token>                           output;
        std::vector<std::pair<OutputLocation, TokenStream>> appended;
        for (std::size_t index = 0; index < queue.size(); ++index) {
            if (index >= 64) {
                fail(queue[index].attribute->getLocation(),
                     "proc-macro expansion recursion limit reached");
                for (const auto& call : queue)
                    report(compiler_.getDiagnostics(),
                           call.attribute->getLocation(),
                           clang::DiagnosticsEngine::Note,
                           call.attribute->getAnnotation().drop_front(attr_annotation.size()));
                return;
            }
            auto text = invoke(input, *queue[index].attribute, false, queue[index].arguments);
            if (! text) return;
            output = lex(*text);
            if (llvm::isa<clang::FunctionDecl>(input.declaration)) {
                const auto before = lex(clean_input(input));
                input.function_input_changed |=
                    before.size() != output.size() ||
                    ! std::equal(before.begin(),
                                 before.end(),
                                 output.begin(),
                                 [&](const auto& left, const auto& right) {
                                     return same(left, right);
                                 });
            }
            auto extra = state_.take_outputs();
            appended.insert(appended.end(), extra.begin(), extra.end());
            if (output.empty()) {
                fail(attribute.getLocation(), "macro output must retain the same declaration");
                return;
            }
            auto span = state_.sources().spelling(clang::CharSourceRange::getTokenRange(
                output.front().getLocation(), output.back().getLocation()));
            if (! span) {
                fail(attribute.getLocation(), "invalid generated declaration source");
                return;
            }
            input.source = *state_.sources().text(*span);
            input.span   = *span;
            input.tokens = output;
            input.body   = definition_open(output);
            input.snapshot.reset();
            input.controls = controls(input);
            for (const auto& control : input.controls) {
                if (control.derive) {
                    input.declaration->addAttr(const_cast<clang::AnnotateAttr*>(control.attribute));
                    derivations_.insert(input.declaration);
                } else
                    queue.push_back(control);
            }
        }
        output = lex(clean_input(input));
        if (original_body && head) {
            unsigned depth = 0;
            for (std::size_t at = *head; at < output.size(); ++at) {
                if (output[at].is(clang::tok::l_brace)) ++depth;
                if (output[at].is(clang::tok::r_brace) && depth) --depth;
                if (! depth && (output[at].is(clang::tok::semi) ||
                                llvm::isa<clang::FunctionDecl>(input.declaration))) {
                    input.end = output[at].getLocation();
                    auto span = state_.sources().spelling(clang::CharSourceRange::getTokenRange(
                        output.front().getLocation(), input.end));
                    if (span) {
                        input.span   = *span;
                        input.source = *state_.sources().text(*span);
                        input.tokens.assign(output.begin(), output.begin() + at + 1);
                        input.body = head;
                    }
                    break;
                }
            }
        }
        std::vector<clang::Token> after;
        for (const auto& [location, stream] : appended) {
            auto text = state_.render(stream);
            if (! text) {
                fail(attribute.getLocation(), "invalid appended TokenStream");
                return;
            }
            if (location == OutputLocation::AfterItem) append(after, lex(*text));
        }
        if (! original_body) {
            bool equal =
                original.size() == output.size() && std::equal(original.begin(),
                                                               original.end(),
                                                               output.begin(),
                                                               [&](const auto& l, const auto& r) {
                                                                   return same(l, r);
                                                               });
            if (equal)
                output.clear();
            else if (! llvm::isa<clang::FunctionDecl>(input.declaration)) {
                fail(attribute.getLocation(),
                     "macro output must retain the declaration before its initializer or "
                     "definition");
                return;
            }
            append(output, std::move(after));
            insert_after(original_end, std::move(output));
        } else {
            if (! head || *head >= output.size() ||
                ! std::equal(original.begin(),
                             original.begin() + *head,
                             output.begin(),
                             [&](const auto& l, const auto& r) {
                                 return same(l, r);
                             }) ||
                ! output[*head].is(clang::tok::l_brace)) {
                fail(attribute.getLocation(), "macro output must retain the same declaration head");
                return;
            }
            auto& pp = compiler_.getPreprocessor();
            if (parser().getCurToken().getLocation() != original_end)
                for (;;) {
                    clang::Token token;
                    pp.Lex(token);
                    if (token.getLocation() == original_end) break;
                    if (token.is(clang::tok::eof)) {
                        fail(attribute.getLocation(),
                             "lost macro input while replacing definition");
                        return;
                    }
                }
            const auto retained = *original_body - original_current;
            if (*head < retained) {
                fail(attribute.getLocation(), "macro output changed the declaration prefix");
                return;
            }
            output.erase(output.begin(), output.begin() + *head - retained + 1);
            append(output, std::move(after));
            enter(std::move(output));
        }
        for (const auto& [location, stream] : appended) {
            if (location != OutputLocation::NamespaceEnd) continue;
            insert_before(target_namespace, lex(*state_.render(stream)));
        }
    }

    clang::CompilerInstance&                                               compiler_;
    ExpansionOptions                                                       options_;
    ExpansionState                                                         state_;
    DefinitionRegistry                                                     definitions_;
    clang::Sema*                                                           sema_ {};
    std::unordered_map<clang::Decl*, PreparedInput>                        prepared_;
    std::unordered_map<unsigned, std::vector<clang::Token>>                files_;
    std::unordered_map<const clang::NamespaceDecl*, clang::SourceLocation> namespace_ends_;
    std::unordered_set<const clang::Decl*>                                 completed_;
    std::unordered_set<const clang::Decl*>                                 derivations_;
    std::unordered_set<clang::Decl*>                                       finished_;
    std::unordered_set<const clang::AnnotateAttr*>                         owned_helpers_;
    using Trace = std::vector<std::pair<std::string, clang::SourceLocation>>;
    std::unordered_map<unsigned, Trace> origins_;
    Trace                               active_trace_;
};

auto prepare_invocation(clang::Sema& sema, clang::Decl& declaration) -> void {
    auto found = compilations.find(&sema);
    if (found != compilations.end()) found->second->prepare(declaration);
}
