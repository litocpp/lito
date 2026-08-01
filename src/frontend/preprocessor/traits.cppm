export module tenon.frontend.preprocessor:traits;

import rstd;
import tenon.frontend.lexical;
import :macro;

using namespace rstd::prelude;

export namespace tenon::frontend::preprocessor {

using lexical::Error;
using lexical::SourceBuffer;
using lexical::SourceFile;
using lexical::SourceId;
using lexical::SourceLocation;
using lexical::SourceManager;
using lexical::LexedSource;
using lexical::SharedLexedSource;
using lexical::make_source_snapshot;
using lexical::Token;
using lexical::TokenKind;
using lexical::lex;
template <typename T> using Result = lexical::Result<T>;

enum class IncludeKind {
  Quoted,
  Angled,
  NextQuoted,
  NextAngled,
};

struct IncludeRequest {
  String name;
  IncludeKind kind{IncludeKind::Quoted};
  rstd::path::PathBuf including_path;
  Option<usize> previous_search_index;
  SourceLocation location;
};

struct IncludeResolution {
  rstd::path::PathBuf path;
  usize search_index{};
  bool system{false};
};

enum class BuiltinQueryKind {
  HasBuiltin,
  HasConstexprBuiltin,
  HasFeature,
  HasExtension,
  HasCppAttribute,
  HasAttribute,
  HasDeclspecAttribute,
  HasWarning,
  IsIdentifier,
};

enum class BuiltinTextKind {
  Date,
  Time,
};

struct BuiltinQuery {
  BuiltinQueryKind kind{BuiltinQueryKind::HasBuiltin};
  String argument;
  SourceLocation location;

  auto clone() const -> BuiltinQuery {
    return BuiltinQuery{
        .kind = kind, .argument = argument.clone(), .location = location};
  }
};

struct MacroSeed {
  String definition;
};

enum class PragmaOutcome {
  Ignored,
  Handled,
};

struct PragmaRequest {
  Vec<Token> tokens;
  SourceLocation location;
};

enum class EventKind {
  EnterFile,
  ExitFile,
  IncludeResolved,
  IncludeNotFound,
  IncludeProbeResolved,
  IncludeProbeNotFound,
  MacroDefined,
  MacroUndefined,
  MacroExpanded,
  Conditional,
  Diagnostic,
};

struct Event {
  EventKind kind{EventKind::EnterFile};
  String name;
  Option<rstd::path::PathBuf> path;
  SourceLocation location;
};

struct SourceProvider {
  template <typename Self, typename = void> struct Api {
    using Trait = SourceProvider;

    auto load(ref<rstd::path::Path> path) -> Result<SharedLexedSource> {
      return rstd::trait_call<0>(this, path);
    }
  };

  template <typename T> using Funcs = TraitFuncs<&T::load>;
};

struct IncludeResolver {
  template <typename Self, typename = void> struct Api {
    using Trait = IncludeResolver;

    auto resolve(const IncludeRequest &request)
        -> Result<Option<IncludeResolution>> {
      return rstd::trait_call<0>(this, request);
    }
  };

  template <typename T> using Funcs = TraitFuncs<&T::resolve>;
};

struct BuiltinProvider {
  template <typename Self, typename = void> struct Api {
    using Trait = BuiltinProvider;

    auto predefined_macros() -> Result<Vec<SharedMacroDefinition>> {
      return rstd::trait_call<0>(this);
    }

    auto prepare(const Vec<BuiltinQuery> &queries) -> Result<empty> {
      return rstd::trait_call<1>(this, queries);
    }

    auto evaluate(const BuiltinQuery &query) -> Result<i64> {
      return rstd::trait_call<2>(this, query);
    }

    auto text(BuiltinTextKind kind) -> Result<String> {
      return rstd::trait_call<3>(this, kind);
    }
  };

  template <typename T>
  using Funcs =
      TraitFuncs<&T::predefined_macros, &T::prepare, &T::evaluate, &T::text>;
};

struct PragmaHandler {
  template <typename Self, typename = void> struct Api {
    using Trait = PragmaHandler;

    auto handle(const PragmaRequest &request) -> Result<PragmaOutcome> {
      return rstd::trait_call<0>(this, request);
    }
  };

  template <typename T> using Funcs = TraitFuncs<&T::handle>;
};

struct PreprocessorEventSink {
  template <typename Self, typename = void> struct Api {
    using Trait = PreprocessorEventSink;

    auto on_event(const Event &event) -> Result<empty> {
      return rstd::trait_call<0>(this, event);
    }
  };

  template <typename T> using Funcs = TraitFuncs<&T::on_event>;
};

struct PreprocessedTokenConsumer {
  template <typename Self, typename = void> struct Api {
    using Trait = PreprocessedTokenConsumer;

    auto consume(Vec<Token> tokens) -> Result<empty> {
      return rstd::trait_call<0>(this, rstd::move(tokens));
    }
  };

  template <typename T> using Funcs = TraitFuncs<&T::consume>;
};

struct IgnorePragmas {
  auto handle(const PragmaRequest &) -> Result<PragmaOutcome> {
    return Ok(PragmaOutcome::Ignored);
  }
};

struct IgnoreEvents {
  auto on_event(const Event &) -> Result<empty> { return Ok(empty{}); }
};

} // namespace tenon::frontend::preprocessor
