export module lito.frontend.preprocessor:session;

import rstd;
import lito.frontend.lexical;
import :builtin;
import :traits;
import :macro;
import :expression;

using namespace rstd::prelude;
using namespace rstd::literals;

using namespace lito::frontend::lexical;

export namespace lito::frontend::preprocessor
{

struct PreprocessRequest {
    rstd::path::PathBuf source;
    String              environment_identity;
    usize               maximum_include_depth { usize(200) };
};

struct PreprocessedTranslationUnit {
    SourceManager                               sources;
    SourceId                                    main_source {};
    Vec<Token>                                  tokens;
    Vec<CommentTrivia>                          active_comments;
    Vec<rstd::path::PathBuf>                    header_inputs;
    Vec<frontend::EmbeddedInput>                embedded_inputs;
    Vec<frontend::ExternalMacroMaterialization> external_macros;
    String                                      environment_identity;
    usize                                       input_bytes {};
    PreprocessorStatistics                      statistics;
};

template<typename Sources,
         typename Includes,
         typename Embeds,
         typename Builtins,
         typename Externals,
         typename Identifiers,
         typename Pragmas,
         typename Events,
         typename Consumer,
         typename Observer>
    requires Impled<Identifiers, TokenMatcher>
class PreprocessorSession {
    struct ConditionalFrame {
        bool           parent_active { false };
        bool           branch_taken { false };
        bool           active { false };
        bool           saw_else { false };
        SourceLocation location;
    };

    struct IncludeFrame {
        SourceId      source {};
        Option<usize> search_index;
    };

    struct DisabledMacro {
        SharedMacroDefinition definition;
        bool                  dynamic_builtin { false };
    };

    struct EmbedDirective {
        String        name;
        EmbedKind     kind { EmbedKind::Quoted };
        usize         offset {};
        Option<usize> limit;
        Vec<Token>    prefix;
        Vec<Token>    suffix;
        Vec<Token>    if_empty;
    };

    struct ResolvedEmbed {
        EmbedResolution resource;
        usize           length {};
    };

    class DisabledMacros {
    public:
        auto contains(ref<str> value) const -> bool {
            for (const auto& macro : values_) {
                if (macro.definition->name.as_str() == value) return true;
            }
            return false;
        }

        auto contains_dynamic(ref<str> value) const -> bool {
            if (dynamic_builtins_ == usize {}) return false;
            for (const auto& macro : values_) {
                if (macro.dynamic_builtin && macro.definition->name.as_str() == value) return true;
            }
            return false;
        }

        auto push(SharedMacroDefinition definition, bool dynamic_builtin) -> void {
            if (dynamic_builtin) ++dynamic_builtins_;
            values_.push(DisabledMacro { .definition      = rstd::move(definition),
                                         .dynamic_builtin = dynamic_builtin });
        }

        auto pop() -> void {
            auto macro = values_.pop().unwrap();
            if (macro.dynamic_builtin) --dynamic_builtins_;
        }

    private:
        Vec<DisabledMacro> values_;
        usize              dynamic_builtins_ {};
    };

    struct RawStatistics {
        size_t files {};
        size_t source_tokens {};
        size_t source_comments {};
        size_t active_comments {};
        size_t token_clones {};
        size_t synthetic_tokens {};
        size_t directives {};
        size_t conditionals {};
        size_t macro_lookups {};
        size_t macro_lookup_hits {};
        size_t macro_expansions {};
        size_t include_attempts {};
        size_t include_hits {};
        size_t consumer_batches {};
        size_t consumer_tokens {};
    };

    class ActivityGuard {
    public:
        ActivityGuard(PreprocessorSession& session, PreprocessorActivity activity) noexcept
            : session_(&session), activity_(activity) {
            session_->begin_activity(activity_);
        }

        ActivityGuard(const ActivityGuard&)                    = delete;
        auto operator=(const ActivityGuard&) -> ActivityGuard& = delete;

        ~ActivityGuard() { session_->end_activity(activity_); }

    private:
        PreprocessorSession* session_;
        PreprocessorActivity activity_;
    };

public:
    PreprocessorSession(PreprocessRequest request,
                        Sources&          sources,
                        Includes&         includes,
                        Embeds&           embeds,
                        Builtins&         builtins,
                        Externals&        externals,
                        Identifiers&      identifiers,
                        Pragmas&          pragmas,
                        Events&           events,
                        Consumer&         consumer,
                        Observer&         observer)
        : request_(rstd::move(request)),
          source_provider_(sources),
          include_resolver_(includes),
          embed_resolver_(embeds),
          builtin_provider_(builtins),
          external_macro_provider_(externals),
          identifier_matcher_(identifiers),
          pragma_handler_(pragmas),
          event_sink_(events),
          consumer_(consumer),
          observer_(observer) {}

    auto run() -> Result<PreprocessedTranslationUnit> {
        {
            auto activity   = ActivityGuard(*this, PreprocessorActivity::PredefinedMacros);
            auto predefined = as<BuiltinProvider>(builtin_provider_).predefined_macros();
            if (predefined.is_err()) return Err(rstd::move(predefined).unwrap_err());
            for (auto& operation : *predefined) {
                if (operation.kind == PredefinedMacroOperationKind::Define) {
                    if (operation.definition.is_none()) {
                        return Err(
                            Error::make("predefined macro define operation has no definition"_str));
                    }
                    (void)macros_.define_shared(rstd::move(operation.definition).unwrap());
                } else {
                    (void)macros_.undefine(operation.name.as_str());
                }
            }
        }

        {
            auto activity  = ActivityGuard(*this, PreprocessorActivity::TranslationUnit);
            auto processed = process_file(request_.source.as_path(), None());
            if (processed.is_err()) {
                auto error = rstd::move(processed).unwrap_err();
                if (error.path.is_none() && error.location.is_some() &&
                    error.location->source < sources_.len()) {
                    error.path =
                        Some(rstd::path::PathBuf::from(sources_.path(error.location->source)));
                }
                return Err(rstd::move(error));
            }
        }
        auto statistics = finish_statistics();
        as<PreprocessorObserver>(observer_).record(statistics);
        return Ok(PreprocessedTranslationUnit {
            .sources              = rstd::move(sources_),
            .main_source          = main_source_,
            .tokens               = Vec<Token>::make(),
            .active_comments      = rstd::move(active_comments_),
            .header_inputs        = Vec<rstd::path::PathBuf>::make(),
            .embedded_inputs      = rstd::move(embedded_inputs_),
            .external_macros      = rstd::move(external_macros_),
            .environment_identity = environment_identity(),
            .input_bytes          = input_bytes_,
            .statistics           = statistics,
        });
    }

private:
    auto begin_activity(PreprocessorActivity activity) -> void {
        as<PreprocessorObserver>(observer_).begin(activity);
    }

    auto end_activity(PreprocessorActivity activity) -> void {
        as<PreprocessorObserver>(observer_).end(activity);
    }

    auto finish_statistics() const noexcept -> PreprocessorStatistics {
        return PreprocessorStatistics {
            .files             = usize(raw_statistics_.files),
            .source_tokens     = usize(raw_statistics_.source_tokens),
            .source_comments   = usize(raw_statistics_.source_comments),
            .active_comments   = usize(raw_statistics_.active_comments),
            .token_clones      = usize(raw_statistics_.token_clones),
            .synthetic_tokens  = usize(raw_statistics_.synthetic_tokens),
            .directives        = usize(raw_statistics_.directives),
            .conditionals      = usize(raw_statistics_.conditionals),
            .macro_lookups     = usize(raw_statistics_.macro_lookups),
            .macro_lookup_hits = usize(raw_statistics_.macro_lookup_hits),
            .macro_expansions  = usize(raw_statistics_.macro_expansions),
            .include_attempts  = usize(raw_statistics_.include_attempts),
            .include_hits      = usize(raw_statistics_.include_hits),
            .consumer_batches  = usize(raw_statistics_.consumer_batches),
            .consumer_tokens   = usize(raw_statistics_.consumer_tokens),
        };
    }

    auto failure(ref<str> message, SourceLocation location) -> Error {
        auto error = Error::at(String::make(message), location);
        if (location.source < sources_.len()) {
            error.path = Some(rstd::path::PathBuf::from(sources_.path(location.source)));
        }
        return error;
    }

    auto failure(String message, SourceLocation location) -> Error {
        auto error = Error::at(rstd::move(message), location);
        if (location.source < sources_.len()) {
            error.path = Some(rstd::path::PathBuf::from(sources_.path(location.source)));
        }
        return error;
    }

    auto path_text(ref<rstd::path::Path> path, SourceLocation location) -> Result<String> {
        auto text = path.to_str();
        if (text.is_none()) {
            return Err(
                failure(rstd::format("source path '{}' is not valid UTF-8", path), location));
        }
        return Ok(String::make(*text));
    }

    auto emit(Event event) -> Result<empty> {
        if (! as<PreprocessorEventSink>(event_sink_).wants(event.kind)) return Ok(empty {});
        return as<PreprocessorEventSink>(event_sink_).on_event(event);
    }

    auto emit_name(EventKind kind, ref<str> name, SourceLocation location) -> Result<empty> {
        if (! as<PreprocessorEventSink>(event_sink_).wants(kind)) return Ok(empty {});
        auto event = Event { .kind = kind, .name = String::make(name), .location = location };
        return as<PreprocessorEventSink>(event_sink_).on_event(event);
    }

    auto active(const Vec<ConditionalFrame>& conditions) const -> bool {
        return conditions.is_empty() || conditions[conditions.len() - usize(1)].active;
    }

    auto clone_token(const Token& token) -> Token {
        ++raw_statistics_.token_clones;
        return token.clone();
    }

    auto clone_tokens_counted(const Vec<Token>& tokens) -> Vec<Token> {
        auto result = Vec<Token>::with_capacity(tokens.len());
        for (const auto& token : tokens) result.push(clone_token(token));
        return result;
    }

    auto without_newline(Vec<Token>& tokens, usize begin, usize end) -> Result<Vec<Token>> {
        auto result = Vec<Token>::with_capacity(end - begin);
        for (auto index = begin; index < end; ++index) {
            if (tokens[index].kind != TokenKind::Newline) result.push(rstd::move(tokens[index]));
        }
        return Ok(rstd::move(result));
    }

    auto number_token(i64 value, const Token& origin) -> Token {
        auto token = clone_token(origin);
        ++raw_statistics_.synthetic_tokens;
        token.kind          = TokenKind::PpNumber;
        token.text          = rstd::format("{}", value);
        token.start_of_line = origin.start_of_line;
        return token;
    }

