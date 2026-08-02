export module tenon.frontend.preprocessor:traits;

import rstd;
import tenon.frontend.lexical;
import :macro;

using namespace rstd::prelude;
using namespace rstd::literals;

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
  IsTargetArch,
  IsTargetVendor,
  IsTargetOs,
  IsTargetEnvironment,
  IsTargetVariantOs,
  IsTargetVariantEnvironment,
};

enum class BuiltinTextKind {
  Date,
  Time,
};

struct BuiltinQueryKey {
  BuiltinQueryKind kind{BuiltinQueryKind::HasBuiltin};
  String argument;

  auto clone() const -> BuiltinQueryKey {
    return BuiltinQueryKey{.kind = kind, .argument = argument.clone()};
  }
};

auto builtin_query_kind(ref<str> name) -> Option<BuiltinQueryKind> {
  if (name == "__has_builtin"_str)
    return Some(BuiltinQueryKind::HasBuiltin);
  if (name == "__has_constexpr_builtin"_str)
    return Some(BuiltinQueryKind::HasConstexprBuiltin);
  if (name == "__has_feature"_str)
    return Some(BuiltinQueryKind::HasFeature);
  if (name == "__has_extension"_str)
    return Some(BuiltinQueryKind::HasExtension);
  if (name == "__has_cpp_attribute"_str)
    return Some(BuiltinQueryKind::HasCppAttribute);
  if (name == "__has_attribute"_str)
    return Some(BuiltinQueryKind::HasAttribute);
  if (name == "__has_declspec_attribute"_str)
    return Some(BuiltinQueryKind::HasDeclspecAttribute);
  if (name == "__has_warning"_str)
    return Some(BuiltinQueryKind::HasWarning);
  if (name == "__is_target_arch"_str)
    return Some(BuiltinQueryKind::IsTargetArch);
  if (name == "__is_target_vendor"_str)
    return Some(BuiltinQueryKind::IsTargetVendor);
  if (name == "__is_target_os"_str)
    return Some(BuiltinQueryKind::IsTargetOs);
  if (name == "__is_target_environment"_str)
    return Some(BuiltinQueryKind::IsTargetEnvironment);
  if (name == "__is_target_variant_os"_str)
    return Some(BuiltinQueryKind::IsTargetVariantOs);
  if (name == "__is_target_variant_environment"_str)
    return Some(BuiltinQueryKind::IsTargetVariantEnvironment);
  return None();
}

auto builtin_query_name(BuiltinQueryKind kind) -> ref<str> {
  switch (kind) {
  case BuiltinQueryKind::HasBuiltin:
    return "__has_builtin"_str;
  case BuiltinQueryKind::HasConstexprBuiltin:
    return "__has_constexpr_builtin"_str;
  case BuiltinQueryKind::HasFeature:
    return "__has_feature"_str;
  case BuiltinQueryKind::HasExtension:
    return "__has_extension"_str;
  case BuiltinQueryKind::HasCppAttribute:
    return "__has_cpp_attribute"_str;
  case BuiltinQueryKind::HasAttribute:
    return "__has_attribute"_str;
  case BuiltinQueryKind::HasDeclspecAttribute:
    return "__has_declspec_attribute"_str;
  case BuiltinQueryKind::HasWarning:
    return "__has_warning"_str;
  case BuiltinQueryKind::IsTargetArch:
    return "__is_target_arch"_str;
  case BuiltinQueryKind::IsTargetVendor:
    return "__is_target_vendor"_str;
  case BuiltinQueryKind::IsTargetOs:
    return "__is_target_os"_str;
  case BuiltinQueryKind::IsTargetEnvironment:
    return "__is_target_environment"_str;
  case BuiltinQueryKind::IsTargetVariantOs:
    return "__is_target_variant_os"_str;
  case BuiltinQueryKind::IsTargetVariantEnvironment:
    return "__is_target_variant_environment"_str;
  }
  __builtin_unreachable();
}

auto normalize_attribute_component(ref<str> value) -> ref<str> {
  if (value.len() >= usize(4) && value.starts_with("__"_str) &&
      value.ends_with("__"_str)) {
    auto inner = value.get(usize(2), value.len() - usize(2));
    if (inner.is_some())
      return *inner;
  }
  return value;
}

