#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito.frontend;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito::frontend::preprocessor;

template<typename T>
using PpResult = lito::frontend::preprocessor::Result<T>;

class MemorySources {
public:
    auto add(ref<str> path, ref<str> contents) -> void {
        files_.insert(String::make(path), String::make(contents));
    }

    auto contains(ref<str> path) const -> bool { return files_.contains_key(path); }

    auto load(ref<rstd::path::Path> path, SourceLoadRole role) -> PpResult<SharedScanFileStorage> {
        if (role == SourceLoadRole::Primary) {
            ++primary_loads;
        } else {
            ++include_loads;
        }
        auto text = path.to_str();
        if (text.is_none()) {
            return Err(
                lito::frontend::preprocessor::Error::make("memory source path is not UTF-8"_str));
        }
        auto contents = files_.get(*text);
        if (contents.is_none()) {
            return Err(lito::frontend::preprocessor::Error::make("memory source is missing"_str));
        }
        auto snapshot = make_source_snapshot(SourceBuffer { .path = rstd::path::PathBuf::from(path),
                                                            .contents = (**contents).clone() });
        auto source   = SourceFile { .snapshot = snapshot.clone() };
        auto lexed    = lex_scan_file(source);
        if (lexed.is_err()) return Err(rstd::move(lexed).unwrap_err());
        return Ok(rstd::sync::Arc<ScanFileStorage>::make(rstd::move(lexed).unwrap()));
    }

    usize primary_loads {};
    usize include_loads {};

private:
    rstd::collections::BTreeMap<String, String> files_;
};

class MemoryIncludes {
public:
    explicit MemoryIncludes(const MemorySources& sources): sources_(sources) {}

    auto resolve(const IncludeRequest& request) -> PpResult<Option<IncludeResolution>> {
        if (request.kind == IncludeKind::NextQuoted || request.kind == IncludeKind::NextAngled) {
            ++next_queries;
        }
        auto path = rstd::path::PathBuf::from("/"_str).join(
            rstd::path::PathBuf::from(request.name.as_str()).as_path());
        auto text = path.as_path().to_str();
        if (text.is_none() || ! sources_.contains(*text)) {
            return Ok(None());
        }
        return Ok(Some(IncludeResolution { .path = rstd::move(path) }));
    }

    usize next_queries {};

private:
    const MemorySources& sources_;
};

class TestBuiltins {
public:
    auto predefined_macros() -> PpResult<Vec<PredefinedMacroOperation>> {
        return Ok(Vec<PredefinedMacroOperation>::make());
    }

    auto evaluate(const BuiltinQueryKey& query) -> PpResult<i64> {
        ++query_count;
        if (query.is<HasBuiltinQuery>() && query.argument.as_str() == "__builtin_assume"_str) {
            ++typed_queries;
        }
        return Ok(i64(1));
    }

    auto text(BuiltinTextKind kind) -> PpResult<String> {
        ++text_queries;
        return Ok(String::make(kind == BuiltinTextKind::Date ? "Aug  1 2026"_str : "00:00:00"_str));
    }

    usize text_queries {};
    usize query_count {};
    usize typed_queries {};
};

auto external_object_macro(ref<str> name, TokenKind kind, ref<str> replacement)
    -> SharedMacroDefinition {
    auto token  = Token {};
    token.kind  = kind;
    token.text  = String::make(replacement);
    auto tokens = Vec<Token>::make();
    tokens.push(rstd::move(token));
    auto macro = MacroDefinition {};
    macro.set_name(String::make(name));
    macro.set_replacement(rstd::move(tokens));
    return share_macro_definition(rstd::move(macro));
}

