export module lito.command.test;

import rstd;
import lito.error;
import lito.command.error_contract;
import lito.build.contract;
import lito.workspace.contract;
import lito.package.identity;
import lito.package.target_contract;
import lito.build;
import lito.system.environment;
import lito.command.artifact;

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
                        .package           = artifact.target.package.as_str(),
                        .executable        = artifact.path.as_path(),
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
    request.build.purpose = PackageSelectionPurpose::Test;
    auto environment      = ResolvedProcessEnvironment::resolve(request.build.environment);
    if (environment.is_err()) {
        return Err(rstd::into<CommandError>(rstd::move(environment).unwrap_err()));
    }
    auto built = build_with_environment(request.build, *environment);
    if (built.is_err()) {
        return Err(rstd::into<CommandError>(rstd::move(built).unwrap_err()));
    }
    auto summary = rstd::move(built).unwrap();

    auto selected = selected_artifacts(summary, ArtifactKind::TestExecutable);

    auto executions = Vec<TestExecution>::with_capacity(selected.len());
    if (! request.no_run) {
        for (const auto* artifact : selected) {
            emit_run(request, *artifact);
            executions.push(
                execute_artifact(*artifact, request.arguments, *environment, "test"_str));
        }
    }
    return Ok(TestSummary {
        .build      = rstd::move(summary),
        .executions = rstd::move(executions),
    });
}

} // namespace lito
