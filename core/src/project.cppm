module;
#include <rstd/macro.hpp>

export module lito.project;

import rstd;
import lito.model;
import lito.workspace_resolver;
import lito.lock_store;
import lito.package;
import lito.dependency;
import lito.toolchain;
import lito.environment;
import lito.platform;
import lito.build_profile;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace lito
{

struct ProjectResolution {
    ResolvedPackageSelection selection;
    LockStatus               lock;
};

auto resolve_project(const PackageSelection&           selection,
                     PackageSelectionPurpose           purpose,
                     const PackageSourceConfig&        sources,
                     bool                              locked,
                     GitResolutionMode                 git,
                     const TargetInfo*                 target,
                     ToolResolver&                     tool_resolver,
                     const ResolvedProcessEnvironment& environment) -> Result<ProjectResolution> {
    auto lock_session        = rstd_try(load_lock_session(selection.root.as_path(), locked, git));
    auto resolution          = lock_session.take_resolution_options();
    resolution.sources       = sources.clone();
    auto external_resolution = resolution.clone();
    auto project             = rstd_try(resolve_package_selection_with_environment(
        selection, purpose, rstd::move(resolution), target, tool_resolver, environment));
    rstd_try(prepare_external_dependency_sources(
        project.graph, rstd::move(external_resolution), tool_resolver, environment));
    auto lock = rstd_try(sync_lock(project.graph, rstd::move(lock_session)));
    return Ok(ProjectResolution {
        .selection = rstd::move(project),
        .lock      = lock,
    });
}

} // namespace lito

export namespace lito
{

auto resolve_project_metadata(const PackageSelection&           selection,
                              const BuildConfiguration&         configuration,
                              const BuildProfileName&           profile,
                              const PackageSourceConfig&        sources,
                              const PkgConfigProviderConfig&    pkg_config,
                              const CMakeProviderConfig&        cmake,
                              const ClangToolchain&             toolchain,
                              ToolResolver&                     tool_resolver,
                              const ResolvedProcessEnvironment& environment,
                              bool                              locked,
                              PackageSelectionPurpose purpose = PackageSelectionPurpose::All)
    -> Result<PackageMetadata> {
    auto build_arguments =
        rstd_try(parse_build_arguments(configuration, toolchain.argument_parser()));
    auto host     = rstd_try(detect_host_info());
    auto platform = rstd_try(resolve_build_platform(
        host, toolchain.target_info(), explicit_cpp_target(build_arguments)));
    auto resolved = rstd_try(resolve_project(selection,
                                             purpose,
                                             sources,
                                             locked,
                                             GitResolutionMode::ReuseLocked,
                                             rstd::addressof(platform.effective_target),
                                             tool_resolver,
                                             environment));
    auto project  = rstd::move(resolved.selection);
    auto resolved_configuration                 = configuration.clone();
    resolved_configuration.toolchain.compiler   = PathBuf::from(toolchain.compiler_path());
    resolved_configuration.toolchain.c_compiler = PathBuf::from(toolchain.c_compiler_path());
    resolved_configuration.toolchain.linker     = PathBuf::from(toolchain.linker_path());
    resolved_configuration.toolchain.archiver   = PathBuf::from(toolchain.archiver_path());
    return adapt_package_graph_metadata(rstd::move(project.graph),
                                        project.selected_package_names,
                                        project.selected_targets,
                                        resolved_configuration,
                                        profile,
                                        pkg_config,
                                        cmake,
                                        platform,
                                        rstd::move(build_arguments),
                                        toolchain.argument_parser(),
                                        tool_resolver,
                                        environment);
}

auto update_dependencies(const UpdateRequest& request) -> Result<LockStatus> {
    if (request.root.is_empty()) {
        return Err(Error::make(ErrorKind::InvalidRequest, "update directory is required"_str));
    }
    auto selection = PackageSelection {
        .root = request.root.clone(),
    };
    auto environment   = rstd_try(ResolvedProcessEnvironment::resolve(request.environment));
    auto tool_resolver = ToolResolver(environment);
    auto resolved      = rstd_try(resolve_project(selection,
                                                  PackageSelectionPurpose::All,
                                                  request.sources,
                                                  false,
                                                  GitResolutionMode::Refresh,
                                                  nullptr,
                                                  tool_resolver,
                                                  environment));
    return Ok(resolved.lock);
}

} // namespace lito
