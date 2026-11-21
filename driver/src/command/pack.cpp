module;
#include <rstd/macro.hpp>

module lito.driver;

import rstd;
import :command.pack;
import :build;
import :pack.script;
import lito.core;
import lito.system;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace lito
{
namespace
{

using lito::system::ResolvedProcessEnvironment;
using lito::system::SystemError;
using lito::package::PackageTargetId;

class PackStagingDirectory {
    PathBuf path_;

public:
    explicit PackStagingDirectory(PathBuf path): path_(rstd::move(path)) {}
    PackStagingDirectory(const PackStagingDirectory&)                    = delete;
    auto operator=(const PackStagingDirectory&) -> PackStagingDirectory& = delete;
    ~PackStagingDirectory() { (void)rstd::fs::remove_dir_all(path_.as_path()); }

    auto file(ref<rstd::path::Path> name) const -> PathBuf { return path_.join(name); }
};

auto pack_io_error(String action, ref<rstd::path::Path> path, rstd::io::Error error)
    -> CommandError {
    return CommandError::System(
        SystemError::Io(rstd::move(action), PathBuf::from(path), rstd::move(error)));
}

auto reserve_pack_directory(ref<rstd::path::Path> parent) -> CommandResult<PathBuf> {
    auto created_parent = rstd::fs::create_dir_all(parent);
    if (created_parent.is_err()) {
        return Err(pack_io_error(
            "create pack output directory"_Str, parent, rstd::move(created_parent).unwrap_err()));
    }
    constexpr auto attempt_limit = usize(64);
    for (auto attempt = usize {}; attempt < attempt_limit; ++attempt) {
        auto name = PathBuf::from(rstd::format(".lito-pack-{}-{}", rstd::process::id(), attempt));
        auto path = PathBuf::from(parent).join(name.as_path());
        auto made = rstd::fs::create_dir(path.as_path());
        if (made.is_ok()) return Ok(rstd::move(path));
        auto error = rstd::move(made).unwrap_err();
        if (error.kind() != rstd::io::ErrorKind { rstd::io::ErrorKind::AlreadyExists }) {
            return Err(pack_io_error(
                "create pack staging directory"_Str, path.as_path(), rstd::move(error)));
        }
    }
    return Err(CommandError::Message(
        rstd::format("cannot reserve pack staging directory under '{}'", parent)));
}

auto pack_tool(const PackRecipe&                 recipe,
               ref<rstd::path::Path>             package_root,
               const ResolvedProcessEnvironment& environment) -> CommandResult<PathBuf> {
    auto tool = PathBuf::from(recipe.tool.as_str());
    if (tool.as_path().is_relative() &&
        ! lito::system::is_searchable_executable_name(tool.as_path())) {
        tool = PathBuf::from(package_root).join(tool.as_path());
    }
    auto description = rstd::format("pack recipe '{}' tool", recipe.name.as_str());
    auto resolved = rstd_try(environment.locate_executable(tool.as_path(), description.as_str()));
    if (resolved.is_none()) {
        return Err(CommandError::Message(
            rstd::format("cannot find {} '{}'", description.as_str(), recipe.tool.as_str())));
    }
    return Ok(rstd::move(resolved).unwrap());
}

auto pack_artifact(const CompletedBuildProduct& product, const PackageTargetId& target)
    -> CommandResult<const BuiltArtifact*> {
    const BuiltArtifact* selected = nullptr;
    for (const auto& artifact : product.artifacts) {
        if (artifact.target != target || artifact.install_link.is_some() ||
            artifact.primary.role != ArtifactFileRole::Runtime)
            continue;
        if (selected != nullptr) {
            return Err(CommandError::Message(
                rstd::format("build contains multiple runtime artifacts for '{}'",
                             lito::package::package_target_id_text(target))));
        }
        selected = rstd::addressof(artifact);
    }
    if (selected == nullptr) {
        return Err(
            CommandError::Message(rstd::format("build has no runtime artifact for '{}'",
                                               lito::package::package_target_id_text(target))));
    }
    auto checked = validate_completed_build_product_file(
        product, selected->primary.path.as_path(), "pack artifact"_str);
    if (checked.is_err()) {
        return Err(CommandError::Build(BuildError::Product(rstd::move(checked).unwrap_err())));
    }
    return Ok(selected);
}

auto reuse_pack_product(const BuildRequest& request, ref<rstd::path::Path> project_root)
    -> CommandResult<CompletedBuildProduct> {
    const auto profile = request.profile->as_str();
    auto       loaded =
        load_completed_build_product(project_root, request.build_directory.as_path(), profile);
    if (loaded.is_err()) {
        return Err(CommandError::Build(BuildError::Product(rstd::move(loaded).unwrap_err())));
    }
    auto product = rstd::move(loaded).unwrap();
    auto checked =
        validate_completed_build_product(product, request, profile, product.target.as_str());
    if (checked.is_err()) {
        return Err(CommandError::Build(BuildError::Product(rstd::move(checked).unwrap_err())));
    }
    if (request.configuration.toolchain.target.is_Config()) {
        const auto& configured = request.configuration.toolchain.target.as_Config();
        auto        parsed     = lito::system::parse_target_info(product.target.as_str());
        if (parsed.is_err() || configured.os.as_str() != parsed->platform_name() ||
            configured.architecture != parsed->architecture ||
            (configured.vendor.is_some() && *configured.vendor != parsed->vendor) ||
            (configured.environment.is_some() && configured.environment != parsed->environment)) {
            return Err(CommandError::Message(rstd::format(
                "completed build target '{}' does not match configured toolchain target '{}-{}'",
                product.target.as_str(),
                lito::system::architecture_name(configured.architecture),
                configured.os.as_str())));
        }
    }
    return Ok(rstd::move(product));
}

auto prepare_pack_product(PackRequest& request, lito::workspace::ResolvedProjectEntry project)
    -> CommandResult<CompletedBuildProduct> {
    if (request.no_build) return reuse_pack_product(request.build, project.root.as_path());
    auto built = build_and_publish_resolved_project(rstd::move(request.build), rstd::move(project));
    if (built.is_err()) return Err(CommandError::Build(rstd::move(built).unwrap_err()));
    return Ok(rstd::move(built).unwrap().product);
}

auto append_pack_arguments(rstd::process::Command&      command,
                           const PackRecipe&            recipe,
                           const CompletedBuildProduct& product,
                           ref<rstd::path::Path>        staged_path) -> CommandResult<empty> {
    for (const auto& argument : recipe.arguments) {
        if (argument.is_Literal()) {
            command.arg(argument.as_Literal().value.as_str());
        } else if (argument.is_Artifact()) {
            auto artifact = rstd_try(pack_artifact(product, argument.as_Artifact().target));
            command.arg(artifact->primary.path.as_path().as_os_str());
        } else if (argument.is_Output()) {
            command.arg(staged_path.as_os_str());
        } else {
            if (product.target_kind != "android"_str || product.android_abi.is_empty()) {
                return Err(CommandError::Message(rstd::format(
                    "pack recipe '{}' requires Android target metadata; completed target is '{}'",
                    recipe.name.as_str(),
                    product.target.as_str())));
            }
            if (argument.is_AndroidAbi()) {
                command.arg(product.android_abi.as_str());
            } else {
                auto minimum_api = rstd::format("{}", product.android_minimum_api);
                command.arg(minimum_api.as_str());
            }
        }
    }
    return Ok(empty {});
}

auto execute_pack(const PackRecipe&                 recipe,
                  ref<rstd::path::Path>             tool,
                  ref<rstd::path::Path>             package_root,
                  const ResolvedProcessEnvironment& environment,
                  const CompletedBuildProduct&      product,
                  PathBuf                           output) -> CommandResult<PathBuf> {
    auto parent = output.as_path().parent();
    auto name   = output.as_path().file_name();
    if (parent.is_none() || name.is_none() || ! output.as_path().is_absolute()) {
        return Err(CommandError::Message(
            rstd::format("pack output '{}' must specify an absolute file path", output.as_path())));
    }
    auto directory = PackStagingDirectory(rstd_try(reserve_pack_directory(*parent)));
    auto staged    = directory.file(recipe.output.as_path());
    auto command   = rstd::process::Command::make(tool.as_os_str());
    rstd_try(append_pack_arguments(command, recipe, product, staged.as_path()));
    command.current_dir(package_root);
    lito::system::apply_command_environment(command, environment);
    auto status = command.status();
    if (status.is_err()) {
        return Err(pack_io_error(rstd::format("execute pack recipe '{}'", recipe.name.as_str()),
                                 tool,
                                 rstd::move(status).unwrap_err()));
    }
    if (! status->success()) {
        auto code = status->code();
        return Err(CommandError::Message(
            rstd::format("pack recipe '{}' tool '{}' failed with {} {}",
                         recipe.name.as_str(),
                         tool,
                         code.is_some() ? "exit code"_str : "signal"_str,
                         code.is_some() ? *code : status->signal().unwrap_or(i32(-1)))));
    }
    auto metadata = rstd::fs::symlink_metadata(staged.as_path());
    if (metadata.is_err()) {
        return Err(pack_io_error(
            rstd::format("inspect output from pack recipe '{}'", recipe.name.as_str()),
            staged.as_path(),
            rstd::move(metadata).unwrap_err()));
    }
    if (! metadata->is_file() || metadata->is_symlink()) {
        return Err(CommandError::Message(
            rstd::format("pack recipe '{}' did not produce a regular file '{}'",
                         recipe.name.as_str(),
                         staged.as_path())));
    }
    auto published = rstd::fs::rename(staged.as_path(), output.as_path());
    if (published.is_err()) {
        return Err(pack_io_error(
            "publish pack output"_Str, output.as_path(), rstd::move(published).unwrap_err()));
    }
    return Ok(rstd::move(output));
}

} // namespace

auto pack(PackRequest request) -> CommandResult<PathBuf> {
    if (request.build.selection.packages.len() != usize(1) || request.recipe.is_empty()) {
        return Err(CommandError::Message("pack requires one local package and a named recipe"_Str));
    }
    if (! request.build.targets.is_empty() || ! request.build.exact_targets.is_empty()) {
        return Err(CommandError::Message("pack selects build targets through its recipe"_Str));
    }
    auto resolved = lito::workspace::resolve_project_entry(request.build.selection.root.as_path());
    if (resolved.is_err()) {
        return Err(CommandError::Message(
            rstd::format("cannot resolve pack project: {}", rstd::move(resolved).unwrap_err())));
    }
    auto project      = rstd::move(resolved).unwrap();
    auto package_name = request.build.selection.packages[usize {}].clone();
    auto package      = project.catalog.package(package_name.as_str());
    if (package.is_none()) {
        return Err(CommandError::Message(
            rstd::format("unknown local pack package '{}'", package_name.as_str())));
    }
    auto package_root = (*package)->root.clone();
    auto recipe       = rstd_try(
        load_pack_recipe(package_root.as_path(), package_name.as_str(), request.recipe.as_str()));
    auto environment = rstd_try(ResolvedProcessEnvironment::resolve(request.build.environment));
    auto tool        = rstd_try(pack_tool(recipe, package_root.as_path(), environment));
    for (const auto& argument : recipe.arguments) {
        if (! argument.is_Artifact()) continue;
        const auto& target  = argument.as_Artifact().target;
        auto        present = false;
        for (const auto& prior : request.build.exact_targets) {
            if (prior == target) {
                present = true;
                break;
            }
        }
        if (! present) request.build.exact_targets.push(target.clone());
    }
    if (request.build.profile.is_none()) {
        request.build.profile = Some(lito::manifest::BuildProfileName { .value = "release"_Str });
    }
    request.build.purpose = lito::package::PackageSelectionPurpose::Production;
    auto product          = rstd_try(prepare_pack_product(request, rstd::move(project)));
    auto output           = request.output.is_some()
                                ? rstd::move(request.output).unwrap()
                                : product.build_directory.join(PathBuf::from("pack"_str).as_path())
                                      .join(PathBuf::from(package_name.as_str()).as_path())
                                      .join(PathBuf::from(recipe.name.as_str()).as_path())
                                      .join(recipe.output.as_path());
    return execute_pack(
        recipe, tool.as_path(), package_root.as_path(), environment, product, rstd::move(output));
}

} // namespace lito