class TestExternalMacros {
public:
    auto resolve(ref<str> name, SourceLocation location)
        -> PpResult<Option<ExternalMacroResolution>> {
        if (name == "LITO_PKG_VERSION"_str) {
            ++version_queries;
            return Ok(Some(ExternalMacroResolution {
                .dependency_key      = String::make("package.version"_str),
                .value_identity      = String::make("1.2.3"_str),
                .state               = lito::frontend::ExternalMacroState::Defined,
                .compiler_definition = Some(String::make("LITO_PKG_VERSION=\"1.2.3\""_str)),
                .definition =
                    Some(external_object_macro(name, TokenKind::StringLiteral, "\"1.2.3\""_str)),
            }));
        }
        if (name == "LITO_FEAT_ON"_str) {
            ++enabled_queries;
            return Ok(Some(ExternalMacroResolution {
                .dependency_key      = String::make("package.feature:on"_str),
                .value_identity      = String::make("on"_str),
                .state               = lito::frontend::ExternalMacroState::Defined,
                .compiler_definition = Some(String::make("LITO_FEAT_ON=1"_str)),
                .definition = Some(external_object_macro(name, TokenKind::PpNumber, "1"_str)),
            }));
        }
        if (name == "LITO_FEAT_OFF"_str) {
            ++disabled_queries;
            return Ok(Some(ExternalMacroResolution {
                .dependency_key = String::make("package.feature:off"_str),
                .value_identity = String::make("off"_str),
                .state          = lito::frontend::ExternalMacroState::Undefined,
            }));
        }
        if (name.starts_with("LITO_FEAT_"_str)) {
            return Err(lito::frontend::lexical::Error::at(
                rstd::format("unknown package feature macro '{}'", name), location));
        }
        return Ok(None());
    }

    usize version_queries {};
    usize enabled_queries {};
    usize disabled_queries {};
};

class TestEvents {
public:
    auto wants(EventKind kind) const -> bool {
        return kind == EventKind::IncludeResolved || kind == EventKind::IncludeProbeResolved;
    }

    auto on_event(const Event& event) -> PpResult<empty> {
        if (event.kind == EventKind::IncludeResolved) ++includes;
        if (event.kind == EventKind::IncludeProbeResolved) ++probes;
        if (event.kind != EventKind::IncludeResolved &&
            event.kind != EventKind::IncludeProbeResolved) {
            ++unexpected;
        }
        return Ok(empty {});
    }

    usize includes {};
    usize probes {};
    usize unexpected {};
};

auto contains_sequence(const Vec<Token>& tokens, ref<str> first, ref<str> second) -> bool {
    for (auto index = usize {}; index + usize(1) < tokens.len(); ++index) {
        if (tokens[index].text.as_str() == first &&
            tokens[index + usize(1)].text.as_str() == second) {
            return true;
        }
    }
    return false;
}

auto contains_token(const Vec<Token>& tokens, ref<str> value) -> bool {
    for (const auto& token : tokens) {
        if (token.text.as_str() == value) return true;
    }
    return false;
}

auto contains_sequence(const Vec<Token>& tokens, ref<str> first, ref<str> second, ref<str> third)
    -> bool {
    for (auto index = usize {}; index + usize(2) < tokens.len(); ++index) {
        if (tokens[index].text.as_str() == first &&
            tokens[index + usize(1)].text.as_str() == second &&
            tokens[index + usize(2)].text.as_str() == third) {
            return true;
        }
    }
    return false;
}

