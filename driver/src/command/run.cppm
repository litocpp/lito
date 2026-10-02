module;
#include <rstd/macro.hpp>

export module lito.driver:command.run;

import rstd;
import lito.core;
import lito.cpp;
import lito.system;
import :build;
import :build.request;
import :build.result;
import :command.error;
import :command.artifact;
import :package.selection;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito
{
struct RunRequest {
    BuildRequest build;
    String       example;
    Vec<String>  arguments;
};

struct RunSummary {
    BuildSummary      build;
    ArtifactExecution execution;
};

auto run(RunRequest request) -> CommandResult<RunSummary> {
    if (request.example.is_empty() || ! request.build.targets.is_empty() ||
        ! request.build.exact_targets.is_empty())
        return Err(
            CommandError::Message("run requires one example and no other target selectors"_Str));
    request.build.purpose = lito::package::PackageSelectionPurpose::Example;
    request.build.targets.push(rstd::format("example:{}", request.example));
    auto environment = lito::system::ResolvedProcessEnvironment::resolve(request.build.environment);
    if (environment.is_err())
        return Err(rstd::into<CommandError>(rstd::move(environment).unwrap_err()));
    auto built = build_with_environment(request.build, *environment);
    if (built.is_err()) return Err(rstd::into<CommandError>(rstd::move(built).unwrap_err()));
    auto summary   = rstd::move(built).unwrap();
    auto artifacts = selected_artifacts(summary, cpp::ArtifactKind::ExampleExecutable);
    if (artifacts.len() != usize(1))
        return Err(CommandError::Message("run requires exactly one example artifact"_Str));
    rstd_try(ensure_artifact_runner(summary.platform, "run"_str));
    if (summary.platform.cross)
        return Err(CommandError::Message(
            rstd::format("run cannot execute cross target '{}' without a configured target runner",
                         summary.platform.effective_target.triple)));
    auto runtime   = rstd_try(artifact_runtime_environment(summary, *environment));
    auto execution = execute_artifact(
        *artifacts[usize {}], request.arguments, *environment, runtime, "example"_str);
    return Ok(RunSummary { .build = rstd::move(summary), .execution = rstd::move(execution) });
}
} // namespace lito
