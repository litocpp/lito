export module lito.test.base_support;

import rstd;
import lito;
import lito.system.environment;
import lito.workspace.contract;

using namespace rstd::prelude;
using namespace rstd::literals;
using PathBuf = rstd::path::PathBuf;

export namespace lito_test
{

template<typename... Values>
auto strings(Values... values) -> Vec<String> {
    auto result = Vec<String>::with_capacity(usize(sizeof...(Values)));
    (result.push(String::make(values)), ...);
    return result;
}

auto fixture_path(ref<str> relative) -> PathBuf {
    auto path = PathBuf::from("fixtures"_str).join(PathBuf::from(relative).as_path());
    return rstd::fs::canonicalize(path.as_path()).unwrap();
}

auto repository_path(ref<str> relative) -> PathBuf {
    auto path = PathBuf::from(".."_str).join(PathBuf::from(relative).as_path());
    return rstd::fs::canonicalize(path.as_path()).unwrap();
}

auto project_root() -> PathBuf {
    return fixture_path("project"_str);
}

auto output_root(ref<str> name) -> PathBuf {
    auto directory = rstd::format("lito-test-{}-{}", rstd::process::id(), name);
    return rstd::env::temp_dir().join(PathBuf::from(directory.as_str()).as_path());
}

auto clear_output(ref<rstd::path::Path> path) -> bool {
    auto exists = rstd::fs::exists(path);
    if (exists.is_err()) return false;
    return ! *exists || rstd::fs::remove_dir_all(path).is_ok();
}

template<typename Error>
auto error_chain_text(const Error& error) -> String {
    auto text   = rstd::format("{}", error);
    auto source = rstd::as<rstd::error::Error>(error).source();
    for (auto depth = usize {}; source.is_some() && depth < usize(32); ++depth) {
        text.push_str("\n"_str);
        text.push_str(rstd::format("{}", *source).as_str());
        source = (*source)->source();
    }
    return text;
}

auto build_profile(ref<str> name) -> lito::BuildProfileName {
    return lito::parse_build_profile(name).unwrap();
}

auto configuration() -> lito::BuildConfiguration {
    auto environment =
        lito::ResolvedProcessEnvironment::resolve(lito::ProcessEnvironmentSpec {}).unwrap();
    auto resolver = lito::ToolResolver(environment);
    auto compiler =
        resolver.resolve(PathBuf::from("clang++"_str).as_path(), "clang++"_str).unwrap().executable;
    auto linker = resolver.resolve(PathBuf::from("ld.lld"_str).as_path(), "LLD linker"_str)
                      .unwrap()
                      .executable;
    auto archiver =
        resolver.resolve(PathBuf::from("llvm-ar"_str).as_path(), "llvm-ar"_str).unwrap().executable;
    return lito::BuildConfiguration {
        .toolchain =
            lito::ToolchainSpec {
                .cxx    = rstd::move(compiler),
                .ld     = rstd::move(linker),
                .ar     = rstd::move(archiver),
                .strip  = PathBuf::from("llvm-strip"_str),
                .format = PathBuf::from("clang-format"_str),
            },
        .standard_library  = lito::StandardLibrary::Libcxx,
        .bmi_mode          = lito::BmiMode::Reduced,
        .language_standard = String::make("c++20"_str),
    };
}

auto build_request(ref<rstd::path::Path>  root,
                   ref<rstd::path::Path>  output,
                   Vec<String>            packages,
                   lito::BuildProfileName profile = {}) -> lito::BuildRequest {
    return lito::BuildRequest {
        .selection =
            lito::PackageSelection {
                .root     = PathBuf::from(root),
                .packages = rstd::move(packages),
            },
        .output        = PathBuf::from(output),
        .configuration = configuration(),
        .profile       = Some(rstd::move(profile)),
    };
}

auto artifact_count(const lito::BuildSummary& summary, lito::ArtifactKind kind) -> usize {
    auto count = usize {};
    for (const auto& artifact : summary.artifacts) {
        if (artifact.kind == kind) ++count;
    }
    return count;
}

auto has_import(const lito::ScanReport& report, ref<str> logical_name) -> bool {
    for (const auto& imported : report.result.imports) {
        if (imported.logical_name.as_str() == logical_name) return true;
    }
    return false;
}

auto copy_directory(ref<rstd::path::Path> source, ref<rstd::path::Path> destination) -> bool {
    if (rstd::fs::create_dir_all(destination).is_err()) return false;
    auto opened = rstd::fs::read_dir(source);
    if (opened.is_err()) return false;
    auto entries = rstd::move(opened).unwrap();
    for (auto next = entries.next(); next.is_some(); next = entries.next()) {
        auto entry_result = rstd::move(next).unwrap();
        if (entry_result.is_err()) return false;
        auto entry = rstd::move(entry_result).unwrap();
        auto type  = entry.file_type();
        if (type.is_err()) return false;
        auto source_path = entry.path();
        auto relative    = source_path.as_path().strip_prefix(source);
        if (relative.is_none()) return false;
        auto destination_path = PathBuf::from(destination).join(*relative);
        if (type->is_dir()) {
            if (! copy_directory(source_path.as_path(), destination_path.as_path())) return false;
            continue;
        }
        if (! type->is_file() ||
            rstd::fs::copy(source_path.as_path(), destination_path.as_path()).is_err()) {
            return false;
        }
    }
    return true;
}

} // namespace lito_test