    auto string_token(ref<str> value, const Token& origin) -> Token {
        auto text = String::make();
        text.push_ascii('"');
        auto begin = usize {};
        for (auto index = usize {}; index < value.len(); ++index) {
            auto byte = value[index];
            if (byte == u8('\\') || byte == u8('"')) {
                text.push_str(value.get(begin, index).unwrap());
                text.push_ascii('\\');
                text.push_ascii(byte);
                begin = index + usize(1);
            }
        }
        text.push_str(value.get(begin, value.len()).unwrap());
        text.push_ascii('"');
        auto token = clone_token(origin);
        ++raw_statistics_.synthetic_tokens;
        token.kind = TokenKind::StringLiteral;
        token.text = rstd::move(text);
        return token;
    }

    auto environment_identity() const -> String {
        if (builtin_identity_.is_empty()) return request_.environment_identity.clone();
        return rstd::format(
            "{}:{}", request_.environment_identity.as_str(), builtin_identity_.as_str());
    }

    auto prepare_external_macro(ref<str> name, SourceLocation location) -> Result<empty> {
        if (resolved_external_macros_.contains_key(name)) return Ok(empty {});
        auto resolved = as<ExternalMacroProvider>(external_macro_provider_).resolve(name, location);
        if (resolved.is_err()) return Err(rstd::move(resolved).unwrap_err());
        if (resolved->is_none()) return Ok(empty {});
        auto value = rstd::move(resolved).unwrap().unwrap();
        if (value.dependency_key.is_empty() || value.value_identity.is_empty()) {
            return Err(failure("external macro resolution has an empty identity"_str, location));
        }
        if (value.state == frontend::ExternalMacroState::Defined) {
            if (value.definition.is_none() || value.compiler_definition.is_none() ||
                (**value.definition).name.as_str() != name) {
                return Err(
                    failure("defined external macro has an invalid definition"_str, location));
            }
            (void)macros_.define_shared(value.definition->clone());
        } else if (value.definition.is_some() || value.compiler_definition.is_some()) {
            return Err(failure("undefined external macro carries a definition"_str, location));
        }
        resolved_external_macros_.insert(String::make(name), empty {});
        external_macros_.push(frontend::ExternalMacroMaterialization {
            .name                = String::make(name),
            .dependency_key      = rstd::move(value.dependency_key),
            .value_identity      = rstd::move(value.value_identity),
            .state               = value.state,
            .compiler_definition = rstd::move(value.compiler_definition),
        });
        return Ok(empty {});
    }

    auto lookup_macro(ref<str> name, SourceLocation location)
        -> Result<Option<SharedMacroDefinition>> {
        auto found = macros_.get(name);
        if (found.is_some() || resolved_external_macros_.contains_key(name)) {
            return Ok(rstd::move(found));
        }
        auto prepared = prepare_external_macro(name, location);
        if (prepared.is_err()) return Err(rstd::move(prepared).unwrap_err());
        return Ok(macros_.get(name));
    }

    auto contains_macro(ref<str> name, SourceLocation location) -> Result<bool> {
        auto found = lookup_macro(name, location);
        if (found.is_err()) return Err(rstd::move(found).unwrap_err());
        return Ok(found->is_some());
    }

    auto clone_range(const Vec<Token>& input, usize begin, usize end) -> Vec<Token> {
        auto result = Vec<Token>::with_capacity(end - begin);
        for (auto index = begin; index < end; ++index) result.push(clone_token(input[index]));
        return result;
    }

    auto move_range(Vec<Token>& input, usize begin, usize end) -> Vec<Token> {
        auto result = Vec<Token>::with_capacity(end - begin);
        for (auto index = begin; index < end; ++index) result.push(rstd::move(input[index]));
        return result;
    }

    auto stringify(const Vec<Token>& argument, const Token& origin) -> Token {
        auto text = String::make();
        text.push_ascii('"');
        auto first = true;
        for (const auto& token : argument) {
            if (token.kind == TokenKind::Newline) continue;
            if (! first && token.leading_space) text.push_ascii(' ');
            auto value = token.text.as_str();
            auto begin = usize {};
            for (auto index = usize {}; index < value.len(); ++index) {
                auto byte = value[index];
                if (byte == u8('\\') || byte == u8('"')) {
                    text.push_str(value.get(begin, index).unwrap());
                    text.push_ascii('\\');
                    text.push_ascii(byte);
                    begin = index + usize(1);
                }
            }
            text.push_str(value.get(begin, value.len()).unwrap());
            first = false;
        }
        text.push_ascii('"');
        auto result = clone_token(origin);
        ++raw_statistics_.synthetic_tokens;
        result.kind = TokenKind::StringLiteral;
        result.text = rstd::move(text);
        return result;
    }

    auto string_contents(const Token& token, ref<str> purpose) -> Result<String> {
        if (token.kind != TokenKind::StringLiteral || token.text.len() < usize(2) ||
            token.text.as_str().as_bytes()[usize {}] != u8('"') ||
            token.text.as_str().as_bytes()[token.text.len() - usize(1)] != u8('"')) {
            return Err(failure(rstd::format("{} requires an ordinary string literal", purpose),
                               token.expansion));
        }
        auto contents = token.text.as_str().get(usize(1), token.text.len() - usize(1));
        if (contents.is_none()) {
            return Err(failure(rstd::format("{} has an invalid string literal", purpose),
                               token.expansion));
        }
        return Ok(String::make(*contents));
    }

    auto pragma_macro_name(const Vec<Token>& tokens, SourceLocation location) -> Result<String> {
        if (tokens.len() != usize(4) || tokens[usize(1)].text.as_str() != "("_str ||
            tokens[usize(3)].text.as_str() != ")"_str) {
            return Err(failure("macro stack pragma requires one string literal"_str, location));
        }
        auto name = string_contents(tokens[usize(2)], "macro stack pragma"_str);
        if (name.is_err()) return name;
        auto source =
            SourceFile::make(SourceId {},
                             SourceBuffer {
                                 .path     = rstd::path::PathBuf::from("<pragma-macro-name>"_str),
                                 .contents = name->clone(),
                             });
        auto lexed = lex(source);
        if (lexed.is_err() || lexed->len() != usize(1) ||
            (*lexed)[usize {}].kind != TokenKind::Identifier ||
            (*lexed)[usize {}].text.as_str() != name->as_str()) {
            return Err(failure("macro stack pragma requires an identifier name"_str, location));
        }
        return name;
    }

    auto handle_pragma(Vec<Token> tokens, SourceLocation location) -> Result<empty> {
        if (tokens.len() == usize(1) && tokens[usize {}].text.as_str() == "once"_str) {
            if (include_stack_.is_empty()) {
                return Err(failure("pragma once has no current source"_str, location));
            }
            auto source = include_stack_[include_stack_.len() - usize(1)].source;
            auto text   = sources_.path(source).to_str();
            if (text.is_some()) once_files_.insert(String::make(*text), empty {});
            return Ok(empty {});
        }
        if (! tokens.is_empty() && (tokens[usize {}].text.as_str() == "push_macro"_str ||
                                    tokens[usize {}].text.as_str() == "pop_macro"_str)) {
            auto push = tokens[usize {}].text.as_str() == "push_macro"_str;
            auto name = pragma_macro_name(tokens, location);
            if (name.is_err()) return Err(rstd::move(name).unwrap_err());
            if (push) {
                auto stored = lookup_macro(name->as_str(), location);
                if (stored.is_err()) return Err(rstd::move(stored).unwrap_err());
                auto stack = macro_stacks_.get_mut(name->as_str());
                if (stack.is_none()) {
                    auto values = Vec<Option<SharedMacroDefinition>>::make();
                    values.push(rstd::move(stored).unwrap());
                    macro_stacks_.insert(rstd::move(name).unwrap(), rstd::move(values));
                } else {
                    (**stack).push(rstd::move(stored).unwrap());
                }
                return Ok(empty {});
            }
            auto stack = macro_stacks_.get_mut(name->as_str());
            if (stack.is_none() || (**stack).is_empty()) {
                return Err(failure(
                    rstd::format("no pushed definition for macro '{}'", name->as_str()), location));
            }
            auto restored = (**stack).pop().unwrap();
            if (restored.is_some()) {
                (void)macros_.define_shared(rstd::move(restored).unwrap());
            } else {
                (void)macros_.undefine(name->as_str());
            }
            return Ok(empty {});
        }
        auto outcome =
            as<PragmaHandler>(pragma_handler_)
                .handle(PragmaRequest { .tokens = rstd::move(tokens), .location = location });
        if (outcome.is_err()) return Err(rstd::move(outcome).unwrap_err());
        return Ok(empty {});
    }

    auto destringize_pragma(const Token& token) -> Result<Vec<Token>> {
        auto encoded = string_contents(token, "_Pragma"_str);
        if (encoded.is_err()) return Err(rstd::move(encoded).unwrap_err());
        auto contents = Vec<u8>::make();
        auto bytes    = encoded->as_str().as_bytes();
        for (auto index = usize {}; index < bytes.len(); ++index) {
            if (bytes[index] == u8('\\') && index + usize(1) < bytes.len() &&
                (bytes[index + usize(1)] == u8('\\') || bytes[index + usize(1)] == u8('"'))) {
                ++index;
            }
            contents.push(bytes[index]);
        }
        auto decoded = String::from_utf8(rstd::move(contents));
        if (decoded.is_err())
            return Err(failure("_Pragma contents are not valid UTF-8"_str, token.spelling));
        auto source = SourceFile::make(
            token.spelling.source,
            SourceBuffer {
                .path     = rstd::path::PathBuf::from(sources_.path(token.spelling.source)),
                .contents = rstd::move(decoded).unwrap(),
            });
        auto lexed = lex(source);
        if (lexed.is_err()) return Err(rstd::move(lexed).unwrap_err());
        auto result = Vec<Token>::make();
        for (auto& item : *lexed) {
            if (item.kind == TokenKind::Newline) continue;
            item.spelling  = token.spelling;
            item.expansion = token.expansion;
            result.push(rstd::move(item));
        }
        return Ok(rstd::move(result));
    }

    auto pasted(Token left, const Token& right) -> Result<Token> {
        ++raw_statistics_.synthetic_tokens;
        left.text.push_str(right.text.as_str());
        if (left.text.is_empty()) {
            return Err(failure("token paste produced an empty token"_str, left.expansion));
        }
        auto source = SourceFile::make(
            left.spelling.source,
            SourceBuffer {
                .path     = rstd::path::PathBuf::from(sources_.path(left.spelling.source)),
                .contents = left.text.clone(),
            });
        auto lexed = lex(source);
        if (lexed.is_err() || lexed->len() != usize(1) ||
            (*lexed)[usize {}].text.as_str() != left.text.as_str()) {
            return Err(failure(rstd::format("token paste '{}' did not form one preprocessing token",
                                            left.text.as_str()),
                               left.expansion));
        }
        left.kind = (*lexed)[usize {}].kind;
        return Ok(rstd::move(left));
    }

