module;
#include <rstd/macro.hpp>

module lito.driver;

import rstd;
import :command.pack;
import :build;
import :build.project;
import :package.module_catalog;
import :pack.script;
import :project;
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

    auto path() const -> ref<rstd::path::Path> { return path_.as_path(); }
};

auto pack_io_error(String action, ref<rstd::path::Path> path, rstd::io::Error error)
    -> CommandError {
    return CommandError::System(
        SystemError::Io(rstd::move(action), PathBuf::from(path), rstd::move(error)));
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
    auto       loaded  = load_completed_build_product(
        project_root, request.build_directory.as_path(), profile, request.configuration.target);
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

struct PackPreparation {
    PackScriptDependencies        dependencies;
    Option<ResolvedBuildProject>  build;
    Option<CompletedBuildProduct> product;
};

auto prepare_pack(const PackRequest&                request,
                  lito::workspace::WorkspaceCatalog workspace,
                  const ResolvedProcessEnvironment& environment,
                  ref<str> package_name) -> CommandResult<PackPreparation> {
    if (request.no_build) {
        auto product =
            rstd_try(reuse_pack_product(request.build, request.build.selection.root.as_path()));
        auto dependencies = rstd_try(resolve_pack_script_dependencies(
            request.build, rstd::move(workspace), environment, product, package_name));
        return Ok(PackPreparation {
            .dependencies = rstd::move(dependencies),
            .product      = Some(rstd::move(product)),
        });
    }
    auto build =
        rstd_try(resolve_build_for_recipe(request.build, rstd::move(workspace), environment));
    auto dependencies =
        rstd_try(pack_script_dependencies(build.session.project.selection, package_name));
    return Ok(PackPreparation {
        .dependencies = rstd::move(dependencies),
        .build        = Some(rstd::move(build)),
    });
}

auto finish_pack_product(const BuildRequest&               request,
                         PackPreparation&                  prepared,
                         const ResolvedProcessEnvironment& environment)
    -> CommandResult<CompletedBuildProduct> {
    if (prepared.product.is_some()) return Ok(rstd::move(prepared.product).unwrap());
    auto built = rstd_try(build_and_publish_resolved_project(
        request, environment, rstd::move(prepared.build).unwrap()));
    return Ok(rstd::move(built.product));
}

auto prepare_pack_variants(PackRequest&                      request,
                           PackPreparation&                  prepared,
                           const PackRecipe&                 recipe,
                           const ResolvedProcessEnvironment& environment)
    -> CommandResult<Vec<CompletedBuildProduct>> {
    auto products = Vec<CompletedBuildProduct>::make();
    if (recipe.android_abis.is_empty()) {
        products.push(rstd_try(finish_pack_product(request.build, prepared, environment)));
        return Ok(rstd::move(products));
    }
    auto& target = request.build.configuration.target;
    if (! target.is_Android()) {
        return Err(CommandError::Message(
            rstd::format("pack recipe '{}' requires an Android build.target", recipe.name)));
    }
    auto& request_abi = target.as_Android().target.abi;
    auto  seed        = request_abi.clone();
    if (! recipe.android_abis.iter().any([&](auto abi) {
            return *abi == seed;
        })) {
        return Err(CommandError::Message(rstd::format(
            "pack recipe '{}' android_abis must include the configured Android ABI '{}'",
            recipe.name,
            seed)));
    }
    for (const auto& abi : recipe.android_abis) {
        request_abi = abi.clone();
        if (abi == seed) {
            products.push(rstd_try(finish_pack_product(request.build, prepared, environment)));
            continue;
        }
        if (request.no_build) {
            products.push(rstd_try(
                reuse_pack_product(request.build, request.build.selection.root.as_path())));
            continue;
        }
        auto project =
            lito::workspace::resolve_project_entry(request.build.selection.root.as_path());
        if (project.is_err()) {
            return Err(CommandError::Message(
                rstd::format("cannot resolve pack project for Android ABI '{}': {}",
                             abi,
                             rstd::move(project).unwrap_err())));
        }
        auto resolved = rstd_try(resolve_build_for_recipe(
            request.build, rstd::move(project).unwrap().catalog, environment));
        auto built    = rstd_try(
            build_and_publish_resolved_project(request.build, environment, rstd::move(resolved)));
        products.push(rstd::move(built.product));
    }
    return Ok(rstd::move(products));
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

auto execute_pack_tool(const PackRecipe&                 recipe,
                       ref<rstd::path::Path>             tool,
                       ref<rstd::path::Path>             package_root,
                       const ResolvedProcessEnvironment& environment,
                       const CompletedBuildProduct&      product,
                       ref<rstd::path::Path>             staged) -> CommandResult<empty> {
    auto command = rstd::process::Command::make(tool.as_os_str());
    rstd_try(append_pack_arguments(command, recipe, product, staged));
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
    return Ok(empty {});
}

auto pack_callback_target(const PackRecipe& recipe, const CompletedBuildProduct& product)
    -> CommandResult<PackCallbackTarget> {
    auto        artifacts = Vec<PackCallbackArtifact>::with_capacity(recipe.inputs.len());
    for (const auto& target : recipe.inputs) {
        auto artifact = rstd_try(pack_artifact(product, target));
        artifacts.push(PackCallbackArtifact {
            .selector = rstd::format("{}:{}",
                                     lito::package::package_target_kind_name(target.kind),
                                     target.name.as_str()),
            .path     = artifact->primary.path.clone(),
        });
    }
    return Ok(PackCallbackTarget {
        .target          = product.target.clone(),
        .android_abi     = product.android_abi.clone(),
        .android_min_api = product.android_minimum_api,
        .artifacts       = rstd::move(artifacts),
    });
}

auto execute_pack_callback(PackScriptSession&                session,
                           const Vec<CompletedBuildProduct>& products,
                           ref<rstd::path::Path>             package_root,
                           ref<rstd::path::Path>             work_dir,
                           ref<rstd::path::Path>             output) -> CommandResult<empty> {
    auto targets = Vec<PackCallbackTarget>::with_capacity(products.len());
    for (const auto& product : products) {
        targets.push(rstd_try(pack_callback_target(session.recipe(), product)));
    }
    return session.execute(PackCallbackContext {
        .package_root = PathBuf::from(package_root),
        .work_dir     = PathBuf::from(work_dir),
        .output       = PathBuf::from(output),
        .targets      = rstd::move(targets),
    });
}

auto execute_pack(PackScriptSession&                session,
                  const Option<PathBuf>&            tool,
                  ref<rstd::path::Path>             package_root,
                  const ResolvedProcessEnvironment& environment,
                  const Vec<CompletedBuildProduct>& products,
                  PathBuf                           output) -> CommandResult<PathBuf> {
    const auto& recipe = session.recipe();
    auto        parent = output.as_path().parent();
    if (parent.is_none() || output.as_path().file_name().is_none() ||
        ! output.as_path().is_absolute()) {
        return Err(CommandError::Message(
            rstd::format("pack output '{}' must specify an absolute file path", output.as_path())));
    }
    auto directory = PackStagingDirectory(rstd_try(reserve_pack_directory(*parent)));
    auto staged    = PathBuf::from(directory.path()).join(recipe.output.as_path());
    if (tool.is_some()) {
        rstd_try(execute_pack_tool(recipe,
                                   tool->as_path(),
                                   package_root,
                                   environment,
                                   products[usize {}],
                                   staged.as_path()));
    } else {
        rstd_try(execute_pack_callback(
            session, products, package_root, directory.path(), staged.as_path()));
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
    auto package_root            = (*package)->root.clone();
    request.build.selection.root = rstd::move(project.root);
    if (request.build.profile.is_none()) {
        request.build.profile = Some(lito::manifest::BuildProfileName { .value = "release"_Str });
    }
    request.build.purpose = lito::package::PackageSelectionPurpose::Production;
    auto environment = rstd_try(ResolvedProcessEnvironment::resolve(request.build.environment));
    auto prepared    = rstd_try(
        prepare_pack(request, rstd::move(project.catalog), environment, package_name.as_str()));
    auto        session = rstd_try(PackScriptSession::load(package_root.as_path(),
                                                           package_name.as_str(),
                                                           request.recipe.as_str(),
                                                           rstd::move(prepared.dependencies),
                                                           environment));
    const auto& recipe  = session.recipe();
    auto        tool    = Option<PathBuf> {};
    if (! recipe.lua_callback) {
        tool = Some(rstd_try(pack_tool(recipe, package_root.as_path(), environment)));
    }
    for (const auto& target : recipe.inputs) request.build.exact_targets.push(target.clone());
    auto        products = rstd_try(prepare_pack_variants(request, prepared, recipe, environment));
    const auto& first    = products[usize {}];
    const auto& output_base =
        recipe.android_abis.is_empty() ? first.build_directory : first.base_directory;
    auto output = request.output.is_some()
                      ? rstd::move(request.output).unwrap()
                      : output_base.join(PathBuf::from("pack"_str).as_path())
                            .join(PathBuf::from(package_name.as_str()).as_path())
                            .join(PathBuf::from(recipe.name.as_str()).as_path())
                            .join(recipe.output.as_path());
    return execute_pack(
        session, tool, package_root.as_path(), environment, products, rstd::move(output));
}

} // namespace lito