auto run_preprocessor_test() -> int {
    auto sources = MemorySources {};
    sources.add("/config.hpp"_str, "#pragma once\n#define LITO_VALUE 7\n"_str);
    sources.add("/effects.hpp"_str,
                "#define LITO_HEADER_COUNTER __COUNTER__\n"
                "LITO_HEADER_COUNTER\n"
                "#define LITO_HEADER_PRAGMA(value) _Pragma(#value)\n"
                "#define LITO_HEADER_STACK 1\n"
                "LITO_HEADER_PRAGMA(push_macro(\"LITO_HEADER_STACK\"))\n"
                "#undef LITO_HEADER_STACK\n"
                "#define LITO_HEADER_STACK 0\n"
                "LITO_HEADER_PRAGMA(pop_macro(\"LITO_HEADER_STACK\"))\n"
                "__DATE__\n"_str);
    sources.add("/main.cppm"_str,
                "#include \"config.hpp\"\n"
                "#include \"config.hpp\"\n"
                "#include \"effects.hpp\"\n"
                "#define LITO_PRAGMA(value) _Pragma(#value)\n"
                "#define LITO_STACK 1\n"
                "LITO_PRAGMA(push_macro(\"LITO_STACK\"))\n"
                "#undef LITO_STACK\n"
                "#define LITO_STACK 0\n"
                "LITO_PRAGMA(pop_macro(\"LITO_STACK\"))\n"
                "#define LITO_MODULE module\n"
                "#define LITO_IMPORT import\n"
                "#if 0\n"
                "/// hidden documentation\n"
                "#endif\n"
                "#if 0L\n"
                "#error zero with an integer suffix must remain false\n"
                "#endif\n"
                "#if LITO_VALUE == 7 && LITO_STACK == 1 && LITO_HEADER_STACK == 1 && "
                "__COUNTER__ == 1 && __has_include(\"config.hpp\") && "
                "__has_include_next(<config.hpp>) && "
                "__has_builtin(__builtin_assume) && "
                "!__building_module(_Builtin_stddef)\n"
                "/// active documentation\n"
                "export LITO_MODULE fixture.memory;\n"
                "export LITO_IMPORT :dependency;\n"
                "#endif\n"
                "#define LITO_DUP(value) value + value\n"
                "LITO_DUP(__COUNTER__)\n"
                "#define LITO_CAT_INNER(left, right) left ## right\n"
                "#define LITO_CAT(left, right) LITO_CAT_INNER(left, right)\n"
                "#define LITO_PREFIX LITO_\n"
                "#define LITO_JOINED 9\n"
                "LITO_CAT(LITO_PREFIX, JOINED);\n"
                "#define LITO_MULTILINE_CAT(left, right) left ## right\n"
                "LITO_MULTILINE_CAT(LITO_,\n"
                "JOINED);\n"
                "#define LITO_TOKEN(value) value\n"
                "#define LITO_EMPTY_PASTE(value, empty) LITO_TOKEN(empty ## value)\n"
                "LITO_EMPTY_PASTE(LITO_JOINED,);\n"
                "#define LITO_STRINGIFY(value) #value\n"
                "LITO_STRINGIFY(alpha   beta);\n"
                "#define LITO_OPTIONAL(prefix, ...) prefix __VA_OPT__(+ __VA_ARGS__)\n"
                "LITO_OPTIONAL(LITO_EMPTY_OPTIONAL);\n"
                "LITO_OPTIONAL(LITO_FULL_OPTIONAL, 31);\n"
                "#define LITO_NESTED(...) "
                "__VA_OPT__(LITO_NESTED_OUTER __VA_OPT__(LITO_NESTED_INNER __VA_ARGS__))\n"
                "LITO_NESTED();\n"
                "LITO_NESTED(41);\n"
                "#define LITO_VA_SUFFIX(root, ...) root ## __VA_OPT__(__VA_ARGS__)\n"
                "LITO_VA_SUFFIX(LITO_, JOINED);\n"
                "LITO_VA_SUFFIX(LITO_EMPTY_SUFFIX);\n"
                "#define LITO_GNU(prefix, ...) prefix, ## __VA_ARGS__\n"
                "LITO_GNU(LITO_GNU_EMPTY);\n"
                "LITO_GNU(LITO_GNU_FULL, 43);\n"
                "LITO_LATE\n"
                "#define LITO_LATE 42\n"
                "LITO_LATE\n"
                "#define __$lito_extension(value) value\n"
                "__$lito_extension(17)\n"
                "#define LITO_REDEFINED 19\n"
                "#define LITO_REDEFINED 23\n"
                "LITO_REDEFINED\n"
                "#line 200 \"virtual.cpp\"\n"
                "LITO_LINE __LINE__\n"
                "LITO_FILE __FILE__\n"
                "#line 300\n"
                "LITO_INHERITED_LINE __LINE__\n"
                "LITO_INHERITED_FILE __FILE__\n"_str);
    auto includes    = MemoryIncludes(sources);
    auto builtins    = TestBuiltins {};
    auto identifiers = lito::frontend::lexical::TokenKindMatcher { TokenKind::Identifier };
    auto pragmas     = IgnorePragmas {};
    auto events      = TestEvents {};
    auto result      = preprocess(
        PreprocessRequest {
            .source               = rstd::path::PathBuf::from("/main.cppm"_str),
            .environment_identity = String::make("memory-v1"_str),
        },
        sources,
        includes,
        builtins,
        identifiers,
        pragmas,
        events);
    if (result.is_err()) return 1;
    if (sources.primary_loads != usize(1) || sources.include_loads != usize(2)) return 28;
    if (! contains_sequence(result->tokens, "module"_str, "fixture"_str)) return 2;
    if (! contains_sequence(result->tokens, "import"_str, ":"_str)) return 3;
    if (result->sources.len() != usize(3)) return 4;
    if (events.includes != usize(3) || events.probes != usize(2)) return 5;
    if (includes.next_queries != usize(1)) return 16;
    if (events.unexpected != usize {}) return 13;
    if (builtins.text_queries != usize(1)) return 6;
    if (builtins.query_count != usize(1) || builtins.typed_queries != usize(1)) return 15;
    if (! contains_sequence(result->tokens, "2"_str, "+"_str, "2"_str)) return 7;
    auto joined = usize {};
    for (const auto& token : result->tokens) {
        if (token.text.as_str() == "9"_str) ++joined;
    }
    if (joined != usize(4)) return 11;
    if (! contains_token(result->tokens, "\"alpha beta\""_str)) return 19;
    if (! contains_sequence(result->tokens, "LITO_FULL_OPTIONAL"_str, "+"_str, "31"_str)) {
        return 20;
    }
    if (! contains_token(result->tokens, "LITO_EMPTY_OPTIONAL"_str)) return 21;
    if (! contains_sequence(
            result->tokens, "LITO_NESTED_OUTER"_str, "LITO_NESTED_INNER"_str, "41"_str)) {
        return 22;
    }
    if (! contains_token(result->tokens, "LITO_EMPTY_SUFFIX"_str)) return 23;
    if (contains_sequence(result->tokens, "LITO_GNU_EMPTY"_str, ","_str)) return 24;
    if (! contains_sequence(result->tokens, "LITO_GNU_FULL"_str, ","_str, "43"_str)) return 25;
    const auto& statistics = result->statistics;
    if (statistics.token_clones !=
        statistics.source_token_materializations + statistics.macro_literal_clones +
            statistics.macro_raw_argument_clones + statistics.macro_expansion_input_clones +
            statistics.macro_expanded_argument_reuse_clones + statistics.other_token_clones) {
        return 26;
    }
    if (statistics.macro_definitions == usize {} || statistics.macro_operations == usize {} ||
        statistics.macro_stringifications < usize(3) || statistics.macro_token_pastes < usize(4) ||
        statistics.macro_va_opt_uses < usize(5)) {
        return 27;
    }
    if (! contains_token(result->tokens, "LITO_LATE"_str) ||
        ! contains_token(result->tokens, "42"_str)) {
        return 12;
    }
    if (! contains_token(result->tokens, "17"_str)) return 17;
    if (! contains_token(result->tokens, "23"_str)) return 18;
    if (! contains_sequence(result->tokens, "LITO_LINE"_str, "200"_str)) return 29;
    if (! contains_sequence(result->tokens, "LITO_FILE"_str, "\"virtual.cpp\""_str)) return 30;
    if (! contains_sequence(result->tokens, "LITO_INHERITED_LINE"_str, "300"_str)) return 31;
    if (! contains_sequence(result->tokens, "LITO_INHERITED_FILE"_str, "\"virtual.cpp\""_str)) {
        return 32;
    }
    if (result->active_comments.len() != usize(1) ||
        result->active_comments[usize {}].kind != CommentKind::OuterDocumentation ||
        ! result->active_comments[usize {}].text.as_str().contains("active documentation"_str)) {
        return 14;
    }

    auto stream_builtins = TestBuiltins {};
    auto stream_pragmas  = IgnorePragmas {};
    auto stream_events   = TestEvents {};
    auto consumer        = lito::frontend::parser::ModuleDependencyConsumer::make();
    auto streamed        = preprocess_to(
        PreprocessRequest {
            .source               = rstd::path::PathBuf::from("/main.cppm"_str),
            .environment_identity = String::make("memory-v1"_str),
        },
        sources,
        includes,
        stream_builtins,
        identifiers,
        stream_pragmas,
        stream_events,
        consumer);
    if (streamed.is_err()) return 8;
    auto facts = consumer.finish(*streamed);
    if (facts.is_err() || facts->provided.is_none() ||
        facts->provided->logical_name.as_str() != "fixture.memory"_str) {
        return 9;
    }
    if (facts->imports.len() != usize(1) ||
        facts->imports[usize {}].logical_name.as_str() != "fixture.memory:dependency"_str ||
        ! facts->imports[usize {}].exported) {
        return 10;
    }
    return 0;
}