    struct ArgumentRange {
        usize begin;
        usize end;
    };

    struct ParsedArguments {
        Vec<ArgumentRange> ranges;
        usize              next;
    };

    auto parse_arguments(const Vec<Token>& input, usize open) -> Result<ParsedArguments> {
        auto ranges = Vec<ArgumentRange>::make();
        auto begin  = open + usize(1);
        auto depth  = usize {};
        for (auto index = open + usize(1); index < input.len(); ++index) {
            const auto& token = input[index];
            if (token.text.as_str() == "("_str) {
                ++depth;
                continue;
            }
            if (token.text.as_str() == ")"_str) {
                if (depth == usize {}) {
                    ranges.push(ArgumentRange { .begin = begin, .end = index });
                    return Ok(
                        ParsedArguments { .ranges = rstd::move(ranges), .next = index + usize(1) });
                }
                --depth;
                continue;
            }
            if (token.text.as_str() == ","_str && depth == usize {}) {
                ranges.push(ArgumentRange { .begin = begin, .end = index });
                begin = index + usize(1);
                continue;
            }
        }
        return Err(failure("unterminated macro invocation"_str, input[open].expansion));
    }

    auto materialize_arguments(Vec<Token>& input, const Vec<ArgumentRange>& ranges)
        -> Vec<Vec<Token>> {
        auto arguments = Vec<Vec<Token>>::with_capacity(ranges.len());
        for (const auto& range : ranges) {
            auto argument = Vec<Token>::with_capacity(range.end - range.begin);
            for (auto index = range.begin; index < range.end; ++index) {
                if (input[index].kind != TokenKind::Newline)
                    argument.push(rstd::move(input[index]));
            }
            arguments.push(rstd::move(argument));
        }
        return arguments;
    }

    auto variadic_argument(const MacroDefinition& macro,
                           Vec<Vec<Token>>&       arguments,
                           const Token&           origin,
                           bool                   consume_arguments) -> Vec<Token> {
        auto result = Vec<Token>::make();
        auto fixed  = macro.parameters.is_some() ? macro.parameters->len() : usize {};
        for (auto index = fixed; index < arguments.len(); ++index) {
            if (! result.is_empty()) {
                auto comma = clone_token(origin);
                ++raw_statistics_.synthetic_tokens;
                comma.kind = TokenKind::Punctuation;
                comma.text = String::make(","_str);
                result.push(rstd::move(comma));
            }
            for (auto& token : arguments[index]) {
                result.push(consume_arguments ? rstd::move(token) : clone_token(token));
            }
        }
        return result;
    }

    auto substitute(const MacroDefinition& macro,
                    Vec<Vec<Token>>&       arguments,
                    const Token&           origin,
                    DisabledMacros&        disabled,
                    bool                   consume_arguments = true) -> Result<Vec<Token>> {
        auto fixed          = macro.parameters.is_some() ? macro.parameters->len() : usize {};
        auto argument_count = arguments.len();
        if (! macro.variadic && fixed == usize {} && argument_count == usize(1) &&
            arguments[usize {}].is_empty()) {
            argument_count = usize {};
        }
        if ((! macro.variadic && argument_count != fixed) ||
            (macro.variadic && arguments.len() < fixed)) {
            return Err(failure(rstd::format("macro '{}' expects {} argument(s), got {}",
                                            macro.name.as_str(),
                                            fixed,
                                            argument_count),
                               origin.expansion));
        }
        auto variadic = variadic_argument(
            macro, arguments, origin, consume_arguments && macro.can_consume_argument(fixed));
        auto expanded_arguments = Vec<Option<Vec<Token>>>::with_capacity(fixed + usize(1));
        for (auto index = usize {}; index <= fixed; ++index)
            expanded_arguments.emplace_back(None());
        auto expanded_argument =
            [&](usize parameter, Vec<Token>& argument, bool last_use) -> Result<Vec<Token>> {
            if (expanded_arguments[parameter].is_none()) {
                auto input    = consume_arguments && macro.can_consume_argument(parameter)
                                    ? rstd::move(argument)
                                    : clone_tokens_counted(argument);
                auto expanded = expand(rstd::move(input), disabled);
                if (expanded.is_err()) return Err(rstd::move(expanded).unwrap_err());
                expanded_arguments[parameter] = Some(rstd::move(expanded).unwrap());
            }
            if (last_use) return Ok(rstd::move(expanded_arguments[parameter]).unwrap_unchecked());
            return Ok(clone_tokens_counted(*expanded_arguments[parameter]));
        };
        auto result = Vec<Token>::make();
        for (auto index = usize {}; index < macro.replacement.len(); ++index) {
            const auto& token = macro.replacement[index];
            if (token.text.as_str() == "__VA_OPT__"_str && macro.variadic &&
                index + usize(1) < macro.replacement.len() &&
                macro.replacement[index + usize(1)].text.as_str() == "("_str) {
                auto end = macro.va_opt_end(index);
                if (end.is_none())
                    return Err(failure("unterminated __VA_OPT__"_str, token.expansion));
                if (! variadic.is_empty()) {
                    auto nested_macro = macro.with_replacement(
                        clone_range(macro.replacement, index + usize(2), *end - usize(1)));
                    auto substituted = substitute(nested_macro, arguments, origin, disabled, false);
                    if (substituted.is_err()) return substituted;
                    for (auto& item : *substituted) result.push(rstd::move(item));
                }
                index = *end - usize(1);
                continue;
            }
            if (token.text.as_str() == "#"_str && index + usize(1) < macro.replacement.len()) {
                auto parameter =
                    macro.parameter_index(macro.replacement[index + usize(1)].text.as_str());
                if (parameter.is_some()) {
                    auto* argument = &variadic;
                    if (*parameter < fixed) argument = &arguments[*parameter];
                    result.push(stringify(*argument, origin));
                    ++index;
                    continue;
                }
            }

            auto paste_left =
                index > usize {} && macro.replacement[index - usize(1)].text.as_str() == "##"_str;
            auto parameter = macro.parameter_index(token.text.as_str());
            auto piece     = Vec<Token>::make();
            if (parameter.is_some()) {
                auto* argument = &variadic;
                if (*parameter < fixed) argument = &arguments[*parameter];
                auto paste_right = index + usize(1) < macro.replacement.len() &&
                                   macro.replacement[index + usize(1)].text.as_str() == "##"_str;
                if (paste_left || paste_right) {
                    piece = clone_tokens_counted(*argument);
                } else {
                    auto expanded = expanded_argument(
                        *parameter, *argument, macro.is_last_expanded_use(*parameter, index));
                    if (expanded.is_err()) return expanded;
                    piece = rstd::move(expanded).unwrap();
                }
            } else {
                piece.push(clone_token(token));
            }

            if (paste_left) {
                if (piece.is_empty()) {
                    if (! result.is_empty() && result[result.len() - usize(1)].text.is_empty()) {
                        (void)result.pop();
                    } else if (! result.is_empty() &&
                               result[result.len() - usize(1)].text.as_str() == ","_str &&
                               parameter.is_some() && *parameter == fixed) {
                        (void)result.pop();
                    }
                } else if (! result.is_empty()) {
                    auto left     = result.pop().unwrap();
                    auto combined = pasted(rstd::move(left), piece[usize {}]);
                    if (combined.is_err()) return Err(rstd::move(combined).unwrap_err());
                    result.push(rstd::move(combined).unwrap());
                    for (auto part = usize(1); part < piece.len(); ++part) {
                        result.push(clone_token(piece[part]));
                    }
                } else {
                    for (auto& item : piece) result.push(rstd::move(item));
                }
            } else if (piece.is_empty() && parameter.is_some() &&
                       index + usize(1) < macro.replacement.len() &&
                       macro.replacement[index + usize(1)].text.as_str() == "##"_str) {
                auto placemarker = clone_token(origin);
                ++raw_statistics_.synthetic_tokens;
                placemarker.kind = TokenKind::Punctuation;
                placemarker.text = String::make();
                result.push(rstd::move(placemarker));
            } else if (token.text.as_str() != "##"_str) {
                for (auto& item : piece) result.push(rstd::move(item));
            }
        }
        for (auto& token : result) token.expansion = origin.expansion;
        return Ok(rstd::move(result));
    }

    auto joined_argument(const Vec<Token>& tokens) -> String {
        auto result = String::make();
        for (const auto& token : tokens) {
            if (token.kind != TokenKind::Newline) result.push_str(token.text.as_str());
        }
        return result;
    }

    template<typename Query>
    auto evaluate_builtin_query(const Vec<Token>& argument, const Token& origin) -> Result<i64> {
        using Handler = typename Query::Handler;

        auto value = String::make();
        if constexpr (Handler::form == BuiltinQueryArgumentForm::StringLiteral) {
            if (argument.len() != usize(1)) {
                return Err(
                    failure(rstd::format("builtin '{}' requires one string literal", Query::name),
                            origin.expansion));
            }
            auto purpose  = rstd::format("builtin '{}'", Query::name);
            auto contents = string_contents(argument[usize {}], purpose.as_str());
            if (contents.is_err()) return Err(rstd::move(contents).unwrap_err());
            value = rstd::move(contents).unwrap();
        } else {
            value = joined_argument(argument);
        }
        return as<BuiltinProvider>(builtin_provider_)
            .evaluate(BuiltinQueryKey::template make<Query>(value.as_str()));
    }

