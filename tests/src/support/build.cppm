module;

#include <rstd/macro.hpp>

export module lito.test.support.build;

import rstd;
import lito.driver;
import lito.core;
import lito.cpp;
import lito.system;
import lito.toolchain.cmake;
import lito.toolchain;
import lito.test.base_support;
import lito.test.support.project;

using namespace rstd::prelude;
using namespace lito::system;
using namespace rstd::literals;
using PathBuf = rstd::path::PathBuf;

export namespace lito_test
{
auto build_profile_project_tree() -> lito::source::SourceTreeResult<lito::source::SourceTree> {
    const ProjectFile files[] = {
        {
            "lito.toml"_str,
            R"toml([package]
name = "fixture-profile"
version = "0.1.0"

[[bin]]
link-stdlib = false
name = "profile-check"
sources = ["main.cpp"]

[profile.perf]
inherits = "release"
opt-level = 2
debug = "line-tables-only"
strip = false
lto = false

[profile.aliases]
inherits = "perf"
opt-level = "z"
debug = 1
strip = true
lto = true

[profile.codegen-variant]
inherits = "perf"
debug = "full"
strip = "symbols"
lto = "thin"

[profile.strip-debug]
inherits = "codegen-variant"
strip = "debuginfo"
)toml"_str,
        },
        {
            "main.cpp"_str,
            R"cpp(int main() {
#ifdef NDEBUG
    return 0;
#else
    return 1;
#endif
}
)cpp"_str,
        },
    };
    return source_tree(files);
}

auto environment_tool_project_tree() -> lito::source::SourceTreeResult<lito::source::SourceTree> {
    const ProjectFile files[] = {
        {
            "append-path/.lito/config.toml"_str,
            R"toml([environment]
append-path = ["tools"]

[toolchain]
cxx = "lito-fixture-clang++"
ar = "lito-fixture-llvm-ar"
format = "lito-fixture-clang-format"
)toml"_str,
        },
        {
            "append-path/lito.toml"_str,
            R"toml([package]
name = "fixture-environment-build"
version = "0.1.0"

[[bin]]
link-stdlib = false
name = "fixture-environment-build"
sources = ["src/main.cpp"]
)toml"_str,
        },
        { "append-path/src/main.cpp"_str, "auto main() -> int { return 0; }\n"_str },
        { "append-path/tools/ld"_str,
          "#!/bin/sh\nexec /usr/bin/ld \"$@\"\n"_str,
          lito::source::SourceFileMode::Executable },
        { "append-path/tools/lito-fixture-clang++"_str,
          "#!/bin/sh\nexec /nix/opt/llvm/22/bin/clang++ \"$@\"\n"_str,
          lito::source::SourceFileMode::Executable },
        { "append-path/tools/lito-fixture-clang-format"_str,
          "#!/bin/sh\nexec /nix/opt/llvm/22/bin/clang-format \"$@\"\n"_str,
          lito::source::SourceFileMode::Executable },
        { "append-path/tools/lito-fixture-llvm-ar"_str,
          "#!/bin/sh\nexec /nix/opt/llvm/22/bin/llvm-ar \"$@\"\n"_str,
          lito::source::SourceFileMode::Executable },
        {
            "test-path/.lito/config.toml"_str,
            R"toml([environment]
append-path = ["../append-path/tools"]

[toolchain]
cxx = "lito-fixture-clang++"
ar = "lito-fixture-llvm-ar"
format = "lito-fixture-clang-format"
)toml"_str,
        },
        {
            "test-path/lito.toml"_str,
            R"toml([package]
name = "fixture-environment-test"
version = "0.1.0"

[[test]]
link-stdlib = false
name = "fixture-environment-test"
sources = ["test/path.cpp"]

[usage]
linker-options = ["-Wl,-rpath,/nix/opt/llvm/22/lib/x86_64-unknown-linux-gnu"]
)toml"_str,
        },
        {
            "test-path/test/path.cpp"_str,
            R"cpp(#include <cstdlib>
#include <cstring>

auto main() -> int {
    const auto* path = std::getenv("PATH");
    return path != nullptr && std::strstr(path, "append-path/tools") != nullptr ? 0 : 1;
}
)cpp"_str,
        },
    };
    return source_tree(files);
}

