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

struct BuildSetupReport {
    BuildToolchainReport toolchain;
};

struct BuildSetupReportSink {
    void* context {};
    void (*notify)(void*, const BuildSetupReport&) noexcept {};
};

} // namespace lito

namespace lito
{

auto emit_build_setup_report(const Option<BuildSetupReportSink>& reporter,
                             const lito::config::ToolchainSpec&  requested,
                             const ClangToolchain&               resolved) noexcept -> void {
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
    reporter->notify(reporter->context, report);
}

} // namespace lito
