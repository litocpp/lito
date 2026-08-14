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

    auto load(ref<rstd::path::Path> path) -> PpResult<SharedLexedSource> {
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
        auto lexed    = lex_with_comments(source, true);
        if (lexed.is_err()) return Err(rstd::move(lexed).unwrap_err());
        auto file = rstd::move(lexed).unwrap();
        return Ok(rstd::sync::Arc<LexedSource>::make(LexedSource {
            .snapshot = rstd::move(snapshot),
            .tokens   = rstd::move(file.tokens),
            .comments = rstd::move(file.comments),
        }));
    }

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
                "LITO_LATE\n"
                "#define LITO_LATE 42\n"
                "LITO_LATE\n"_str);
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
    if (! contains_sequence(result->tokens, "module"_str, "fixture"_str)) return 2;
    if (! contains_sequence(result->tokens, "import"_str, ":"_str)) return 3;
    if (result->sources.len() != usize(3)) return 4;
    if (events.includes != usize(3) || events.probes != usize(2)) return 5;
    if (includes.next_queries != usize(1)) return 16;
    if (events.unexpected != usize {}) return 13;
    if (builtins.text_queries != usize(1)) return 6;
    if (builtins.query_count != usize(1) || builtins.typed_queries != usize(1)) return 15;
    if (! contains_sequence(result->tokens, "2"_str, "+"_str, "2"_str)) return 7;
    if (! contains_sequence(result->tokens, "9"_str, ";"_str)) return 11;
    if (! contains_token(result->tokens, "LITO_LATE"_str) ||
        ! contains_token(result->tokens, "42"_str)) {
        return 12;
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