TEST(Preprocessor, Core) {
    EXPECT_EQ(run_preprocessor_test(), 0);
}

TEST(PreprocessorMacro, OwnsParsedSourceAndCompilesCommandLineReplacement) {
    auto retained = []() -> Option<SharedMacroDefinition> {
        auto parsed = parse_macro_source(SourceBuffer {
            .path     = rstd::path::PathBuf::from("/predefined.txt"_str),
            .contents = String::make("#define LITO_RETAINED(value) value + 17\n"_str),
        });
        if (parsed.is_err() || parsed->definitions.len() != usize(1)) return None();
        return Some(parsed->definitions[usize {}].clone());
    }();
    ASSERT_TRUE(retained.is_some());
    ASSERT_EQ((**retained).replacement.len(), usize(3));
    EXPECT_EQ((**retained).replacement[usize(2)].text.as_str(), "17"_str);
    EXPECT_EQ((**retained).operations().len(), usize(3));

    auto function = parse_command_line_macro_definition(
        "LITO_COMMAND(value, ...)=value __VA_OPT__(+ __VA_ARGS__)"_str);
    ASSERT_TRUE(function.is_ok());
    ASSERT_TRUE(function->parameters.is_some());
    EXPECT_EQ(function->parameters->len(), usize(1));
    EXPECT_TRUE(function->variadic);
    EXPECT_EQ(function->operations().len(), usize(4));

    auto empty = parse_command_line_macro_definition("LITO_EMPTY="_str);
    ASSERT_TRUE(empty.is_ok());
    EXPECT_TRUE(empty->replacement.is_empty());
    auto defaulted = parse_command_line_macro_definition("LITO_DEFAULT"_str);
    ASSERT_TRUE(defaulted.is_ok());
    ASSERT_EQ(defaulted->replacement.len(), usize(1));
    EXPECT_EQ(defaulted->replacement[usize {}].text.as_str(), "1"_str);
}

