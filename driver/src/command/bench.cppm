export module lito.driver:command.bench;

import rstd;
import lito.core;
import :command.error;
import :build.request;
import :build.result;
import lito.cpp;
import :build;
import lito.system;
import :command.artifact;

using namespace rstd::prelude;
using namespace lito::system;
using namespace rstd::literals;

export namespace lito
{

struct BenchEvent {
    const PackageTargetId& target;
    ref<rstd::path::Path>  executable;
    ref<rstd::path::Path>  working_directory;
    slice<String>          arguments;
};

struct BenchObserver {
    void* context {};
    void (*notify)(void*, const BenchEvent&) noexcept {};
};

struct BenchRequest {
    BuildRequest          build;
    Vec<String>           arguments;
    bool                  no_run { false };
    Option<BenchObserver> observer;
};

using BenchExecution = ArtifactExecution;

struct BenchSummary {
    BuildSummary        build;
    Vec<BenchExecution> executions;

    auto success() const noexcept -> bool {
        for (const auto& execution : executions) {
            if (! execution.success()) return false;
        }
        return true;
    }
};

auto bench(BenchRequest request) -> CommandResult<BenchSummary> {
    request.build.purpose = PackageSelectionPurpose::Benchmark;
    auto environment      = ResolvedProcessEnvironment::resolve(request.build.environment);
    if (environment.is_err()) {
        return Err(rstd::into<CommandError>(rstd::move(environment).unwrap_err()));
    }
    auto built = build_with_environment(request.build, *environment);
    if (built.is_err()) {
        return Err(rstd::into<CommandError>(rstd::move(built).unwrap_err()));
    }
    auto summary  = rstd::move(built).unwrap();
    auto selected = selected_artifacts(summary, cpp::ArtifactKind::BenchmarkExecutable);

    auto executions = Vec<BenchExecution>::with_capacity(selected.len());
    if (! request.no_run) {
        for (const auto* artifact : selected) {
            if (request.observer.is_some() && request.observer->notify != nullptr) {
                request.observer->notify(request.observer->context,
                                         BenchEvent {
                                             .target            = artifact->target,
                                             .executable        = artifact->path.as_path(),
                                             .working_directory = artifact->package_root.as_path(),
                                             .arguments         = request.arguments.as_slice(),
                                         });
            }
            executions.push(
                execute_artifact(*artifact, request.arguments, *environment, "benchmark"_str));
        }
    }
    return Ok(BenchSummary {
        .build      = rstd::move(summary),
        .executions = rstd::move(executions),
    });
}

} // namespace lito