    auto include_query(const Token& origin, bool include_next, const Vec<Token>& argument)
        -> Result<bool> {
        if (include_stack_.is_empty()) {
            return Err(failure("include query has no current source"_str, origin.expansion));
        }
        auto kind   = include_next ? IncludeKind::NextQuoted : IncludeKind::Quoted;
        auto header = String::make();
        if (argument.len() == usize(1) && argument[usize {}].kind == TokenKind::StringLiteral) {
            auto text  = argument[usize {}].text.as_str();
            auto bytes = text.as_bytes();
            if (bytes.len() < usize(2))
                return Err(failure("invalid include query"_str, origin.expansion));
            auto inside = text.get(usize(1), text.len() - usize(1));
            if (inside.is_none())
                return Err(failure("invalid include query"_str, origin.expansion));
            header = String::make(*inside);
        } else if (argument.len() >= usize(2) && argument[usize {}].text.as_str() == "<"_str &&
                   argument[argument.len() - usize(1)].text.as_str() == ">"_str) {
            kind = include_next ? IncludeKind::NextAngled : IncludeKind::Angled;
            for (auto index = usize(1); index + usize(1) < argument.len(); ++index) {
                header.push_str(argument[index].text.as_str());
            }
        } else {
            return Err(
                failure("include query requires a quoted or angled header"_str, origin.expansion));
        }
        const auto& frame   = include_stack_[include_stack_.len() - usize(1)];
        auto        request = IncludeRequest {
            .name                  = rstd::move(header),
            .kind                  = kind,
            .including_path        = rstd::path::PathBuf::from(sources_.path(frame.source)),
            .previous_search_index = frame.search_index,
            .location              = origin.expansion,
        };
        auto resolved = as<IncludeResolver>(include_resolver_).resolve(request);
        ++raw_statistics_.include_attempts;
        if (resolved.is_err()) return Err(rstd::move(resolved).unwrap_err());
        if (resolved->is_some()) ++raw_statistics_.include_hits;
        auto event = resolved->is_some() ? emit(Event {
                                               .kind     = EventKind::IncludeProbeResolved,
                                               .name     = request.name.clone(),
                                               .path     = Some((*resolved)->path.clone()),
                                               .location = origin.expansion,
                                           })
                                         : emit_name(EventKind::IncludeProbeNotFound,
                                                     request.name.as_str(),
                                                     origin.expansion);
        if (event.is_err()) return Err(rstd::move(event).unwrap_err());
        return Ok(resolved->is_some());
    }

    auto expand(Vec<Token> input, DisabledMacros& disabled) -> Result<Vec<Token>> {
        auto output = Vec<Token>::make();
        for (auto index = usize {}; index < input.len();) {
            auto token = rstd::move(input[index]);
            if (token.kind != TokenKind::Identifier || token.disable_expand) {
                output.push(rstd::move(token));
                ++index;
                continue;
            }
            auto revision = macros_.revision();
            if (token.is_known_unavailable_macro(revision)) {
                ++raw_statistics_.macro_lookup_hits;
                output.push(rstd::move(token));
                ++index;
                continue;
            }
            auto name       = token.text.as_str();
            auto name_hash  = token.text.comparable_hash();
            auto name_bytes = name.as_bytes();
            if (! name_bytes.is_empty() && name_bytes[usize {}] == u8('_')) {
                if (disabled.contains_dynamic(name)) {
                    token.disable_expand = true;
                    output.push(rstd::move(token));
                    ++index;
                    continue;
                }
                if (token.text.matches<LineBuiltin>()) {
                    output.push(number_token(as_cast<i64>(token.expansion.line), token));
                    ++index;
                    continue;
                }
                if (token.text.matches<IncludeLevelBuiltin>()) {
                    auto level =
                        include_stack_.is_empty() ? usize {} : include_stack_.len() - usize(1);
                    output.push(number_token(as_cast<i64>(level), token));
                    ++index;
                    continue;
                }
                if (token.text.matches<CounterBuiltin>()) {
                    output.push(number_token(as_cast<i64>(counter_), token));
                    ++counter_;
                    ++index;
                    continue;
                }
                auto file      = token.text.matches<FileBuiltin>();
                auto file_name = token.text.matches<FileNameBuiltin>();
                auto base_file = token.text.matches<BaseFileBuiltin>();
                if (file || file_name || base_file) {
                    auto source =
                        base_file ? include_stack_[usize {}].source : token.expansion.source;
                    auto path = ! base_file && token.presumed_path.is_some()
                                    ? token.presumed_path->as_path()
                                    : sources_.path(source);
                    if (file_name && path.file_name().is_some()) {
                        auto text = (*path.file_name()).to_str();
                        if (text.is_some()) {
                            output.push(string_token(*text, token));
                            ++index;
                            continue;
                        }
                    }
                    auto text = path.to_str();
                    if (text.is_none())
                        return Err(failure("source path is not valid UTF-8"_str, token.expansion));
                    output.push(string_token(*text, token));
                    ++index;
                    continue;
                }
                auto date = token.text.matches<DateBuiltin>();
                auto time = token.text.matches<TimeBuiltin>();
                if (date || time) {
                    auto kind  = date ? BuiltinTextKind::Date : BuiltinTextKind::Time;
                    auto value = as<BuiltinProvider>(builtin_provider_).text(kind);
                    if (value.is_err()) return Err(rstd::move(value).unwrap_err());
                    builtin_identity_.push_str(name);
                    builtin_identity_.push_ascii('=');
                    builtin_identity_.push_str(value->as_str());
                    builtin_identity_.push_ascii(';');
                    output.push(string_token(value->as_str(), token));
                    ++index;
                    continue;
                }
                if (token.text.matches<PragmaBuiltin>()) {
                    auto open = index + usize(1);
                    while (open < input.len() && input[open].kind == TokenKind::Newline) ++open;
                    if (open >= input.len() || input[open].text.as_str() != "("_str) {
                        return Err(failure("_Pragma requires parentheses"_str, token.expansion));
                    }
                    auto parsed = parse_arguments(input, open);
                    if (parsed.is_err()) return Err(rstd::move(parsed).unwrap_err());
                    auto arguments = materialize_arguments(input, parsed->ranges);
                    if (arguments.len() != usize(1)) {
                        return Err(
                            failure("_Pragma requires one string literal"_str, token.expansion));
                    }
                    auto expanded_argument = expand(rstd::move(arguments[usize {}]), disabled);
                    if (expanded_argument.is_err()) {
                        return Err(rstd::move(expanded_argument).unwrap_err());
                    }
                    if (expanded_argument->len() != usize(1)) {
                        return Err(
                            failure("_Pragma requires one string literal"_str, token.expansion));
                    }
                    auto pragma = destringize_pragma((*expanded_argument)[usize {}]);
                    if (pragma.is_err()) return Err(rstd::move(pragma).unwrap_err());
                    auto handled = handle_pragma(rstd::move(pragma).unwrap(), token.expansion);
                    if (handled.is_err()) return Err(rstd::move(handled).unwrap_err());
                    index = parsed->next;
                    continue;
                }
                auto query_builtin      = BuiltinQuerySet::contains(name_hash, name);
                auto include_next       = token.text.matches<HasIncludeNextBuiltin>();
                auto include_builtin    = token.text.matches<HasIncludeBuiltin>() || include_next;
                auto embed_builtin      = token.text.matches<HasEmbedBuiltin>();
                auto identifier_builtin = token.text.matches<IsIdentifierBuiltin>();
                auto building_module    = token.text.matches<BuildingModuleBuiltin>();
                if (query_builtin || include_builtin || embed_builtin || identifier_builtin ||
                    building_module) {
                    auto open = index + usize(1);
                    while (open < input.len() && input[open].kind == TokenKind::Newline) ++open;
                    if (open >= input.len() || input[open].text.as_str() != "("_str) {
                        return Err(failure(rstd::format("builtin '{}' requires parentheses", name),
                                           token.expansion));
                    }
                    auto parsed = parse_arguments(input, open);
                    if (parsed.is_err()) return Err(rstd::move(parsed).unwrap_err());
                    auto arguments = materialize_arguments(input, parsed->ranges);
                    if (arguments.len() != usize(1)) {
                        return Err(failure(rstd::format("builtin '{}' requires one argument", name),
                                           token.expansion));
                    }
                    auto value = Result<i64>(Ok(i64 {}));
                    if (include_builtin) {
                        auto included = include_query(token, include_next, arguments[usize {}]);
                        if (included.is_err()) return Err(rstd::move(included).unwrap_err());
                        value = Ok(i64(*included));
                    } else if (embed_builtin) {
                        auto directive =
                            parse_embed(rstd::move(arguments[usize {}]), token.expansion);
                        if (directive.is_err()) return Err(rstd::move(directive).unwrap_err());
                        auto embedded = resolve_embed(*directive, token.expansion, true);
                        if (embedded.is_err()) return Err(rstd::move(embedded).unwrap_err());
                        if (embedded->is_none()) {
                            auto event = emit_name(EventKind::EmbedProbeNotFound,
                                                   directive->name.as_str(),
                                                   token.expansion);
                            if (event.is_err()) return Err(rstd::move(event).unwrap_err());
                            value = Ok(i64 {});
                        } else {
                            auto resolved = rstd::move(embedded).unwrap().unwrap();
                            record_embed(*directive, resolved);
                            auto event = emit(Event {
                                .kind     = EventKind::EmbedProbeResolved,
                                .name     = directive->name.clone(),
                                .path     = Some(resolved.resource.path.clone()),
                                .location = token.expansion,
                            });
                            if (event.is_err()) return Err(rstd::move(event).unwrap_err());
                            value = Ok(resolved.length == usize {} ? i64(2) : i64(1));
                        }
                    } else if (identifier_builtin || building_module) {
                        if (arguments[usize {}].len() != usize(1) ||
                            arguments[usize {}][usize {}].kind != TokenKind::Identifier) {
                            return Err(failure(
                                rstd::format("builtin '{}' requires one identifier token", name),
                                token.expansion));
                        }
                        value = building_module
                                    ? Ok(i64 {})
                                    : Ok(i64(matches_token(identifier_matcher_,
                                                           arguments[usize {}][usize {}])));
                    } else {
                        auto matched = BuiltinQuerySet::visit(name_hash, name, [&](auto query) {
                            using Query = typename decltype(query)::type;
                            value       = evaluate_builtin_query<Query>(arguments[usize {}], token);
                        });
                        if (! matched) rstd::unreachable();
                    }
                    if (value.is_err()) return Err(rstd::move(value).unwrap_err());
                    output.push(number_token(*value, token));
                    index = parsed->next;
                    continue;
                }
            }

            ++raw_statistics_.macro_lookups;
            auto resolved = lookup_macro(name, token.expansion);
            if (resolved.is_err()) return Err(rstd::move(resolved).unwrap_err());
            auto found = rstd::move(resolved).unwrap();
            if (found.is_none()) {
                token.mark_unavailable_macro(revision);
                output.push(rstd::move(token));
                ++index;
                continue;
            }
            if (disabled.contains(name)) {
                token.disable_expand = true;
                output.push(rstd::move(token));
                ++index;
                continue;
            }
            auto next = index + usize(1);
            if ((**found).parameters.is_some()) {
                auto open = next;
                while (open < input.len() && input[open].kind == TokenKind::Newline) ++open;
                if (open >= input.len() || input[open].text.as_str() != "("_str) {
                    output.push(rstd::move(token));
                    ++index;
                    continue;
                }
            }
            auto        macro      = rstd::move(found).unwrap();
            const auto& definition = *macro;
            ++raw_statistics_.macro_expansions;
            auto replacement = Vec<Token>::make();
            if (definition.parameters.is_some()) {
                auto open = next;
                while (open < input.len() && input[open].kind == TokenKind::Newline) ++open;
                auto parsed = parse_arguments(input, open);
                if (parsed.is_err()) return Err(rstd::move(parsed).unwrap_err());
                auto arguments   = materialize_arguments(input, parsed->ranges);
                auto substituted = substitute(definition, arguments, token, disabled);
                if (substituted.is_err()) return substituted;
                replacement = rstd::move(substituted).unwrap();
                next        = parsed->next;
            } else {
                replacement = clone_tokens_counted(definition.replacement);
                for (auto& item : replacement) item.expansion = token.expansion;
            }
            auto event =
                emit_name(EventKind::MacroExpanded, definition.name.as_str(), token.expansion);
            if (event.is_err()) return Err(rstd::move(event).unwrap_err());
            auto dynamic_builtin = definition.is_dynamic_builtin();
            disabled.push(rstd::move(macro), dynamic_builtin);
            auto rescanned = expand(rstd::move(replacement), disabled);
            disabled.pop();
            if (rescanned.is_err()) return rescanned;
            for (auto& item : *rescanned) output.push(rstd::move(item));
            index = next;
        }
        return Ok(rstd::move(output));
    }