auto normalize_builtin_query_argument(BuiltinQueryKind kind, ref<str> value)
    -> String {
  if (kind == BuiltinQueryKind::HasAttribute ||
      kind == BuiltinQueryKind::HasDeclspecAttribute) {
    return String::make(normalize_attribute_component(value));
  }
  if (kind == BuiltinQueryKind::HasCppAttribute) {
    auto separator = value.split_once("::"_str);
    if (separator.is_none())
      return String::make(normalize_attribute_component(value));
    auto result = String::make(
        normalize_attribute_component(separator->template get<0>()));
    result.push_str("::"_str);
    result.push_str(
        normalize_attribute_component(separator->template get<1>()));
    return result;
  }
  if (kind == BuiltinQueryKind::IsTargetArch ||
      kind == BuiltinQueryKind::IsTargetVendor ||
      kind == BuiltinQueryKind::IsTargetOs ||
      kind == BuiltinQueryKind::IsTargetEnvironment ||
      kind == BuiltinQueryKind::IsTargetVariantOs ||
      kind == BuiltinQueryKind::IsTargetVariantEnvironment) {
    auto result = String::make();
    for (auto character : value) {
      if (character >= u8('A') && character <= u8('Z'))
        character = u8(character.to_primitive() + u8('a').to_primitive() -
                       u8('A').to_primitive());
      result.push_ascii(character);
    }
    return result;
  }
  return String::make(value);
}

struct MacroSeed {
  String definition;
};

enum class PredefinedMacroOperationKind {
  Define,
  Undefine,
};

struct PredefinedMacroOperation {
  PredefinedMacroOperationKind kind{PredefinedMacroOperationKind::Define};
  String name;
  Option<SharedMacroDefinition> definition;

  static auto define(SharedMacroDefinition value)
      -> PredefinedMacroOperation {
    return PredefinedMacroOperation{
        .kind = PredefinedMacroOperationKind::Define,
        .definition = Some(rstd::move(value)),
    };
  }

  static auto undefine(String name) -> PredefinedMacroOperation {
    return PredefinedMacroOperation{
        .kind = PredefinedMacroOperationKind::Undefine,
        .name = rstd::move(name),
    };
  }
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

    auto predefined_macros() -> Result<Vec<PredefinedMacroOperation>> {
      return rstd::trait_call<0>(this);
    }

    auto evaluate(const BuiltinQueryKey &query) -> Result<i64> {
      return rstd::trait_call<1>(this, query);
    }

    auto text(BuiltinTextKind kind) -> Result<String> {
      return rstd::trait_call<2>(this, kind);
    }
  };

  template <typename T>
  using Funcs = TraitFuncs<&T::predefined_macros, &T::evaluate, &T::text>;
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

    auto wants(EventKind kind) const -> bool {
      return rstd::trait_call<0>(this, kind);
    }

    auto on_event(const Event &event) -> Result<empty> {
      return rstd::trait_call<1>(this, event);
    }
  };

  template <typename T> using Funcs = TraitFuncs<&T::wants, &T::on_event>;
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

enum class PreprocessorActivity {
  PredefinedMacros,
  TranslationUnit,
};

struct PreprocessorStatistics {
  usize files{};
  usize source_tokens{};
  usize token_clones{};
  usize synthetic_tokens{};
  usize directives{};
  usize conditionals{};
  usize macro_lookups{};
  usize macro_lookup_hits{};
  usize macro_expansions{};
  usize include_attempts{};
  usize include_hits{};
  usize consumer_batches{};
  usize consumer_tokens{};

  auto add(const PreprocessorStatistics &other) noexcept -> void {
    files += other.files;
    source_tokens += other.source_tokens;
    token_clones += other.token_clones;
    synthetic_tokens += other.synthetic_tokens;
    directives += other.directives;
    conditionals += other.conditionals;
    macro_lookups += other.macro_lookups;
    macro_lookup_hits += other.macro_lookup_hits;
    macro_expansions += other.macro_expansions;
    include_attempts += other.include_attempts;
    include_hits += other.include_hits;
    consumer_batches += other.consumer_batches;
    consumer_tokens += other.consumer_tokens;
  }
};

struct PreprocessorObserver {
  template <typename Self, typename = void> struct Api {
    using Trait = PreprocessorObserver;

    auto begin(PreprocessorActivity activity) -> void {
      rstd::trait_call<0>(this, activity);
    }

    auto end(PreprocessorActivity activity) -> void {
      rstd::trait_call<1>(this, activity);
    }

    auto record(const PreprocessorStatistics &statistics) -> void {
      rstd::trait_call<2>(this, statistics);
    }
  };

  template <typename T>
  using Funcs = TraitFuncs<&T::begin, &T::end, &T::record>;
};

struct IgnorePreprocessorObserver {
  auto begin(PreprocessorActivity) -> void {}
  auto end(PreprocessorActivity) -> void {}
  auto record(const PreprocessorStatistics &) -> void {}
};

struct IgnorePragmas {
  auto handle(const PragmaRequest &) -> Result<PragmaOutcome> {
    return Ok(PragmaOutcome::Ignored);
  }
};

struct IgnoreEvents {
  auto wants(EventKind) const -> bool { return false; }
  auto on_event(const Event &) -> Result<empty> { return Ok(empty{}); }
};

} // namespace tenon::frontend::preprocessor