TEST(Preprocessor, LookupCandidateTreatsRegularAncestorAsAbsent) {
    auto directory = rstd::fs::TempDir::make("lito-frontend-lookup"_str);
    ASSERT_TRUE(directory.is_ok());
    auto ancestor = rstd::path::PathBuf::from(directory->path())
                        .join(rstd::path::PathBuf::from("QtCore"_str).as_path());
    ASSERT_TRUE(rstd::fs::write(ancestor.as_path(), "aggregate header"_str.as_bytes()).is_ok());
    auto candidate = ancestor.join(rstd::path::PathBuf::from("qtcoreglobal.h"_str).as_path());

    auto exists = lito::frontend::lookup_candidate_exists(candidate.as_path());
    ASSERT_TRUE(exists.is_ok());
    EXPECT_FALSE(*exists);
}

TEST(Preprocessor, LazilyMaterializesExternalMacros) {
    auto sources = MemorySources {};
    sources.add("/metadata.hpp"_str,
                "#if defined(LITO_PKG_VERSION)\n"
                "LITO_PKG_VERSION\n"
                "#endif\n"_str);
    sources.add("/main.cpp"_str,
                "#include \"metadata.hpp\"\n"
                "#if defined LITO_FEAT_ON && !defined(LITO_FEAT_OFF)\n"
                "LITO_PKG_VERSION\n"
                "#else\n"
                "#error external macro state mismatch\n"
                "#endif\n"
                "#if 0\n"
                "LITO_FEAT_NEVER\n"
                "#endif\n"
                "#undef LITO_FEAT_ON\n"
                "#ifdef LITO_FEAT_ON\n"
                "#error external macro undef failed\n"
                "#endif\n"
                "#define LITO_FEAT_OFF 7\n"
                "#if LITO_FEAT_OFF != 7\n"
                "#error external macro redefine failed\n"
                "#endif\n"_str);
    auto includes    = MemoryIncludes(sources);
    auto builtins    = TestBuiltins {};
    auto externals   = TestExternalMacros {};
    auto identifiers = lito::frontend::lexical::TokenKindMatcher { TokenKind::Identifier };
    auto pragmas     = IgnorePragmas {};
    auto events      = TestEvents {};
    auto result      = preprocess(
        PreprocessRequest {
            .source               = rstd::path::PathBuf::from("/main.cpp"_str),
            .environment_identity = String::make("external-v1"_str),
        },
        sources,
        includes,
        builtins,
        externals,
        identifiers,
        pragmas,
        events);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(externals.version_queries, usize(1));
    EXPECT_EQ(externals.enabled_queries, usize(1));
    EXPECT_EQ(externals.disabled_queries, usize(1));
    ASSERT_EQ(result->external_macros.len(), usize(3));
    EXPECT_EQ(result->external_macros[usize {}].name, "LITO_PKG_VERSION"_str);
    EXPECT_EQ(result->external_macros[usize(1)].name, "LITO_FEAT_ON"_str);
    EXPECT_EQ(result->external_macros[usize(2)].name, "LITO_FEAT_OFF"_str);
    EXPECT_EQ(result->external_macros[usize(2)].state,
              lito::frontend::ExternalMacroState::Undefined);
    EXPECT_TRUE(result->external_macros[usize(2)].compiler_definition.is_none());

    auto invalid_sources = MemorySources {};
    invalid_sources.add("/invalid.cpp"_str,
                        "#ifdef LITO_FEAT_TYPO\n"
                        "#endif\n"_str);
    auto invalid_includes  = MemoryIncludes(invalid_sources);
    auto invalid_builtins  = TestBuiltins {};
    auto invalid_externals = TestExternalMacros {};
    auto invalid_pragmas   = IgnorePragmas {};
    auto invalid_events    = TestEvents {};
    auto invalid           = preprocess(
        PreprocessRequest {
            .source               = rstd::path::PathBuf::from("/invalid.cpp"_str),
            .environment_identity = String::make("external-v1"_str),
        },
        invalid_sources,
        invalid_includes,
        invalid_builtins,
        invalid_externals,
        identifiers,
        invalid_pragmas,
        invalid_events);
    ASSERT_TRUE(invalid.is_err());
    EXPECT_TRUE(invalid.unwrap_err().message.as_str().contains(
        "unknown package feature macro 'LITO_FEAT_TYPO'"_str));
}