    auto define_macro(const Vec<Token>& line, bool send_event) -> Result<empty> {
        auto parsed = parse_macro_definition(line);
        if (parsed.is_err()) return Err(rstd::move(parsed).unwrap_err());
        auto external =
            prepare_external_macro(line[usize {}].text.as_str(), line[usize {}].expansion);
        if (external.is_err()) return Err(rstd::move(external).unwrap_err());
        auto macro    = rstd::move(parsed).unwrap();
        auto previous = macros_.define(rstd::move(macro));
        (void)previous;
        if (send_event) {
            auto event = emit_name(
                EventKind::MacroDefined, line[usize {}].text.as_str(), line[usize {}].expansion);
            if (event.is_err()) return event;
        }
        return Ok(empty {});
    }

    auto replace_defined(const Vec<Token>& line) -> Result<Vec<Token>> {
        auto result = Vec<Token>::make();
        for (auto index = usize {}; index < line.len();) {
            const auto& token = line[index];
            if (token.text.as_str() != "defined"_str) {
                result.push(clone_token(token));
                ++index;
                continue;
            }
            auto cursor        = index + usize(1);
            auto parenthesized = cursor < line.len() && line[cursor].text.as_str() == "("_str;
            if (parenthesized) ++cursor;
            if (cursor >= line.len() || line[cursor].kind != TokenKind::Identifier) {
                return Err(failure("defined requires an identifier"_str, token.expansion));
            }
            const auto& identifier = line[cursor].text;
            auto        contains   = contains_macro(identifier.as_str(), line[cursor].expansion);
            if (contains.is_err()) return Err(rstd::move(contains).unwrap_err());
            auto value = *contains || DynamicBuiltinSet::contains(identifier.comparable_hash(),
                                                                  identifier.as_str());
            ++cursor;
            if (parenthesized) {
                if (cursor >= line.len() || line[cursor].text.as_str() != ")"_str) {
                    return Err(failure("defined requires a closing ')'"_str, token.expansion));
                }
                ++cursor;
            }
            result.push(number_token(i64(value), token));
            index = cursor;
        }
        return Ok(rstd::move(result));
    }

    auto condition_value(const Vec<Token>& line) -> Result<bool> {
        auto defined = replace_defined(line);
        if (defined.is_err()) return Err(rstd::move(defined).unwrap_err());
        auto disabled = DisabledMacros {};
        auto expanded = expand(rstd::move(defined).unwrap(), disabled);
        if (expanded.is_err()) return Err(rstd::move(expanded).unwrap_err());
        auto filtered = Vec<Token>::make();
        for (auto& token : *expanded) {
            if (token.kind != TokenKind::Newline) filtered.push(rstd::move(token));
        }
        auto value = evaluate_expression(filtered);
        if (value.is_err()) return Err(rstd::move(value).unwrap_err());
        return Ok(*value != i64 {});
    }

    auto directive_message(const Vec<Token>& line, usize begin) -> String {
        auto result = String::make();
        for (auto index = begin; index < line.len(); ++index) {
            if (! result.is_empty()) result.push_ascii(' ');
            result.push_str(line[index].text.as_str());
        }
        return result;
    }

    auto embed_parameter_name(ref<str> value) -> ref<str> {
        if (value.len() >= usize(4) && value.starts_with("__"_str) && value.ends_with("__"_str)) {
            auto inner = value.get(usize(2), value.len() - usize(2));
            if (inner.is_some()) return *inner;
        }
        return value;
    }

    auto embed_parameter_tokens(const Vec<Token>& line, usize& cursor, SourceLocation location)
        -> Result<Vec<Token>> {
        if (cursor >= line.len() || line[cursor].text.as_str() != "("_str) {
            return Err(failure("#embed parameter requires parentheses"_str, location));
        }
        ++cursor;
        auto depth  = usize(1);
        auto result = Vec<Token>::make();
        while (cursor < line.len()) {
            if (line[cursor].text.as_str() == "("_str) {
                ++depth;
            } else if (line[cursor].text.as_str() == ")"_str) {
                --depth;
                if (depth == usize {}) {
                    ++cursor;
                    return Ok(rstd::move(result));
                }
            }
            result.push(clone_token(line[cursor]));
            ++cursor;
        }
        return Err(failure("unterminated #embed parameter"_str, location));
    }

    auto embed_count(const Vec<Token>& tokens, ref<str> parameter, SourceLocation location)
        -> Result<usize> {
        if (tokens.is_empty()) {
            return Err(
                failure(rstd::format("#embed {} requires an expression", parameter), location));
        }
        auto value = evaluate_expression(tokens);
        if (value.is_err()) return Err(rstd::move(value).unwrap_err());
        if (*value < i64 {}) {
            return Err(
                failure(rstd::format("#embed {} must be non-negative", parameter), location));
        }
        return Ok(usize(static_cast<size_t>(value->to_primitive())));
    }

    auto parse_embed(Vec<Token> line, SourceLocation location) -> Result<EmbedDirective> {
        auto disabled = DisabledMacros {};
        auto expanded = expand(rstd::move(line), disabled);
        if (expanded.is_err()) return Err(rstd::move(expanded).unwrap_err());
        auto tokens = rstd::move(expanded).unwrap();
        if (tokens.is_empty()) {
            return Err(failure("#embed requires a resource"_str, location));
        }
        auto result = EmbedDirective {};
        auto cursor = usize {};
        if (tokens[cursor].kind == TokenKind::StringLiteral) {
            auto name = string_contents(tokens[cursor], "#embed"_str);
            if (name.is_err()) return Err(rstd::move(name).unwrap_err());
            result.name = rstd::move(name).unwrap();
            ++cursor;
        } else if (tokens[cursor].text.as_str() == "<"_str) {
            result.kind = EmbedKind::Angled;
            ++cursor;
            while (cursor < tokens.len() && tokens[cursor].text.as_str() != ">"_str) {
                result.name.push_str(tokens[cursor].text.as_str());
                ++cursor;
            }
            if (cursor >= tokens.len()) {
                return Err(failure("#embed has an unterminated angled resource"_str, location));
            }
            ++cursor;
        } else {
            return Err(failure("#embed requires a quoted or angled resource"_str, location));
        }
        if (result.name.is_empty()) {
            return Err(failure("#embed resource name is empty"_str, location));
        }

        auto saw_limit    = false;
        auto saw_offset   = false;
        auto saw_prefix   = false;
        auto saw_suffix   = false;
        auto saw_if_empty = false;
        while (cursor < tokens.len()) {
            if (tokens[cursor].kind != TokenKind::Identifier) {
                return Err(failure("invalid #embed parameter"_str, tokens[cursor].expansion));
            }
            auto component          = embed_parameter_name(tokens[cursor].text.as_str());
            auto name               = String::make(component);
            auto parameter_location = tokens[cursor].expansion;
            ++cursor;
            if (cursor + usize(1) < tokens.len() && tokens[cursor].text.as_str() == "::"_str &&
                tokens[cursor + usize(1)].kind == TokenKind::Identifier) {
                name.push_str("::"_str);
                name.push_str(embed_parameter_name(tokens[cursor + usize(1)].text.as_str()));
                cursor += usize(2);
            }
            auto value = embed_parameter_tokens(tokens, cursor, parameter_location);
            if (value.is_err()) return Err(rstd::move(value).unwrap_err());
            if (name.as_str() == "limit"_str) {
                if (saw_limit)
                    return Err(failure("duplicate #embed limit parameter"_str, parameter_location));
                auto count = embed_count(*value, "limit"_str, parameter_location);
                if (count.is_err()) return Err(rstd::move(count).unwrap_err());
                saw_limit    = true;
                result.limit = Some(*count);
            } else if (name.as_str() == "clang::offset"_str) {
                if (saw_offset)
                    return Err(
                        failure("duplicate #embed offset parameter"_str, parameter_location));
                auto count = embed_count(*value, "offset"_str, parameter_location);
                if (count.is_err()) return Err(rstd::move(count).unwrap_err());
                saw_offset    = true;
                result.offset = *count;
            } else if (name.as_str() == "prefix"_str) {
                if (saw_prefix)
                    return Err(
                        failure("duplicate #embed prefix parameter"_str, parameter_location));
                saw_prefix    = true;
                result.prefix = rstd::move(value).unwrap();
            } else if (name.as_str() == "suffix"_str) {
                if (saw_suffix)
                    return Err(
                        failure("duplicate #embed suffix parameter"_str, parameter_location));
                saw_suffix    = true;
                result.suffix = rstd::move(value).unwrap();
            } else if (name.as_str() == "if_empty"_str) {
                if (saw_if_empty) {
                    return Err(
                        failure("duplicate #embed if_empty parameter"_str, parameter_location));
                }
                saw_if_empty    = true;
                result.if_empty = rstd::move(value).unwrap();
            } else {
                return Err(failure(rstd::format("unsupported #embed parameter '{}'", name.as_str()),
                                   parameter_location));
            }
        }
        return Ok(rstd::move(result));
    }

