export module lito.test.base_support;

import rstd;
import lito.driver;
import lito.cpp;
import lito.system;
import lito.core;

using namespace rstd::prelude;
using namespace lito::system;
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

template<typename Error>
auto error_chain_text(const Error& error) -> String {
    auto text   = rstd::format("{}", error);
    auto source = as<rstd::error::Error>(error).source();
    for (auto depth = usize {}; source.is_some() && depth < usize(32); ++depth) {
        text.push_str("\n"_str);
        text.push_str(rstd::format("{}", *source).as_str());
        source = (*source)->source();
    }
    return text;
}

auto build_profile(ref<str> name) -> lito::manifest::BuildProfileName {
    return lito::manifest::parse_build_profile(name).unwrap();
}

auto configuration() -> lito::cpp::BuildConfiguration {
    auto environment =
        lito::system::ResolvedProcessEnvironment::resolve(lito::system::ProcessEnvironmentSpec {})
            .unwrap();
    auto resolver = lito::system::ToolResolver(environment);
    auto compiler =
        resolver.resolve(PathBuf::from("clang++"_str).as_path(), "clang++"_str).unwrap().executable;
    auto linker = resolver.resolve(PathBuf::from("ld.lld"_str).as_path(), "LLD linker"_str)
                      .unwrap()
                      .executable;
    auto archiver =
        resolver.resolve(PathBuf::from("llvm-ar"_str).as_path(), "llvm-ar"_str).unwrap().executable;
    return lito::cpp::BuildConfiguration {
        .toolchain =
            lito::config::ToolchainSpec {
                .cxx = rstd::move(compiler),
                .ld  = rstd::move(linker),
                .ar  = rstd::move(archiver),
            },
#if defined(_WIN32)
        .standard_library = lito::config::StandardLibrary::Msvc,
#else
        .standard_library = lito::config::StandardLibrary::Libcxx,
#endif
        .standard_library_runtime = lito::config::StandardLibraryRuntime::Dynamic,
        .bmi_mode                 = lito::cpp::BmiMode::Reduced,
        .language_standard        = String::make("c++20"_str),
    };
}

auto build_request(ref<rstd::path::Path>            root,
                   ref<rstd::path::Path>            output,
                   Vec<String>                      packages,
                   lito::manifest::BuildProfileName profile = {}) -> lito::BuildRequest {
    return lito::BuildRequest {
        .selection =
            lito::package::PackageSelection {
                .root     = PathBuf::from(root),
                .packages = rstd::move(packages),
            },
        .output        = PathBuf::from(output),
        .configuration = configuration(),
        .profile       = Some(rstd::move(profile)),
    };
}

auto artifact_count(const lito::BuildSummary& summary, lito::cpp::ArtifactKind kind) -> usize {
    auto count = usize {};
    for (const auto& artifact : summary.artifacts) {
        if (artifact.kind == kind) ++count;
    }
    return count;
}

auto has_import(const lito::ScanReport& report, ref<str> logical_name) -> bool {
    if (! report.result.language.is_Cpp()) return false;
    for (const auto& imported : report.result.language.as_Cpp().facts.required_modules) {
        if (imported.imported && imported.logical_name.as_str() == logical_name) return true;
    }
    return false;
}

} // namespace lito_test
