export module lito.frontend.preprocessor:traits;

import rstd;
import lito.frontend.lexical;
import lito.frontend.result;
import :builtin;
import :macro;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::frontend::preprocessor
{

using lexical::Error;
using lexical::SourceBuffer;
using lexical::SourceFile;
using lexical::SourceId;
using lexical::SourceLocation;
using lexical::SourceManager;
using lexical::ScanFileStorage;
using lexical::SharedScanFileStorage;
using lexical::SourceTokenView;
using lexical::CommentTrivia;
using lexical::CommentKind;
using lexical::CommentStyle;
using lexical::make_source_snapshot;
using lexical::Token;
using lexical::TokenKind;
using lexical::lex;
using lexical::lex_scan_file;
template<typename T>
using Result = lexical::Result<T>;

using IncludeKind = frontend::IncludeLookupKind;

struct IncludeRequest {
    String              name;
    IncludeKind         kind { IncludeKind::Quoted };
    rstd::path::PathBuf including_path;
    Option<usize>       previous_search_index;
    SourceLocation      location;
};

struct IncludeResolution {
    rstd::path::PathBuf path;
    usize               search_index {};
    bool                system { false };
};

using EmbedKind = frontend::EmbedLookupKind;

struct EmbedRequest {
    String              name;
    EmbedKind           kind { EmbedKind::Quoted };
    rstd::path::PathBuf including_path;
    SourceLocation      location;
    usize               offset {};
    Option<usize>       limit;
    bool                probe { false };
};

struct EmbedResolution {
    rstd::path::PathBuf path;
    usize               search_index {};
    u64                 size {};
    String              digest;
};

enum class BuiltinTextKind
{
    Date,
    Time,
};

enum class PredefinedMacroOperationKind
{
    Define,
    Undefine,
};

struct PredefinedMacroOperation {
    PredefinedMacroOperationKind  kind { PredefinedMacroOperationKind::Define };
    String                        name;
    Option<SharedMacroDefinition> definition;

    static auto define(SharedMacroDefinition value) -> PredefinedMacroOperation {
        return PredefinedMacroOperation {
            .kind       = PredefinedMacroOperationKind::Define,
            .definition = Some(rstd::move(value)),
        };
    }

    static auto undefine(String name) -> PredefinedMacroOperation {
        return PredefinedMacroOperation {
            .kind = PredefinedMacroOperationKind::Undefine,
            .name = rstd::move(name),
        };
    }
};

enum class PragmaOutcome
{
    Ignored,
    Handled,
};

struct PragmaRequest {
    Vec<Token>     tokens;
    SourceLocation location;
};

enum class EventKind
{
    EnterFile,
    ExitFile,
    IncludeResolved,
    IncludeNotFound,
    IncludeProbeResolved,
    IncludeProbeNotFound,
    EmbedResolved,
    EmbedNotFound,
    EmbedProbeResolved,
    EmbedProbeNotFound,
    MacroDefined,
    MacroUndefined,
    MacroExpanded,
    Conditional,
    Diagnostic,
};

struct Event {
    EventKind                   kind { EventKind::EnterFile };
    String                      name;
    Option<rstd::path::PathBuf> path;
    SourceLocation              location;
};

enum class SourceLoadRole
{
    Primary,
    Include,
};

struct SourceProvider {
    template<typename Self, typename = void>
    struct Api {
        using Trait = SourceProvider;

        auto load(ref<rstd::path::Path> path, SourceLoadRole role)
            -> Result<SharedScanFileStorage> {
            return rstd::trait_call<0>(this, path, role);
        }
    };

    template<typename T>
    using Funcs = TraitFuncs<&T::load>;
};

struct IncludeResolver {
    template<typename Self, typename = void>
    struct Api {
        using Trait = IncludeResolver;

        auto resolve(const IncludeRequest& request) -> Result<Option<IncludeResolution>> {
            return rstd::trait_call<0>(this, request);
        }
    };

    template<typename T>
    using Funcs = TraitFuncs<&T::resolve>;
};

struct EmbedResolver {
    template<typename Self, typename = void>
    struct Api {
        using Trait = EmbedResolver;

        auto resolve(const EmbedRequest& request) -> Result<Option<EmbedResolution>> {
            return rstd::trait_call<0>(this, request);
        }
    };

    template<typename T>
    using Funcs = TraitFuncs<&T::resolve>;
};

class UnsupportedEmbedResolver {
public:
    auto resolve(const EmbedRequest& request) -> Result<Option<EmbedResolution>> {
        return Err(Error::at(String::make("#embed is not supported by this frontend context"_str),
                             request.location));
    }
};

struct BuiltinProvider {
    template<typename Self, typename = void>
    struct Api {
        using Trait = BuiltinProvider;

        auto predefined_macros() -> Result<Vec<PredefinedMacroOperation>> {
            return rstd::trait_call<0>(this);
        }

        auto evaluate(const BuiltinQueryKey& query) -> Result<i64> {
            return rstd::trait_call<1>(this, query);
        }

        auto text(BuiltinTextKind kind) -> Result<String> {
            return rstd::trait_call<2>(this, kind);
        }
    };

    template<typename T>
    using Funcs = TraitFuncs<&T::predefined_macros, &T::evaluate, &T::text>;
};

struct ExternalMacroResolution {
    String                        dependency_key;
    String                        value_identity;
    frontend::ExternalMacroState  state { frontend::ExternalMacroState::Undefined };
    Option<String>                compiler_definition;
    Option<SharedMacroDefinition> definition;
};

struct ExternalMacroProvider {
    template<typename Self, typename = void>
    struct Api {
        using Trait = ExternalMacroProvider;

        auto resolve(ref<str> name, SourceLocation location)
            -> Result<Option<ExternalMacroResolution>> {
            return rstd::trait_call<0>(this, name, location);
        }
    };

    template<typename T>
    using Funcs = TraitFuncs<&T::resolve>;
};

class EmptyExternalMacroProvider {
public:
    auto resolve(ref<str>, SourceLocation) -> Result<Option<ExternalMacroResolution>> {
        return Ok(None());
    }
};

struct PragmaHandler {
    template<typename Self, typename = void>
    struct Api {
        using Trait = PragmaHandler;

        auto handle(const PragmaRequest& request) -> Result<PragmaOutcome> {
            return rstd::trait_call<0>(this, request);
        }
    };

    template<typename T>
    using Funcs = TraitFuncs<&T::handle>;
};

struct PreprocessorEventSink {
    template<typename Self, typename = void>
    struct Api {
        using Trait = PreprocessorEventSink;

        auto wants(EventKind kind) const -> bool { return rstd::trait_call<0>(this, kind); }

        auto on_event(const Event& event) -> Result<empty> {
            return rstd::trait_call<1>(this, event);
        }
    };

    template<typename T>
    using Funcs = TraitFuncs<&T::wants, &T::on_event>;
};

struct PreprocessedTokenConsumer {
    template<typename Self, typename = void>
    struct Api {
        using Trait = PreprocessedTokenConsumer;

        auto consume(Vec<Token> tokens) -> Result<empty> {
            return rstd::trait_call<0>(this, rstd::move(tokens));
        }
    };

    template<typename T>
    using Funcs = TraitFuncs<&T::consume>;
};

enum class PreprocessorActivity
{
    PredefinedMacros,
    TranslationUnit,
};

struct PreprocessorStatistics {
    usize files {};
    usize source_tokens {};
    usize source_comments {};
    usize active_comments {};
    usize token_clones {};
    usize source_token_materializations {};
    usize macro_literal_clones {};
    usize macro_raw_argument_clones {};
    usize macro_expansion_input_clones {};
    usize macro_expanded_argument_reuse_clones {};
    usize other_token_clones {};
    usize synthetic_tokens {};
    usize directives {};
    usize conditionals {};
    usize macro_lookups {};
    usize macro_negative_cache_hits {};
    usize macro_expansions {};
    usize macro_definitions {};
    usize macro_operations {};
    usize macro_expanded_parameter_uses {};
    usize macro_raw_parameter_uses {};
    usize macro_stringifications {};
    usize macro_token_pastes {};
    usize macro_va_opt_uses {};
    usize pragma_fragment_lexes {};
    usize macro_identifier_validations {};
    usize include_attempts {};
    usize include_hits {};
    usize consumer_batches {};
    usize consumer_tokens {};

    auto add(const PreprocessorStatistics& other) noexcept -> void {
        files += other.files;
        source_tokens += other.source_tokens;
        source_comments += other.source_comments;
        active_comments += other.active_comments;
        token_clones += other.token_clones;
        source_token_materializations += other.source_token_materializations;
        macro_literal_clones += other.macro_literal_clones;
        macro_raw_argument_clones += other.macro_raw_argument_clones;
        macro_expansion_input_clones += other.macro_expansion_input_clones;
        macro_expanded_argument_reuse_clones += other.macro_expanded_argument_reuse_clones;
        other_token_clones += other.other_token_clones;
        synthetic_tokens += other.synthetic_tokens;
        directives += other.directives;
        conditionals += other.conditionals;
        macro_lookups += other.macro_lookups;
        macro_negative_cache_hits += other.macro_negative_cache_hits;
        macro_expansions += other.macro_expansions;
        macro_definitions += other.macro_definitions;
        macro_operations += other.macro_operations;
        macro_expanded_parameter_uses += other.macro_expanded_parameter_uses;
        macro_raw_parameter_uses += other.macro_raw_parameter_uses;
        macro_stringifications += other.macro_stringifications;
        macro_token_pastes += other.macro_token_pastes;
        macro_va_opt_uses += other.macro_va_opt_uses;
        pragma_fragment_lexes += other.pragma_fragment_lexes;
        macro_identifier_validations += other.macro_identifier_validations;
        include_attempts += other.include_attempts;
        include_hits += other.include_hits;
        consumer_batches += other.consumer_batches;
        consumer_tokens += other.consumer_tokens;
    }
};

struct PreprocessorObserver {
    template<typename Self, typename = void>
    struct Api {
        using Trait = PreprocessorObserver;

        auto begin(PreprocessorActivity activity) -> void { rstd::trait_call<0>(this, activity); }

        auto end(PreprocessorActivity activity) -> void { rstd::trait_call<1>(this, activity); }

        auto record(const PreprocessorStatistics& statistics) -> void {
            rstd::trait_call<2>(this, statistics);
        }
    };

    template<typename T>
    using Funcs = TraitFuncs<&T::begin, &T::end, &T::record>;
};

struct IgnorePreprocessorObserver {
    auto begin(PreprocessorActivity) -> void {}
    auto end(PreprocessorActivity) -> void {}
    auto record(const PreprocessorStatistics&) -> void {}
};

struct IgnorePragmas {
    auto handle(const PragmaRequest&) -> Result<PragmaOutcome> {
        return Ok(PragmaOutcome::Ignored);
    }
};

struct IgnoreEvents {
    auto wants(EventKind) const -> bool { return false; }
    auto on_event(const Event&) -> Result<empty> { return Ok(empty {}); }
};

} // namespace lito::frontend::preprocessor
