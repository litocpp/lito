export module lito.driver:command.artifact;

import rstd;
import lito.core;
import :build.artifact;
import :build.result;
import :command.error;
import lito.cpp;
import lito.system;

using namespace rstd::prelude;
using namespace lito::system;
using namespace rstd::literals;

export namespace lito
{

struct ArtifactExecution {
    lito::package::PackageTargetId    target;
    PathBuf                           executable;
    PathBuf                           working_directory;
    Option<rstd::process::ExitStatus> status;
    Option<SystemError>               error;
    rstd::time::Duration              elapsed;

    auto success() const noexcept -> bool {
        return error.is_none() && status.is_some() && status->success();
    }
};

auto selected_artifacts(const BuildSummary& summary, cpp::ArtifactKind kind)
    -> Vec<const BuiltArtifact*> {
    auto selected = Vec<const BuiltArtifact*>::make();
    for (const auto& artifact : summary.product.artifacts) {
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

auto ensure_artifact_runner(const lito::system::BuildPlatform& platform, ref<str> command)
    -> CommandResult<empty> {
    if (platform.android_abi.is_none()) return Ok(empty {});
    return Err(CommandError::Message(
        rstd::format("{} cannot run Android target '{}' without a configured target runner",
                     command,
                     platform.effective_target.triple.as_str())));
}

auto artifact_runtime_environment(const BuildSummary&               summary,
                                  const ResolvedProcessEnvironment& environment)
    -> CommandResult<CommandEnvironment> {
    auto directories = Vec<PathBuf>::make();
    for (const auto& artifact : summary.product.artifacts) {
        if (artifact.kind != cpp::ArtifactKind::SharedLibrary) continue;
        auto parent = artifact.primary.path.as_path().parent();
        if (parent.is_none()) continue;
        auto repeated = false;
        for (const auto& directory : directories) {
            if (directory.as_path() == *parent) {
                repeated = true;
                break;
            }
        }
        if (! repeated) directories.push(PathBuf::from(*parent));
    }
    if (directories.is_empty()) return Ok(CommandEnvironment {});

    auto variable = "LD_LIBRARY_PATH"_str;
    if (summary.platform.effective_target.platform == TargetPlatform::Windows) {
        variable       = "PATH"_str;
        auto inherited = rstd::env::split_paths(environment.child_path());
        for (auto directory : inherited) directories.push(rstd::move(directory));
    } else {
        if (summary.platform.effective_target.platform == TargetPlatform::Macos) {
            variable = "DYLD_LIBRARY_PATH"_str;
        }
        auto inherited = rstd::env::var_os(variable);
        if (inherited.is_some()) {
            auto paths = rstd::env::split_paths(inherited->as_os_str());
            for (auto directory : paths) directories.push(rstd::move(directory));
        }
    }

    auto value = rstd::env::join_paths(directories.as_slice());
    if (value.is_err()) {
        return Err(rstd::into<CommandError>(SystemError::PathJoin(rstd::move(value).unwrap_err())));
    }
    auto entries = Vec<CommandEnvironmentEntry>::make();
    entries.push(CommandEnvironmentEntry {
        .key   = String::make(variable),
        .value = Some(rstd::move(value).unwrap()),
    });
    return Ok(CommandEnvironment { .entries = rstd::move(entries) });
}

auto execute_artifact(const BuiltArtifact&              artifact,
                      const Vec<String>&                arguments,
                      const ResolvedProcessEnvironment& environment,
                      const CommandEnvironment&         runtime_environment,
                      ref<str>                          description) -> ArtifactExecution {
    auto command = rstd::process::Command::make(artifact.primary.path.as_path().as_os_str());
    for (const auto& argument : arguments) command.arg(argument.as_str());
    command.current_dir(artifact.package_root.as_path());
    apply_command_environment(
        command,
        environment,
        Some(ref<CommandEnvironment>::from_raw_parts(rstd::addressof(runtime_environment))));

    auto started = rstd::time::Instant::now();
    auto status  = command.status();
    auto elapsed = started.elapsed();
    if (status.is_err()) {
        return ArtifactExecution {
            .target            = artifact.target.clone(),
            .executable        = artifact.primary.path.clone(),
            .working_directory = artifact.package_root.clone(),
            .error             = Some(SystemError::Io(rstd::format("execute {}", description),
                                                      artifact.primary.path.clone(),
                                                      rstd::move(status).unwrap_err())),
            .elapsed           = elapsed,
        };
    }
    return ArtifactExecution {
        .target            = artifact.target.clone(),
        .executable        = artifact.primary.path.clone(),
        .working_directory = artifact.package_root.clone(),
        .status            = Some(rstd::move(status).unwrap()),
        .elapsed           = elapsed,
    };
}

} // namespace lito
