import rstd;
import tenon.frontend;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace tenon::frontend::preprocessor;

template<typename T>
using PpResult = tenon::frontend::preprocessor::Result<T>;

class MemorySources {
public:
    auto add(ref<str> path, ref<str> contents) -> void {
        files_.insert(String::make(path), String::make(contents));
    }

    auto contains(ref<str> path) const -> bool { return files_.contains_key(path); }

    auto load(ref<rstd::path::Path> path) -> PpResult<SharedLexedSource> {
        auto text = path.to_str();
        if (text.is_none()) {
            return Err(tenon::frontend::preprocessor::Error::make(
                "memory source path is not UTF-8"_str));
        }
        auto contents = files_.get(*text);
        if (contents.is_none()) {
            return Err(tenon::frontend::preprocessor::Error::make(
                "memory source is missing"_str));
        }
        auto snapshot = make_source_snapshot(SourceBuffer {
            .path = rstd::path::PathBuf::from(path), .contents = (**contents).clone()
        });
        auto source = SourceFile { .snapshot = snapshot.clone() };
        auto tokens = lex(source, true);
        if (tokens.is_err()) return Err(rstd::move(tokens).unwrap_err());
        return Ok(rstd::rc::make_rc<LexedSource>(LexedSource {
                     .snapshot = rstd::move(snapshot),
                     .tokens = rstd::move(tokens).unwrap(),
                 })
                      .to_const());
    }

private:
    rstd::collections::BTreeMap<String, String> files_;
};

class MemoryIncludes {
public:
    explicit MemoryIncludes(const MemorySources& sources): sources_(sources) {}

    auto resolve(const IncludeRequest& request) -> PpResult<Option<IncludeResolution>> {
        auto path = rstd::path::PathBuf::from("/"_str)
                        .join(rstd::path::PathBuf::from(request.name.as_str()).as_path());
        auto text = path.as_path().to_str();
        if (text.is_none() || ! sources_.contains(*text)) {
            return Ok(None());
        }
        return Ok(Some(IncludeResolution { .path = rstd::move(path) }));
    }

private:
    const MemorySources& sources_;
};

class TestBuiltins {
public:
    auto predefined_macros() -> PpResult<Vec<SharedMacroDefinition>> {
        return Ok(Vec<SharedMacroDefinition>::make());
    }

    auto prepare(const Vec<BuiltinQuery>&) -> PpResult<empty> { return Ok(empty {}); }

    auto evaluate(const BuiltinQuery&) -> PpResult<i64> { return Ok(i64(1)); }

    auto text(BuiltinTextKind kind) -> PpResult<String> {
        ++text_queries;
        return Ok(String::make(kind == BuiltinTextKind::Date ? "Aug  1 2026"_str
                                                             : "00:00:00"_str));
    }

    usize text_queries {};
};

class TestEvents {
public:
    auto on_event(const Event& event) -> PpResult<empty> {
        if (event.kind == EventKind::IncludeResolved) ++includes;
        if (event.kind == EventKind::IncludeProbeResolved) ++probes;
        return Ok(empty {});
    }

    usize includes {};
    usize probes {};
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

auto contains_sequence(const Vec<Token>& tokens,
                       ref<str> first,
                       ref<str> second,
                       ref<str> third) -> bool {
    for (auto index = usize {}; index + usize(2) < tokens.len(); ++index) {
        if (tokens[index].text.as_str() == first &&
            tokens[index + usize(1)].text.as_str() == second &&
            tokens[index + usize(2)].text.as_str() == third) {
            return true;
        }
    }
    return false;
}

auto main() -> int {
    auto sources = MemorySources {};
    sources.add("/config.hpp"_str,
                "#pragma once\n#define TENON_VALUE 7\n"_str);
    sources.add(
        "/effects.hpp"_str,
        "#define TENON_HEADER_COUNTER __COUNTER__\n"
        "TENON_HEADER_COUNTER\n"
        "#define TENON_HEADER_PRAGMA(value) _Pragma(#value)\n"
        "#define TENON_HEADER_STACK 1\n"
        "TENON_HEADER_PRAGMA(push_macro(\"TENON_HEADER_STACK\"))\n"
        "#undef TENON_HEADER_STACK\n"
        "#define TENON_HEADER_STACK 0\n"
        "TENON_HEADER_PRAGMA(pop_macro(\"TENON_HEADER_STACK\"))\n"
        "__DATE__\n"_str);
    sources.add(
        "/main.cppm"_str,
        "#include \"config.hpp\"\n"
        "#include \"config.hpp\"\n"
        "#include \"effects.hpp\"\n"
        "#define TENON_PRAGMA(value) _Pragma(#value)\n"
        "#define TENON_STACK 1\n"
        "TENON_PRAGMA(push_macro(\"TENON_STACK\"))\n"
        "#undef TENON_STACK\n"
        "#define TENON_STACK 0\n"
        "TENON_PRAGMA(pop_macro(\"TENON_STACK\"))\n"
        "#define TENON_MODULE module\n"
        "#define TENON_IMPORT import\n"
        "#if TENON_VALUE == 7 && TENON_STACK == 1 && TENON_HEADER_STACK == 1 && "
        "__COUNTER__ == 1 && __has_include(\"config.hpp\")\n"
        "export TENON_MODULE fixture.memory;\n"
        "TENON_IMPORT :dependency;\n"
        "#endif\n"
        "#define TENON_DUP(value) value + value\n"
        "TENON_DUP(__COUNTER__)\n"_str);
    auto includes = MemoryIncludes(sources);
    auto builtins = TestBuiltins {};
    auto pragmas  = IgnorePragmas {};
    auto events   = TestEvents {};
    auto result   = preprocess(PreprocessRequest {
                                 .source = rstd::path::PathBuf::from("/main.cppm"_str),
                                 .environment_identity = String::make("memory-v1"_str),
                               },
                               sources,
                               includes,
                               builtins,
                               pragmas,
                               events);
    if (result.is_err()) return 1;
    if (! contains_sequence(result->tokens, "module"_str, "fixture"_str)) return 2;
    if (! contains_sequence(result->tokens, "import"_str, ":"_str)) return 3;
    if (result->sources.len() != usize(3)) return 4;
    if (events.includes != usize(3) || events.probes != usize(1)) return 5;
    if (builtins.text_queries != usize(1)) return 6;
    if (! contains_sequence(result->tokens, "2"_str, "+"_str, "2"_str)) return 7;

    auto stream_builtins = TestBuiltins {};
    auto stream_pragmas  = IgnorePragmas {};
    auto stream_events   = TestEvents {};
    auto consumer = tenon::frontend::parser::ModuleDependencyConsumer::make();
    auto streamed = preprocess_to(
        PreprocessRequest {
            .source = rstd::path::PathBuf::from("/main.cppm"_str),
            .environment_identity = String::make("memory-v1"_str),
        },
        sources,
        includes,
        stream_builtins,
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
        facts->imports[usize {}].logical_name.as_str() !=
            "fixture.memory:dependency"_str) {
        return 10;
    }
    return 0;
}
