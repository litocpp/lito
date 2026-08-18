export module lito.driver:build.setup_report;

import rstd;
import lito.core;
import lito.toolchain;

using namespace rstd::prelude;

export namespace lito
{

struct BuildToolResolution {
    PathBuf requested;
    PathBuf executable;
};

struct BuildToolchainReport {
    BuildToolResolution cc;
    BuildToolResolution cxx;
    BuildToolResolution ld;
    BuildToolResolution ar;
};

enum class BuildOptionReportDomain
{
    Cpp,
    C,
    Link,
};

struct BuildOptionReport {
    BuildOptionReportDomain domain { BuildOptionReportDomain::Cpp };
    String                  source;
    Vec<String>             arguments;
};

struct BuildSetupReport {
    BuildToolchainReport   toolchain;
    Vec<BuildOptionReport> options;
};

struct BuildSetupReportSink {
    void* context {};
    void (*notify)(void*, const BuildSetupReport&) noexcept {};
};

} // namespace lito

namespace lito
{

auto emit_build_setup_report(const Option<BuildSetupReportSink>&      reporter,
                             const lito::config::ToolchainSpec&       requested,
                             const ClangToolchain&                    resolved,
                             const lito::config::ProjectBuildOptions& options) noexcept -> void {
    if (reporter.is_none() || reporter->notify == nullptr) return;
    auto report = BuildSetupReport {
        .toolchain =
            BuildToolchainReport {
                .cc  = BuildToolResolution { .requested  = requested.cc.clone(),
                                             .executable = PathBuf::from(resolved.cc_path()) },
                .cxx = BuildToolResolution { .requested  = requested.cxx.clone(),
                                             .executable = PathBuf::from(resolved.cxx_path()) },
                .ld  = BuildToolResolution { .requested  = requested.ld.clone(),
                                             .executable = PathBuf::from(resolved.ld_path()) },
                .ar  = BuildToolResolution { .requested  = requested.ar.clone(),
                                             .executable = PathBuf::from(resolved.ar_path()) },
            },
    };
    const auto append = [&report](BuildOptionReportDomain                    domain,
                                  const Vec<lito::config::BuildOptionInput>& inputs) {
        for (const auto& input : inputs) {
            report.options.push(BuildOptionReport {
                .domain    = domain,
                .source    = input.source.clone(),
                .arguments = input.arguments.clone(),
            });
        }
    };
    append(BuildOptionReportDomain::Cpp, options.cpp);
    append(BuildOptionReportDomain::C, options.c);
    append(BuildOptionReportDomain::Link, options.linker);
    reporter->notify(reporter->context, report);
}

} // namespace lito
