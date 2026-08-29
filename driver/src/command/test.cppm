module;
#include <rstd/macro.hpp>

export module lito.driver:command.test;

import rstd;
import lito.core;
import :package.selection;
import :command.error;
import :build.request;
import :build.artifact;
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
                        .package           = artifact.target.package.as_str(),
                        .executable        = artifact.primary.path.as_path(),
                        .working_directory = artifact.package_root.as_path(),
                        .arguments         = request.arguments.as_slice(),
                    });
}

} // namespace lito

export namespace lito
{

using TestExecution = ArtifactExecution;

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

auto test(TestRequest request) -> CommandResult<TestSummary> {
    request.build.purpose = lito::package::PackageSelectionPurpose::Test;
    auto environment      = ResolvedProcessEnvironment::resolve(request.build.environment);
    if (environment.is_err()) {
        return Err(rstd::into<CommandError>(rstd::move(environment).unwrap_err()));
    }
    auto built = build_with_environment(request.build, *environment);
    if (built.is_err()) {
        return Err(rstd::into<CommandError>(rstd::move(built).unwrap_err()));
    }
    auto summary = rstd::move(built).unwrap();

    auto selected = selected_artifacts(summary, cpp::ArtifactKind::TestExecutable);

    auto executions = Vec<TestExecution>::with_capacity(selected.len());
    if (! request.no_run) {
        rstd_try(ensure_artifact_runner(summary.platform, "test"_str));
        auto runtime_environment = rstd_try(artifact_runtime_environment(summary, *environment));
        for (const auto* artifact : selected) {
            emit_run(request, *artifact);
            executions.push(execute_artifact(
                *artifact, request.arguments, *environment, runtime_environment, "test"_str));
        }
    }
    return Ok(TestSummary {
        .build      = rstd::move(summary),
        .executions = rstd::move(executions),
    });
}

} // namespace lito
