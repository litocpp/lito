export module lito.test.base_support;

import rstd;
import lito.tools;
import lito.driver;
import lito.cpp;
import lito.system;
import lito.core;
import lito.toolchain;

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
    auto resolver = lito::tools::ToolResolver(environment);
    auto compiler =
        resolver.resolve(PathBuf::from("clang++"_str).as_path(), "clang++"_str).unwrap().executable;
    auto archiver =
        resolver.resolve(PathBuf::from("llvm-ar"_str).as_path(), "llvm-ar"_str).unwrap().executable;
    auto resolved = lito::ClangToolchain::create(
                        lito::config::ToolchainSpec {
                            .cxx = rstd::move(compiler),
                            .ar  = rstd::move(archiver),
                        },
                        environment)
                        .unwrap();
    return lito::cpp::BuildConfiguration {
        .toolchain =
            lito::config::ToolchainSpec {
                .cc  = PathBuf::from(resolved.cc_path()),
                .cxx = PathBuf::from(resolved.cxx_path()),
                .ld  = PathBuf::from(resolved.ld_path()),
                .ar  = PathBuf::from(resolved.ar_path()),
            },
        .standard_library         = resolved.compile_target().standard_library,
        .standard_library_runtime = lito::config::StandardLibraryRuntime::Dynamic,
        .bmi_mode                 = lito::cpp::BmiMode::Reduced,
        .language_standard        = String::make("c++20"_str),
    };
}

auto linker_identity() -> lito::LinkerIdentity {
    auto environment =
        lito::system::ResolvedProcessEnvironment::resolve(lito::system::ProcessEnvironmentSpec {})
            .unwrap();
    auto build = configuration();
    return lito::probe_linker(build.toolchain.ld.as_path(), environment).unwrap();
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
        .build_directory = PathBuf::from(output),
        .configuration   = lito::config::build_configuration_request(configuration()),
        .profile         = Some(rstd::move(profile)),
    };
}

auto artifact_count(const lito::BuildSummary& summary, lito::cpp::ArtifactKind kind) -> usize {
    auto count = usize {};
    for (const auto& artifact : summary.product.artifacts) {
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
