import rstd;
import tenon.preprocessor;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace tenon::preprocessor;

template<typename T>
using PpResult = tenon::preprocessor::Result<T>;

class MemorySources {
public:
    auto add(ref<str> path, ref<str> contents) -> void {
        files_.insert(String::make(path), String::make(contents));
    }

    auto contains(ref<str> path) const -> bool { return files_.contains_key(path); }

    auto load(ref<rstd::path::Path> path) -> PpResult<SourceBuffer> {
        auto text = path.to_str();
        if (text.is_none()) {
            return Err(tenon::preprocessor::Error::make(
                "memory source path is not UTF-8"_str));
        }
        auto contents = files_.get(*text);
        if (contents.is_none()) {
            return Err(tenon::preprocessor::Error::make("memory source is missing"_str));
        }
        return Ok(SourceBuffer {
            .path = rstd::path::PathBuf::from(path), .contents = (**contents).clone()
        });
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
    auto predefined_macros() -> PpResult<Vec<MacroSeed>> {
        return Ok(Vec<MacroSeed>::make());
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
        "#endif\n"_str);
    auto includes = MemoryIncludes(sources);
    auto builtins = TestBuiltins {};
    auto pragmas  = IgnorePragmas {};
    auto events   = TestEvents {};
    auto result   = preprocess(PreprocessRequest {
                                 .source = rstd::path::PathBuf::from("/main.cppm"_str),
                                 .environment_identity = String::make("memory-v1"_str),
                                 .purpose = PreprocessPurpose::DependencyDiscovery,
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
    return 0;
}