    auto resolve_embed(const EmbedDirective& directive, SourceLocation location, bool probe)
        -> Result<Option<ResolvedEmbed>> {
        if (include_stack_.is_empty()) {
            return Err(failure("#embed has no current source"_str, location));
        }
        const auto& frame = include_stack_[include_stack_.len() - usize(1)];
        auto        resolved =
            as<EmbedResolver>(embed_resolver_)
                .resolve(EmbedRequest {
                    .name           = directive.name.clone(),
                    .kind           = directive.kind,
                    .including_path = rstd::path::PathBuf::from(sources_.path(frame.source)),
                    .location       = location,
                    .offset         = directive.offset,
                    .limit          = directive.limit,
                    .probe          = probe,
                });
        if (resolved.is_err()) return Err(rstd::move(resolved).unwrap_err());
        if (resolved->is_none()) return Ok(None());
        auto resource = rstd::move(resolved).unwrap().unwrap();
        if (resource.size > u64(usize::MAX.to_primitive())) {
            return Err(failure("embedded resource is too large for this host"_str, location));
        }
        auto size = usize(static_cast<size_t>(resource.size.to_primitive()));
        if (directive.offset > size) {
            return Err(failure("#embed offset exceeds the resource size"_str, location));
        }
        auto length = size - directive.offset;
        if (directive.limit.is_some() && *directive.limit < length) length = *directive.limit;
        return Ok(Some(ResolvedEmbed {
            .resource = rstd::move(resource),
            .length   = length,
        }));
    }

    auto record_embed(const EmbedDirective& directive, const ResolvedEmbed& resolved) -> void {
        embedded_inputs_.push(frontend::EmbeddedInput {
            .path   = resolved.resource.path.clone(),
            .size   = resolved.resource.size,
            .digest = resolved.resource.digest.clone(),
            .offset = directive.offset,
            .length = resolved.length,
        });
    }

    auto handle_embed(Vec<Token> line, SourceLocation location) -> Result<empty> {
        auto directive = parse_embed(rstd::move(line), location);
        if (directive.is_err()) return Err(rstd::move(directive).unwrap_err());
        auto resolved = resolve_embed(*directive, location, false);
        if (resolved.is_err()) return Err(rstd::move(resolved).unwrap_err());
        if (resolved->is_none()) {
            auto event = emit_name(EventKind::EmbedNotFound, directive->name.as_str(), location);
            if (event.is_err()) return Err(rstd::move(event).unwrap_err());
            return Err(failure(
                rstd::format("embedded resource '{}' was not found", directive->name.as_str()),
                location));
        }
        auto value = rstd::move(resolved).unwrap().unwrap();
        record_embed(*directive, value);
        auto event = emit(Event {
            .kind     = EventKind::EmbedResolved,
            .name     = directive->name.clone(),
            .path     = Some(value.resource.path.clone()),
            .location = location,
        });
        if (event.is_err()) return Err(rstd::move(event).unwrap_err());
        auto output = Vec<Token>::make();
        if (value.length == usize {}) {
            for (auto& token : directive->if_empty) output.push(rstd::move(token));
        } else {
            for (auto& token : directive->prefix) output.push(rstd::move(token));
            output.push(number_token(i64 {},
                                     Token {
                                         .kind      = TokenKind::PpNumber,
                                         .text      = String::make("0"_str),
                                         .spelling  = location,
                                         .expansion = location,
                                     }));
            for (auto& token : directive->suffix) output.push(rstd::move(token));
        }
        if (output.is_empty()) return Ok(empty {});
        ++raw_statistics_.consumer_batches;
        raw_statistics_.consumer_tokens += output.len().to_primitive();
        return as<PreprocessedTokenConsumer>(consumer_).consume(rstd::move(output));
    }

    auto include_name(const Vec<Token>& line, IncludeKind& kind, SourceLocation location)
        -> Result<String> {
        if (line.len() == usize(1) && line[usize {}].kind == TokenKind::StringLiteral) {
            auto text = line[usize {}].text.as_str();
            if (text.len() < usize(2)) return Err(failure("invalid #include header"_str, location));
            auto inner = text.get(usize(1), text.len() - usize(1));
            if (inner.is_none()) return Err(failure("invalid #include header"_str, location));
            return Ok(String::make(*inner));
        }
        if (line.len() >= usize(2) && line[usize {}].text.as_str() == "<"_str &&
            line[line.len() - usize(1)].text.as_str() == ">"_str) {
            kind = kind == IncludeKind::NextQuoted ? IncludeKind::NextAngled : IncludeKind::Angled;
            auto name = String::make();
            for (auto index = usize(1); index + usize(1) < line.len(); ++index) {
                name.push_str(line[index].text.as_str());
            }
            return Ok(rstd::move(name));
        }
        return Err(failure("#include requires a quoted or angled header"_str, location));
    }

    auto detected_include_guard(const Vec<Token>& tokens) -> Option<String> {
        auto cursor = usize {};
        while (cursor < tokens.len() && tokens[cursor].kind == TokenKind::Newline) ++cursor;
        if (cursor + usize(2) >= tokens.len() || ! tokens[cursor].start_of_line ||
            tokens[cursor].text.as_str() != "#"_str ||
            tokens[cursor + usize(1)].text.as_str() != "ifndef"_str ||
            tokens[cursor + usize(2)].kind != TokenKind::Identifier) {
            return None();
        }
        auto guard = tokens[cursor + usize(2)].text.clone();
        while (cursor < tokens.len() && tokens[cursor].kind != TokenKind::Newline) ++cursor;
        while (cursor < tokens.len() && tokens[cursor].kind == TokenKind::Newline) ++cursor;
        if (cursor + usize(2) >= tokens.len() || ! tokens[cursor].start_of_line ||
            tokens[cursor].text.as_str() != "#"_str ||
            tokens[cursor + usize(1)].text.as_str() != "define"_str ||
            tokens[cursor + usize(2)].text.as_str() != guard.as_str()) {
            return None();
        }
        auto last = tokens.len();
        while (last > usize {} && tokens[last - usize(1)].kind == TokenKind::Newline) --last;
        if (last < usize(2)) return None();
        auto line = last - usize(1);
        while (line > usize {} && ! tokens[line].start_of_line) --line;
        if (line >= last || tokens[line].text.as_str() != "#"_str || line + usize(1) >= last ||
            tokens[line + usize(1)].text.as_str() != "endif"_str) {
            return None();
        }
        return Some(rstd::move(guard));
    }

    auto handle_include(Vec<Token> line, bool next, SourceLocation location) -> Result<empty> {
        auto direct_header =
            line.len() == usize(1) && line[usize {}].kind == TokenKind::StringLiteral;
        direct_header =
            direct_header || (line.len() >= usize(2) && line[usize {}].text.as_str() == "<"_str &&
                              line[line.len() - usize(1)].text.as_str() == ">"_str);
        auto expanded = Result<Vec<Token>>(Ok(Vec<Token>::make()));
        if (! direct_header) {
            auto disabled = DisabledMacros {};
            expanded      = expand(rstd::move(line), disabled);
            if (expanded.is_err()) return Err(rstd::move(expanded).unwrap_err());
        } else {
            expanded = Ok(rstd::move(line));
        }
        auto kind = next ? IncludeKind::NextQuoted : IncludeKind::Quoted;
        auto name = include_name(*expanded, kind, location);
        if (name.is_err()) return Err(rstd::move(name).unwrap_err());
        const auto& frame = include_stack_[include_stack_.len() - usize(1)];
        auto        resolved =
            as<IncludeResolver>(include_resolver_)
                .resolve(IncludeRequest {
                    .name                  = name->clone(),
                    .kind                  = kind,
                    .including_path        = rstd::path::PathBuf::from(sources_.path(frame.source)),
                    .previous_search_index = frame.search_index,
                    .location              = location,
                });
        ++raw_statistics_.include_attempts;
        if (resolved.is_err()) return Err(rstd::move(resolved).unwrap_err());
        if (resolved->is_none()) {
            auto event = emit_name(EventKind::IncludeNotFound, name->as_str(), location);
            if (event.is_err()) return Err(rstd::move(event).unwrap_err());
            return Err(
                failure(rstd::format("header '{}' was not found", name->as_str()), location));
        }
        ++raw_statistics_.include_hits;
        auto value = rstd::move(resolved).unwrap().unwrap();
        auto event = emit(Event {
            .kind     = EventKind::IncludeResolved,
            .name     = rstd::move(name).unwrap(),
            .path     = Some(value.path.clone()),
            .location = location,
        });
        if (event.is_err()) return Err(rstd::move(event).unwrap_err());
        return process_file(value.path.as_path(), Some(value.search_index));
    }

    auto handle_conditional(ref<str>               directive,
                            const Vec<Token>&      rest,
                            SourceLocation         location,
                            Vec<ConditionalFrame>& conditions) -> Result<bool> {
        if (directive == "if"_str || directive == "ifdef"_str || directive == "ifndef"_str) {
            auto parent = active(conditions);
            auto value  = false;
            if (parent) {
                if (directive == "if"_str) {
                    auto evaluated = condition_value(rest);
                    if (evaluated.is_err()) return Err(rstd::move(evaluated).unwrap_err());
                    value = *evaluated;
                } else {
                    if (rest.len() != usize(1) || rest[usize {}].kind != TokenKind::Identifier) {
                        return Err(
                            failure("conditional directive requires one identifier"_str, location));
                    }
                    const auto& identifier = rest[usize {}].text;
                    auto contains = contains_macro(identifier.as_str(), rest[usize {}].expansion);
                    if (contains.is_err()) return Err(rstd::move(contains).unwrap_err());
                    value = *contains || DynamicBuiltinSet::contains(identifier.comparable_hash(),
                                                                     identifier.as_str());
                    if (directive == "ifndef"_str) value = ! value;
                }
            }
            conditions.push(ConditionalFrame {
                .parent_active = parent,
                .branch_taken  = parent && value,
                .active        = parent && value,
                .location      = location,
            });
            auto event =
                emit_name(EventKind::Conditional, value ? "true"_str : "false"_str, location);
            if (event.is_err()) return Err(rstd::move(event).unwrap_err());
            return Ok(true);
        }
        if (directive == "elif"_str || directive == "elifdef"_str || directive == "elifndef"_str) {
            if (conditions.is_empty()) return Err(failure("#elif without #if"_str, location));
            auto& frame = conditions[conditions.len() - usize(1)];
            if (frame.saw_else) return Err(failure("#elif after #else"_str, location));
            auto value = false;
            if (frame.parent_active && ! frame.branch_taken) {
                if (directive == "elif"_str) {
                    auto evaluated = condition_value(rest);
                    if (evaluated.is_err()) return Err(rstd::move(evaluated).unwrap_err());
                    value = *evaluated;
                } else {
                    if (rest.len() != usize(1) || rest[usize {}].kind != TokenKind::Identifier) {
                        return Err(
                            failure("conditional directive requires one identifier"_str, location));
                    }
                    const auto& identifier = rest[usize {}].text;
                    auto contains = contains_macro(identifier.as_str(), rest[usize {}].expansion);
                    if (contains.is_err()) return Err(rstd::move(contains).unwrap_err());
                    value = *contains || DynamicBuiltinSet::contains(identifier.comparable_hash(),
                                                                     identifier.as_str());
                    if (directive == "elifndef"_str) value = ! value;
                }
            }
            frame.active       = frame.parent_active && ! frame.branch_taken && value;
            frame.branch_taken = frame.branch_taken || frame.active;
            return Ok(true);
        }
        if (directive == "else"_str) {
            if (conditions.is_empty()) return Err(failure("#else without #if"_str, location));
            auto& frame = conditions[conditions.len() - usize(1)];
            if (frame.saw_else) return Err(failure("duplicate #else"_str, location));
            frame.saw_else     = true;
            frame.active       = frame.parent_active && ! frame.branch_taken;
            frame.branch_taken = true;
            return Ok(true);
        }
        if (directive == "endif"_str) {
            if (conditions.is_empty()) return Err(failure("#endif without #if"_str, location));
            (void)conditions.pop();
            return Ok(true);
        }
        return Ok(false);
    }

