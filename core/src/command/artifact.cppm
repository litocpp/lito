export module lito.command.artifact;

import rstd;
import lito.error;
import lito.build.contract;
import lito.package.identity;
import lito.package.target_contract;
import lito.system.environment;
import lito.system.process;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito
{

struct ArtifactExecution {
    PackageTargetId                   target;
    PathBuf                           executable;
    PathBuf                           working_directory;
    Option<rstd::process::ExitStatus> status;
    Option<String>                    error;
    rstd::time::Duration              elapsed;

    auto success() const noexcept -> bool {
        return error.is_none() && status.is_some() && status->success();
    }
};

auto selected_artifacts(const BuildSummary& summary, ArtifactKind kind)
    -> Vec<const BuiltArtifact*> {
    auto selected = Vec<const BuiltArtifact*>::make();
    for (const auto& artifact : summary.artifacts) {
        if (artifact.kind == kind) selected.push(rstd::addressof(artifact));
    }
    rstd::slice_::sort_unstable_by(selected.as_mut_slice().as_mut_ref(),
                                   [](const BuiltArtifact* left, const BuiltArtifact* right) {
                                       if (left->target.package != right->target.package) {
                                           return left->target.package < right->target.package;
                                       }
                                       if (left->target.kind != right->target.kind) {
                                           return left->target.kind < right->target.kind;
                                       }
                                       return left->target.name < right->target.name;
                                   });
    return selected;
}

auto execute_artifact(const BuiltArtifact&              artifact,
                      const Vec<String>&                arguments,
                      const ResolvedProcessEnvironment& environment,
                      ref<str>                          description) -> ArtifactExecution {
    auto command = rstd::process::Command::make(artifact.path.as_path().as_os_str());
    for (const auto& argument : arguments) command.arg(argument.as_str());
    command.current_dir(artifact.package_root.as_path());
    apply_command_environment(command, environment);

    auto started = rstd::time::Instant::now();
    auto status  = command.status();
    auto elapsed = started.elapsed();
    if (status.is_err()) {
        return ArtifactExecution {
            .target            = artifact.target.clone(),
            .executable        = artifact.path.clone(),
            .working_directory = artifact.package_root.clone(),
            .error             = Some(rstd::format(
                "failed to execute {}: {}", description, rstd::move(status).unwrap_err())),
            .elapsed           = elapsed,
        };
    }
    return ArtifactExecution {
        .target            = artifact.target.clone(),
        .executable        = artifact.path.clone(),
        .working_directory = artifact.package_root.clone(),
        .status            = Some(rstd::move(status).unwrap()),
        .elapsed           = elapsed,
    };
}

} // namespace lito