TEST(Preprocessor, ValidatesExternalMacrosInModuleNames) {
    auto sources = MemorySources {};
    sources.add("/defined.cppm"_str, "export module LITO_PKG_VERSION;\n"_str);
    auto includes    = MemoryIncludes(sources);
    auto builtins    = TestBuiltins {};
    auto externals   = TestExternalMacros {};
    auto identifiers = lito::frontend::lexical::TokenKindMatcher { TokenKind::Identifier };
    auto pragmas     = IgnorePragmas {};
    auto events      = TestEvents {};
    auto defined     = preprocess(
        PreprocessRequest {
            .source               = rstd::path::PathBuf::from("/defined.cppm"_str),
            .environment_identity = String::make("external-v1"_str),
        },
        sources,
        includes,
        builtins,
        externals,
        identifiers,
        pragmas,
        events);
    ASSERT_TRUE(defined.is_err());
    EXPECT_TRUE(defined.unwrap_err().message.as_str().contains(
        "module name identifier 'LITO_PKG_VERSION' is defined as an object-like macro"_str));

    auto disabled_sources = MemorySources {};
    disabled_sources.add("/disabled.cppm"_str, "export module LITO_FEAT_OFF;\n"_str);
    auto disabled_includes  = MemoryIncludes(disabled_sources);
    auto disabled_builtins  = TestBuiltins {};
    auto disabled_externals = TestExternalMacros {};
    auto disabled_pragmas   = IgnorePragmas {};
    auto disabled_events    = TestEvents {};
    auto disabled           = preprocess(
        PreprocessRequest {
            .source               = rstd::path::PathBuf::from("/disabled.cppm"_str),
            .environment_identity = String::make("external-v1"_str),
        },
        disabled_sources,
        disabled_includes,
        disabled_builtins,
        disabled_externals,
        identifiers,
        disabled_pragmas,
        disabled_events);
    ASSERT_TRUE(disabled.is_ok());
    EXPECT_TRUE(contains_sequence(disabled->tokens, "module"_str, "LITO_FEAT_OFF"_str));
    ASSERT_EQ(disabled->external_macros.len(), usize(1));
    EXPECT_EQ(disabled->external_macros[usize {}].state,
              lito::frontend::ExternalMacroState::Undefined);
}

TEST(BuiltinQuery, TypedDefinition) {
    static_assert(HasBuiltinQuery::name == "__has_builtin"_str);
    static_assert(HasBuiltinQuery::Handler::form == BuiltinQueryArgumentForm::Tokens);
    static_assert(HasWarningQuery::Handler::form == BuiltinQueryArgumentForm::StringLiteral);
    static_assert(DynamicBuiltinSet::contains("__has_builtin"_str));
    static_assert(DynamicBuiltinSet::contains("__building_module"_str));
    static_assert(DynamicBuiltinSet::contains("__DATE__"_str));
    static_assert(! DynamicBuiltinSet::contains("__not_a_builtin"_str));

    auto attribute = BuiltinQueryKey::make<HasCppAttributeQuery>("__gnu__::__cold__"_str);
    EXPECT_TRUE(attribute.is<HasCppAttributeQuery>());
    EXPECT_EQ(attribute.name(), HasCppAttributeQuery::name);
    EXPECT_EQ(attribute.argument.as_str(), "gnu::cold"_str);

    auto warning = BuiltinQueryKey::make<HasWarningQuery>("-Winvalid-specialization"_str);
    EXPECT_EQ(warning.render_argument().as_str(), "\"-Winvalid-specialization\""_str);
}