    auto flush_normal(Vec<Token>& normal) -> Result<empty> {
        if (normal.is_empty()) return Ok(empty {});
        auto module_names = validate_module_name_macros(normal);
        if (module_names.is_err()) return Err(rstd::move(module_names).unwrap_err());
        auto disabled = DisabledMacros {};
        auto expanded = expand(rstd::move(normal), disabled);
        normal        = Vec<Token>::make();
        if (expanded.is_err()) return Err(rstd::move(expanded).unwrap_err());
        ++raw_statistics_.consumer_batches;
        raw_statistics_.consumer_tokens += expanded->len().to_primitive();
        return as<PreprocessedTokenConsumer>(consumer_).consume(rstd::move(expanded).unwrap());
    }

    auto validate_module_name_macros(const Vec<Token>& tokens) -> Result<empty> {
        for (auto index = usize {}; index < tokens.len(); ++index) {
            if (! tokens[index].start_of_line || tokens[index].kind != TokenKind::Identifier) {
                continue;
            }
            auto cursor = index;
            if (tokens[cursor].text.as_str() == "export"_str) {
                ++cursor;
                while (cursor < tokens.len() && tokens[cursor].kind == TokenKind::Newline) ++cursor;
            }
            if (cursor >= tokens.len() || (tokens[cursor].text.as_str() != "module"_str &&
                                           tokens[cursor].text.as_str() != "import"_str)) {
                continue;
            }
            ++cursor;
            while (cursor < tokens.len() && tokens[cursor].kind == TokenKind::Newline) ++cursor;
            if (cursor >= tokens.len() || tokens[cursor].text.as_str() == ";"_str ||
                tokens[cursor].text.as_str() == "<"_str ||
                tokens[cursor].kind == TokenKind::StringLiteral ||
                tokens[cursor].kind == TokenKind::HeaderName) {
                continue;
            }
            if (tokens[cursor].text.as_str() == ":"_str) ++cursor;
            while (cursor < tokens.len()) {
                while (cursor < tokens.len() && tokens[cursor].kind == TokenKind::Newline) ++cursor;
                if (cursor >= tokens.len() || tokens[cursor].kind != TokenKind::Identifier) break;
                auto macro = lookup_macro(tokens[cursor].text.as_str(), tokens[cursor].expansion);
                if (macro.is_err()) return Err(rstd::move(macro).unwrap_err());
                if (macro->is_some() && (***macro).parameters.is_none()) {
                    return Err(failure(
                        rstd::format(
                            "module name identifier '{}' is defined as an object-like macro",
                            tokens[cursor].text.as_str()),
                        tokens[cursor].expansion));
                }
                ++cursor;
                while (cursor < tokens.len() && tokens[cursor].kind == TokenKind::Newline) ++cursor;
                if (cursor >= tokens.len() || (tokens[cursor].text.as_str() != "."_str &&
                                               tokens[cursor].text.as_str() != ":"_str)) {
                    break;
                }
                ++cursor;
            }
        }
        return Ok(empty {});
    }

    auto collect_comments_through(const Vec<CommentTrivia>& comments,
                                  usize&                    cursor,
                                  usize                     offset,
                                  SourceId                  source,
                                  bool                      enabled) -> void {
        while (cursor < comments.len() && comments[cursor].begin.offset <= offset) {
            if (enabled) {
                auto comment         = comments[cursor].clone();
                comment.begin.source = source;
                comment.end.source   = source;
                active_comments_.push(rstd::move(comment));
                ++raw_statistics_.active_comments;
            }
            ++cursor;
        }
    }

    auto process_file(ref<rstd::path::Path> path, Option<usize> search_index) -> Result<empty> {
        if (include_stack_.len() >= request_.maximum_include_depth) {
            return Err(Error::make(rstd::format("maximum include depth exceeded at '{}'", path)));
        }
        auto path_value = path.to_str();
        if (path_value.is_some() && once_files_.contains_key(*path_value)) {
            return Ok(empty {});
        }
        if (path_value.is_some()) {
            auto guard = include_guards_.get(*path_value);
            if (guard.is_some()) {
                auto defined = contains_macro((**guard).as_str(), SourceLocation {});
                if (defined.is_err()) return Err(rstd::move(defined).unwrap_err());
                if (*defined) return Ok(empty {});
            }
        }
        auto loaded = as<SourceProvider>(source_provider_).load(path);
        if (loaded.is_err()) return Err(rstd::move(loaded).unwrap_err());
        input_bytes_ += (*loaded)->snapshot->contents.len();
        ++raw_statistics_.files;
        raw_statistics_.source_tokens += (*loaded)->tokens.len().to_primitive();
        raw_statistics_.source_comments += (*loaded)->comments.len().to_primitive();
        auto source    = sources_.add((*loaded)->snapshot.clone());
        auto main_file = include_stack_.is_empty();
        if (main_file) main_source_ = source;
        auto tokens = Vec<Token>::with_capacity((*loaded)->tokens.len());
        for (const auto& cached : (*loaded)->tokens) {
            auto token             = clone_token(cached);
            token.spelling.source  = source;
            token.expansion.source = source;
            tokens.push(rstd::move(token));
        }
        if (path_value.is_some()) {
            auto guard = detected_include_guard(tokens);
            if (guard.is_some()) {
                include_guards_.insert(String::make(*path_value), rstd::move(guard).unwrap());
            }
        }
        include_stack_.push(IncludeFrame { .source = source, .search_index = search_index });
        auto entered = emit(Event {
            .kind     = EventKind::EnterFile,
            .path     = Some(rstd::path::PathBuf::from(sources_.path(source))),
            .location = SourceLocation { .source = source },
        });
        if (entered.is_err()) return Err(rstd::move(entered).unwrap_err());

        auto normal         = Vec<Token>::make();
        auto conditions     = Vec<ConditionalFrame>::make();
        auto comment_cursor = usize {};
        for (auto cursor = usize {}; cursor < tokens.len();) {
            auto end = cursor;
            while (end < tokens.len() && tokens[end].kind != TokenKind::Newline) ++end;
            auto directive = cursor < end && tokens[cursor].start_of_line &&
                             tokens[cursor].text.as_str() == "#"_str;
            auto line_end  = end < tokens.len() ? tokens[end].spelling.offset
                                                : (*loaded)->snapshot->contents.len();
            collect_comments_through((*loaded)->comments,
                                     comment_cursor,
                                     line_end,
                                     source,
                                     ! directive && active(conditions));
            if (! directive) {
                if (active(conditions)) {
                    for (auto index = cursor; index < end; ++index)
                        normal.push(rstd::move(tokens[index]));
                    if (end < tokens.len()) normal.push(rstd::move(tokens[end]));
                }
                cursor = end < tokens.len() ? end + usize(1) : end;
                continue;
            }

            ++raw_statistics_.directives;

            auto flushed = flush_normal(normal);
            if (flushed.is_err()) return Err(rstd::move(flushed).unwrap_err());
            auto line = without_newline(tokens, cursor + usize(1), end);
            if (line.is_err()) return Err(rstd::move(line).unwrap_err());
            if (line->is_empty()) {
                cursor = end < tokens.len() ? end + usize(1) : end;
                continue;
            }
            if ((*line)[usize {}].kind != TokenKind::Identifier) {
                return Err(
                    failure("invalid preprocessing directive"_str, (*line)[usize {}].expansion));
            }
            auto keyword     = (*line)[usize {}].text.as_str();
            auto location    = (*line)[usize {}].expansion;
            auto rest        = move_range(*line, usize(1), line->len());
            auto conditional = handle_conditional(keyword, rest, location, conditions);
            if (conditional.is_err()) return Err(rstd::move(conditional).unwrap_err());
            if (*conditional) {
                ++raw_statistics_.conditionals;
                cursor = end < tokens.len() ? end + usize(1) : end;
                continue;
            }
            if (! active(conditions)) {
                cursor = end < tokens.len() ? end + usize(1) : end;
                continue;
            }

            if (keyword == "define"_str) {
                auto result = define_macro(rest, true);
                if (result.is_err()) return Err(rstd::move(result).unwrap_err());
            } else if (keyword == "undef"_str) {
                if (rest.len() != usize(1) || rest[usize {}].kind != TokenKind::Identifier) {
                    return Err(failure("#undef requires one identifier"_str, location));
                }
                auto external =
                    prepare_external_macro(rest[usize {}].text.as_str(), rest[usize {}].expansion);
                if (external.is_err()) return Err(rstd::move(external).unwrap_err());
                auto removed = macros_.undefine(rest[usize {}].text.as_str());
                (void)removed;
                auto event =
                    emit_name(EventKind::MacroUndefined, rest[usize {}].text.as_str(), location);
                if (event.is_err()) return Err(rstd::move(event).unwrap_err());
            } else if (keyword == "include"_str || keyword == "include_next"_str) {
                auto included =
                    handle_include(rstd::move(rest), keyword == "include_next"_str, location);
                if (included.is_err()) return Err(rstd::move(included).unwrap_err());
            } else if (keyword == "embed"_str) {
                auto embedded = handle_embed(rstd::move(rest), location);
                if (embedded.is_err()) return Err(rstd::move(embedded).unwrap_err());
            } else if (keyword == "pragma"_str) {
                auto handled = handle_pragma(rstd::move(rest), location);
                if (handled.is_err()) return Err(rstd::move(handled).unwrap_err());
            } else if (keyword == "error"_str) {
                return Err(
                    failure(rstd::format("#error {}", directive_message(rest, usize {}).as_str()),
                            location));
            } else if (keyword == "warning"_str) {
                auto event = emit_name(
                    EventKind::Diagnostic, directive_message(rest, usize {}).as_str(), location);
                if (event.is_err()) return Err(rstd::move(event).unwrap_err());
            } else if (keyword == "line"_str) {
                auto disabled = DisabledMacros {};
                auto expanded = expand(rstd::move(rest), disabled);
                if (expanded.is_err()) return Err(rstd::move(expanded).unwrap_err());
                if (expanded->is_empty() || (*expanded)[usize {}].kind != TokenKind::PpNumber) {
                    return Err(failure("#line requires a line number"_str, location));
                }
                auto number = Vec<Token>::make();
                number.push(clone_token((*expanded)[usize {}]));
                auto value = evaluate_expression(number);
                if (value.is_err() || *value <= i64 {}) {
                    return Err(failure("#line requires a positive line number"_str, location));
                }
                auto presumed = Option<rstd::path::PathBuf> {};
                if (expanded->len() > usize(1)) {
                    if ((*expanded)[usize(1)].kind != TokenKind::StringLiteral) {
                        return Err(
                            failure("#line file name must be a string literal"_str, location));
                    }
                    auto text = (*expanded)[usize(1)].text.as_str();
                    if (text.len() < usize(2)) {
                        return Err(failure("invalid #line file name"_str, location));
                    }
                    auto inner = text.get(usize(1), text.len() - usize(1));
                    if (inner.is_none()) {
                        return Err(failure("invalid #line file name"_str, location));
                    }
                    presumed = Some(rstd::path::PathBuf::from(*inner));
                }
                auto physical  = end < tokens.len() ? tokens[end].expansion.line + usize(1)
                                                    : location.line + usize(1);
                auto requested = as_cast<usize>(*value);
                for (auto adjust = end < tokens.len() ? end + usize(1) : end; adjust < tokens.len();
                     ++adjust) {
                    if (tokens[adjust].expansion.line >= physical) {
                        auto line = requested + (tokens[adjust].expansion.line - physical);
                        tokens[adjust].spelling.line  = line;
                        tokens[adjust].expansion.line = line;
                        if (presumed.is_some()) {
                            tokens[adjust].presumed_path =
                                Some(rstd::path::PathBuf::from(presumed->as_path()));
                        }
                    }
                }
            } else {
                return Err(failure(
                    rstd::format("unsupported preprocessing directive '#{}'", keyword), location));
            }
            cursor = end < tokens.len() ? end + usize(1) : end;
        }
        collect_comments_through((*loaded)->comments,
                                 comment_cursor,
                                 (*loaded)->snapshot->contents.len(),
                                 source,
                                 active(conditions));
        auto flushed = flush_normal(normal);
        if (flushed.is_err()) return Err(rstd::move(flushed).unwrap_err());
        if (! conditions.is_empty()) {
            return Err(failure("unterminated conditional directive"_str,
                               conditions[conditions.len() - usize(1)].location));
        }
        auto exited = emit(Event {
            .kind     = EventKind::ExitFile,
            .path     = Some(rstd::path::PathBuf::from(sources_.path(source))),
            .location = SourceLocation { .source = source },
        });
        if (exited.is_err()) return Err(rstd::move(exited).unwrap_err());
        (void)include_stack_.pop();
        return Ok(empty {});
    }

