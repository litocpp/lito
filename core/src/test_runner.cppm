export module lito.test_runner;

import rstd;
import lito.model;
import lito.builder;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito
{

struct TestEvent {
    ref<str>              package;
    ref<rstd::path::Path> executable;
    ref<rstd::path::Path> working_directory;
    slice<String>         arguments;
};

struct TestObserver {
    void* context {};
    void (*notify)(void*, const TestEvent&) noexcept {};
};

struct TestRequest {
    BuildRequest         build;
    Vec<String>          arguments;
    bool                 no_run { false };
    Option<TestObserver> observer;
};

} // namespace lito

namespace lito
{

auto emit_run(const TestRequest& request, const BuiltArtifact& artifact) noexcept -> void {
    if (request.observer.is_none()) return;
    const auto& observer = *request.observer;
    if (observer.notify == nullptr) return;
    observer.notify(observer.context,
                    TestEvent {
                        .package           = artifact.package.as_str(),
                        .executable        = artifact.path.as_path(),
                        .working_directory = artifact.package_root.as_path(),
                        .arguments         = request.arguments.as_slice(),
                    });
}

} // namespace lito

export namespace lito
{

struct TestExecution {
    String                            package;
    PathBuf                           executable;
    PathBuf                           working_directory;
    Option<rstd::process::ExitStatus> status;
    Option<String>                    error;
    rstd::time::Duration              elapsed;

    auto success() const noexcept -> bool {
        return error.is_none() && status.is_some() && status->success();
    }
};

struct TestSummary {
    BuildSummary       build;
    Vec<TestExecution> executions;

    auto success() const noexcept -> bool {
        for (const auto& execution : build.compile_tests) {
            if (! execution.success()) return false;
        }
        for (const auto& execution : executions) {
            if (! execution.success()) return false;
        }
        return true;
    }
};

auto test(TestRequest request) -> Result<TestSummary> {
    request.build.purpose = PackageSelectionPurpose::Test;
    auto built            = build(request.build);
    if (built.is_err()) return Err(rstd::move(built).unwrap_err());
    auto summary = rstd::move(built).unwrap();

    auto selected = Vec<const BuiltArtifact*>::make();
    for (const auto& artifact : summary.artifacts) {
        if (artifact.kind == ArtifactKind::TestExecutable) {
            selected.push(rstd::addressof(artifact));
        }
    }
    rstd::slice_::sort_unstable_by(selected.as_mut_slice().as_mut_ref(),
                                   [](const BuiltArtifact* left, const BuiltArtifact* right) {
                                       return left->package < right->package;
                                   });

    auto executions = Vec<TestExecution>::with_capacity(selected.len());
    if (! request.no_run) {
        for (const auto* artifact : selected) {
            emit_run(request, *artifact);
            auto command = rstd::process::Command::make(artifact->path.as_path().as_os_str());
            for (const auto& argument : request.arguments) command.arg(argument.as_str());
            command.current_dir(artifact->package_root.as_path());

            auto started = rstd::time::Instant::now();
            auto status  = command.status();
            auto elapsed = started.elapsed();
            if (status.is_err()) {
                executions.push(TestExecution {
                    .package           = artifact->package.clone(),
                    .executable        = artifact->path.clone(),
                    .working_directory = artifact->package_root.clone(),
                    .error             = Some(rstd::format("failed to execute test: {}",
                                                           rstd::move(status).unwrap_err())),
                    .elapsed           = elapsed,
                });
                continue;
            }
            executions.push(TestExecution {
                .package           = artifact->package.clone(),
                .executable        = artifact->path.clone(),
                .working_directory = artifact->package_root.clone(),
                .status            = Some(rstd::move(status).unwrap()),
                .elapsed           = elapsed,
            });
        }
    }
    return Ok(TestSummary {
        .build      = rstd::move(summary),
        .executions = rstd::move(executions),
    });
}

} // namespace lito