auto install_selection_project_tree() -> lito::source::SourceTreeResult<lito::source::SourceTree> {
    const ProjectFile files[] = {
        { "lito.toml"_str, R"([workspace]
name = "lito-test-install-project"
members = ["test-lib", "test-app", "multi-target", "multi-consumer"]
[workspace.package]
version = "0.1.0"
[profile]
exceptions = false
rtti = false
)"_str },
        { "multi-target/lito.toml"_str, R"([package]
name = "fixture-multi-target"
version = { workspace = true }
[lib]
name = "shared"
module = "fixture.multi"
archive = "fixture_multi"
sources = ["src/lib.cppm"]
[[bin]]
link-stdlib = false
name = "shared"
sources = ["src/main.cpp"]
[[bin]]
link-stdlib = false
name = "tool"
sources = ["src/main.cpp"]
)"_str },
        { "multi-target/src/lib.cppm"_str,
          "export module fixture.multi;\nexport auto fixture_value() -> int { return 42; }\n"_str },
        { "multi-target/src/main.cpp"_str,
          "import fixture.multi;\nauto main() -> int { return fixture_value() == 42 ? 0 : 1; }\n"_str },
        { "multi-consumer/lito.toml"_str, R"([package]
name = "fixture-multi-consumer"
version = { workspace = true }
[[bin]]
link-stdlib = false
name = "fixture-multi-consumer"
sources = ["src/main.cpp"]
[dependencies.fixture-multi-target]
path = "../multi-target"
visibility = "private"
)"_str },
        { "multi-consumer/src/main.cpp"_str,
          "import fixture.multi;\nauto main() -> int { return fixture_value() == 42 ? 0 : 1; }\n"_str },
        { "test-app/lito.toml"_str, R"([package]
name = "fixture-test-app"
version = { workspace = true }
[[bin]]
link-stdlib = false
name = "fixture-test-app"
sources = ["src/main.cpp"]
)"_str },
        { "test-app/src/main.cpp"_str, "auto main() -> int { return 0; }\n"_str },
        { "test-lib/lito.toml"_str, R"([package]
name = "fixture-test-lib"
version = { workspace = true }
[lib]
name = "fixture-test-lib"
module = "fixture.test.lib"
archive = "fixture_test_lib"
sources = ["src/lib.cppm"]
)"_str },
        { "test-lib/src/lib.cppm"_str, "export module fixture.test.lib;\n"_str },
    };
    return source_tree(files);
}

auto executable(const lito::BuildSummary& summary) -> Option<ref<rstd::path::Path>> {
    for (const auto& artifact : summary.artifacts) {
        if (artifact.kind == lito::cpp::ArtifactKind::Executable) {
            return Some(artifact.path.as_path());
        }
    }
    return None();
}

auto project_root_role(const lito::package::ResolvedPackageGraph& graph, ref<str> name)
    -> Option<lito::package::ProjectRootRole> {
    for (const auto& root : graph.roots) {
        if (root.name.as_str() == name) {
            auto role = root.role;
            return Some(role);
        }
    }
    return None();
}

auto contains_name(const Vec<String>& names, ref<str> name) -> bool {
    for (const auto& candidate : names) {
        if (candidate.as_str() == name) return true;
    }
    return false;
}

auto write_executable(ref<rstd::path::Path> path) -> bool {
    if (rstd::fs::write(path, "fixture\n"_str.as_bytes()).is_err()) return false;
#if RSTD_OS_UNIX
    return rstd::fs::set_permissions(path, rstd::fs::Permissions::from_mode(u32(0755))).is_ok();
#else
    return true;
#endif
}

auto regular_file_count(ref<rstd::path::Path> directory) -> Option<usize> {
    auto opened = rstd::fs::read_dir(directory);
    if (opened.is_err()) return None();
    auto count   = usize {};
    auto entries = rstd::move(opened).unwrap();
    for (auto next = entries.next(); next.is_some(); next = entries.next()) {
        auto item = rstd::move(next).unwrap();
        if (item.is_err()) return None();
        auto entry = rstd::move(item).unwrap();
        auto type  = entry.file_type();
        if (type.is_err()) return None();
        if (type->is_file()) {
            ++count;
        } else if (type->is_dir()) {
            auto nested = regular_file_count(entry.path().as_path());
            if (nested.is_none()) return None();
            count += *nested;
        }
    }
    return Some(count);
}

struct CompileProgressCapture {
    Vec<lito::BuildProgress> values;
    Vec<PathBuf>             requested_tools;
    Vec<PathBuf>             resolved_tools;
    bool                     missing {};
};

void capture_compile_progress(void* raw_context, const lito::BuildEvent& event) noexcept {
    auto& capture = *static_cast<CompileProgressCapture*>(raw_context);
    if (event.kind != lito::BuildEventKind::Compile) return;
    if (event.progress.is_none()) {
        capture.missing = true;
        return;
    }
    capture.values.push(lito::BuildProgress {
        .current = event.progress->current,
        .total   = event.progress->total,
    });
}

void capture_build_setup(void* raw_context, const lito::BuildSetupReport& report) noexcept {
    auto&                            capture = *static_cast<CompileProgressCapture*>(raw_context);
    const lito::BuildToolResolution* tools[] = {
        rstd::addressof(report.toolchain.cc),
        rstd::addressof(report.toolchain.cxx),
        rstd::addressof(report.toolchain.ld),
        rstd::addressof(report.toolchain.ar),
    };
    for (const auto* tool : tools) {
        capture.requested_tools.push(tool->requested.clone());
        capture.resolved_tools.push(tool->executable.clone());
    }
}

} // namespace lito_test