    PreprocessRequest                           request_;
    Sources&                                    source_provider_;
    Includes&                                   include_resolver_;
    Embeds&                                     embed_resolver_;
    Builtins&                                   builtin_provider_;
    Externals&                                  external_macro_provider_;
    Identifiers&                                identifier_matcher_;
    Pragmas&                                    pragma_handler_;
    Events&                                     event_sink_;
    Consumer&                                   consumer_;
    Observer&                                   observer_;
    SourceManager                               sources_;
    MacroTable                                  macros_;
    rstd::collections::BTreeMap<String, empty>  resolved_external_macros_;
    Vec<frontend::ExternalMacroMaterialization> external_macros_;
    Vec<frontend::EmbeddedInput>                embedded_inputs_;
    Vec<IncludeFrame>                           include_stack_;
    Vec<CommentTrivia>                          active_comments_;
    rstd::collections::BTreeMap<String, empty>  once_files_;
    rstd::collections::BTreeMap<String, String> include_guards_;
    rstd::collections::BTreeMap<String, Vec<Option<SharedMacroDefinition>>> macro_stacks_;
    usize                                                                   counter_ {};
    usize                                                                   input_bytes_ {};
    SourceId                                                                main_source_ {};
    String                                                                  builtin_identity_;
    RawStatistics                                                           raw_statistics_;
};

class CollectPreprocessedTokens {
public:
    auto consume(Vec<Token> tokens) -> Result<empty> {
        for (auto& token : tokens) tokens_.push(rstd::move(token));
        return Ok(empty {});
    }

    auto take() -> Vec<Token> { return rstd::move(tokens_); }

private:
    Vec<Token> tokens_;
};

template<typename Sources,
         typename Includes,
         typename Embeds,
         typename Builtins,
         typename Externals,
         typename Identifiers,
         typename Pragmas,
         typename Events,
         typename Consumer,
         typename Observer>
    requires Impled<Identifiers, TokenMatcher>
auto preprocess_with_embeds_to(PreprocessRequest request,
                               Sources&          sources,
                               Includes&         includes,
                               Embeds&           embeds,
                               Builtins&         builtins,
                               Externals&        externals,
                               Identifiers&      identifiers,
                               Pragmas&          pragmas,
                               Events&           events,
                               Consumer&         consumer,
                               Observer&         observer) -> Result<PreprocessedTranslationUnit> {
    return PreprocessorSession<Sources,
                               Includes,
                               Embeds,
                               Builtins,
                               Externals,
                               Identifiers,
                               Pragmas,
                               Events,
                               Consumer,
                               Observer>(rstd::move(request),
                                         sources,
                                         includes,
                                         embeds,
                                         builtins,
                                         externals,
                                         identifiers,
                                         pragmas,
                                         events,
                                         consumer,
                                         observer)
        .run();
}

template<typename Sources,
         typename Includes,
         typename Builtins,
         typename Externals,
         typename Identifiers,
         typename Pragmas,
         typename Events,
         typename Consumer,
         typename Observer>
    requires Impled<Identifiers, TokenMatcher>
auto preprocess_to(PreprocessRequest request,
                   Sources&          sources,
                   Includes&         includes,
                   Builtins&         builtins,
                   Externals&        externals,
                   Identifiers&      identifiers,
                   Pragmas&          pragmas,
                   Events&           events,
                   Consumer&         consumer,
                   Observer&         observer) -> Result<PreprocessedTranslationUnit> {
    auto embeds = UnsupportedEmbedResolver {};
    return preprocess_with_embeds_to(rstd::move(request),
                                     sources,
                                     includes,
                                     embeds,
                                     builtins,
                                     externals,
                                     identifiers,
                                     pragmas,
                                     events,
                                     consumer,
                                     observer);
}

template<typename Sources,
         typename Includes,
         typename Builtins,
         typename Externals,
         typename Identifiers,
         typename Pragmas,
         typename Events,
         typename Consumer>
    requires Impled<Identifiers, TokenMatcher>
auto preprocess_to(PreprocessRequest request,
                   Sources&          sources,
                   Includes&         includes,
                   Builtins&         builtins,
                   Externals&        externals,
                   Identifiers&      identifiers,
                   Pragmas&          pragmas,
                   Events&           events,
                   Consumer&         consumer) -> Result<PreprocessedTranslationUnit> {
    auto observer = IgnorePreprocessorObserver {};
    return preprocess_to(rstd::move(request),
                         sources,
                         includes,
                         builtins,
                         externals,
                         identifiers,
                         pragmas,
                         events,
                         consumer,
                         observer);
}

template<typename Sources,
         typename Includes,
         typename Builtins,
         typename Identifiers,
         typename Pragmas,
         typename Events,
         typename Consumer,
         typename Observer>
    requires Impled<Identifiers, TokenMatcher>
auto preprocess_to(PreprocessRequest request,
                   Sources&          sources,
                   Includes&         includes,
                   Builtins&         builtins,
                   Identifiers&      identifiers,
                   Pragmas&          pragmas,
                   Events&           events,
                   Consumer&         consumer,
                   Observer&         observer) -> Result<PreprocessedTranslationUnit> {
    auto externals = EmptyExternalMacroProvider {};
    return preprocess_to(rstd::move(request),
                         sources,
                         includes,
                         builtins,
                         externals,
                         identifiers,
                         pragmas,
                         events,
                         consumer,
                         observer);
}

template<typename Sources,
         typename Includes,
         typename Builtins,
         typename Identifiers,
         typename Pragmas,
         typename Events,
         typename Consumer>
    requires Impled<Identifiers, TokenMatcher>
auto preprocess_to(PreprocessRequest request,
                   Sources&          sources,
                   Includes&         includes,
                   Builtins&         builtins,
                   Identifiers&      identifiers,
                   Pragmas&          pragmas,
                   Events&           events,
                   Consumer&         consumer) -> Result<PreprocessedTranslationUnit> {
    auto externals = EmptyExternalMacroProvider {};
    auto observer  = IgnorePreprocessorObserver {};
    return preprocess_to(rstd::move(request),
                         sources,
                         includes,
                         builtins,
                         externals,
                         identifiers,
                         pragmas,
                         events,
                         consumer,
                         observer);
}

template<typename Sources,
         typename Includes,
         typename Builtins,
         typename Externals,
         typename Identifiers,
         typename Pragmas,
         typename Events>
    requires Impled<Identifiers, TokenMatcher>
auto preprocess(PreprocessRequest request,
                Sources&          sources,
                Includes&         includes,
                Builtins&         builtins,
                Externals&        externals,
                Identifiers&      identifiers,
                Pragmas&          pragmas,
                Events&           events) -> Result<PreprocessedTranslationUnit> {
    auto consumer = CollectPreprocessedTokens {};
    auto result   = preprocess_to(rstd::move(request),
                                  sources,
                                  includes,
                                  builtins,
                                  externals,
                                  identifiers,
                                  pragmas,
                                  events,
                                  consumer);
    if (result.is_err()) return Err(rstd::move(result).unwrap_err());
    result->tokens = consumer.take();
    return result;
}

template<typename Sources,
         typename Includes,
         typename Builtins,
         typename Identifiers,
         typename Pragmas,
         typename Events>
    requires Impled<Identifiers, TokenMatcher>
auto preprocess(PreprocessRequest request,
                Sources&          sources,
                Includes&         includes,
                Builtins&         builtins,
                Identifiers&      identifiers,
                Pragmas&          pragmas,
                Events&           events) -> Result<PreprocessedTranslationUnit> {
    auto externals = EmptyExternalMacroProvider {};
    return preprocess(
        rstd::move(request), sources, includes, builtins, externals, identifiers, pragmas, events);
}

} // namespace lito::frontend::preprocessor
