module;
#include <rstd/macro.hpp>

export module lito.project;

import rstd;
import lito.error;
import lito.project.error_contract;
import lito.cpp;
import lito.command.project_contract;
import lito.source.contract;
import lito.build.configuration;
import lito.build.profile_contract;
import lito.build.contract;
import lito.build.layout;
import lito.lock.contract;
import lito.package.graph_contract;
import lito.package.target_contract;
import lito.workspace.contract;
import lito.workspace;
import lito.workspace.resolver;
import lito.lock;
import lito.package;
import lito.dependency;
import lito.toolchain;
import lito.system.environment;
import lito.platform;
import lito.build.profile;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito
{

struct ProjectResolution {
    ResolvedPackageSelection selection;
    LockStatus               lock;
};

} // namespace lito

namespace lito
{

struct StartedProjectResolution {
    ResolvedPackageSelection selection;
    LockSession              lock;
    PackageResolutionOptions external;
};

auto observer_value(const Option<BuildObserver>& observer) -> BuildObserver {
    return observer.is_some() ? *observer : BuildObserver {};
}

auto start_project_resolution(const PackageSelection&           selection,
                              PackageSelectionPurpose           purpose,
                              const PackageSourceConfig&        sources,
                              const LockConfig&                 lock,
                              bool                              locked,
                              GitResolutionMode                 git,
                              const TargetInfo*                 target,
                              ToolResolver&                     tool_resolver,
                              const ResolvedProcessEnvironment& environment,
                              usize                             jobs     = usize(1),
                              BuildObserver                     observer = {},
                              Option<WorkspaceCatalog>          catalog  = None())
    -> ProjectResult<StartedProjectResolution> {
    auto lock_session  = rstd_try(load_lock_session(selection.root.as_path(), lock, locked, git));
    auto resolution    = lock_session.take_resolution_options();
    resolution.sources = sources.clone();
    auto external_resolution = resolution.clone();
    auto project = rstd_try(resolve_package_selection_with_environment(selection,
                                                                       purpose,
                                                                       rstd::move(resolution),
                                                                       target,
                                                                       tool_resolver,
                                                                       environment,
                                                                       jobs,
                                                                       observer,
                                                                       rstd::move(catalog)));
    return Ok(StartedProjectResolution {
        .selection = rstd::move(project),
        .lock      = rstd::move(lock_session),
        .external  = rstd::move(external_resolution),
    });
}

auto resolve_project(const PackageSelection&           selection,
                     PackageSelectionPurpose           purpose,
                     const PackageSourceConfig&        sources,
                     const LockConfig&                 lock_config,
                     bool                              locked,
                     GitResolutionMode                 git,
                     const TargetInfo*                 target,
                     ToolResolver&                     tool_resolver,
                     const ResolvedProcessEnvironment& environment,
                     usize                             jobs     = usize(1),
                     BuildObserver                     observer = {},
                     Option<WorkspaceCatalog>          catalog  = None())
    -> ProjectResult<ProjectResolution> {
    auto started = rstd_try(start_project_resolution(selection,
                                                     purpose,
                                                     sources,
                                                     lock_config,
                                                     locked,
                                                     git,
                                                     target,
                                                     tool_resolver,
                                                     environment,
                                                     jobs,
                                                     observer,
                                                     rstd::move(catalog)));
    rstd_try(prepare_external_dependency_sources(started.selection.graph,
                                                 started.selection.selected_package_names,
                                                 rstd::move(started.external),
                                                 tool_resolver,
                                                 environment,
                                                 jobs,
                                                 observer));
    auto lock = rstd_try(sync_lock(started.selection.graph, rstd::move(started.lock)));
    return Ok(ProjectResolution {
        .selection = rstd::move(started.selection),
        .lock      = lock,
    });
}

} // namespace lito

export namespace lito
{

auto resolve_project_selection(const PackageSelection&           selection,
                               PackageSelectionPurpose           purpose,
                               const PackageSourceConfig&        sources,
                               const LockConfig&                 lock,
                               bool                              locked,
                               ToolResolver&                     tool_resolver,
                               const ResolvedProcessEnvironment& environment,
                               usize                             jobs     = usize(1),
                               const Option<BuildObserver>&      observer = None())
    -> ProjectResult<ResolvedPackageSelection> {
    auto started = start_project_resolution(selection,
                                            purpose,
                                            sources,
                                            lock,
                                            locked,
                                            GitResolutionMode::ReuseLocked,
                                            nullptr,
                                            tool_resolver,
                                            environment,
                                            jobs,
                                            observer_value(observer));
    if (started.is_err()) return Err(rstd::move(started).unwrap_err());
    return Ok(rstd::move(started).unwrap().selection);
}

struct PreparedBuildProject {
    ClangToolchain       toolchain;
    BuildLayout          layout;
    PackageMetadata      metadata;
    ExternalAssetCatalog external_assets;
};

struct ResolvedProjectMetadata {
    BuildLayout          layout;
    PackageMetadata      metadata;
    ExternalAssetCatalog external_assets;
};

struct ResolvedProjectSession {
    ProjectResolution project;
    CppArgumentLayer  build_arguments;
    BuildPlatform     platform;
};

auto resolve_project_session(const PackageSelection&           selection,
                             const BuildConfiguration&         configuration,
                             const PackageSourceConfig&        sources,
                             const LockConfig&                 lock,
                             const ClangToolchain&             toolchain,
                             ToolResolver&                     tool_resolver,
                             const ResolvedProcessEnvironment& environment,
                             bool                              locked,
                             PackageSelectionPurpose           purpose,
                             usize                             jobs,
                             const Option<BuildObserver>&      observer = None(),
                             Option<WorkspaceCatalog>          catalog  = None())
    -> ProjectResult<ResolvedProjectSession> {
    auto build_arguments =
        rstd_try(parse_build_arguments(configuration, toolchain.argument_parser()));
    auto host     = rstd_try(detect_host_info());
    auto platform = rstd_try(resolve_build_platform(
        host, toolchain.target_info(), explicit_cpp_target(build_arguments)));
    auto project  = rstd_try(resolve_project(selection,
                                             purpose,
                                             sources,
                                             lock,
                                             locked,
                                             GitResolutionMode::ReuseLocked,
                                             rstd::addressof(platform.effective_target),
                                             tool_resolver,
                                             environment,
                                             jobs,
                                             observer_value(observer),
                                             rstd::move(catalog)));
    return Ok(ResolvedProjectSession {
        .project         = rstd::move(project),
        .build_arguments = rstd::move(build_arguments),
        .platform        = rstd::move(platform),
    });
}

auto resolve_project_metadata(ResolvedProjectSession            session,
                              const BuildConfiguration&         configuration,
                              const BuildProfileName&           profile,
                              ref<rstd::path::Path>             requested_output,
                              const PackageSourceConfig&        sources,
                              const PkgConfigProviderConfig&    pkg_config,
                              const CMakeProviderConfig&        cmake,
                              const ClangToolchain&             toolchain,
                              ToolResolver&                     tool_resolver,
                              const ResolvedProcessEnvironment& environment,
                              usize                             jobs,
                              const Option<BuildObserver>&      observer = None())
    -> ProjectResult<ResolvedProjectMetadata> {
    auto project                                = rstd::move(session.project.selection);
    auto resolved_configuration                 = configuration.clone();
    resolved_configuration.toolchain.compiler   = PathBuf::from(toolchain.compiler_path());
    resolved_configuration.toolchain.c_compiler = PathBuf::from(toolchain.c_compiler_path());
    resolved_configuration.toolchain.linker     = PathBuf::from(toolchain.linker_path());
    resolved_configuration.toolchain.archiver   = PathBuf::from(toolchain.archiver_path());
    auto resolved_profile = rstd_try(make_profile_spec(resolved_configuration,
                                                       project.graph.profile,
                                                       profile,
                                                       rstd::move(session.build_arguments)));
    auto layout           = BuildLayout::create(
        project.graph.root_directory.as_path(), requested_output, resolved_profile.name.as_str());
    if (layout.is_err()) {
        return Err(rstd::into<ProjectError>(rstd::move(layout).unwrap_err()));
    }
    auto external_usage = rstd_try(resolve_external_usage_catalog(project.graph,
                                                                  project.selected_package_names,
                                                                  pkg_config,
                                                                  cmake,
                                                                  resolved_configuration,
                                                                  resolved_profile,
                                                                  *layout,
                                                                  session.platform,
                                                                  toolchain.argument_parser(),
                                                                  tool_resolver,
                                                                  environment,
                                                                  jobs,
                                                                  observer,
                                                                  sources));
    auto assets         = rstd::move(external_usage.assets);
    auto metadata       = adapt_package_graph_metadata(rstd::move(project.graph),
                                                       project.selected_package_names,
                                                       project.selected_targets,
                                                       resolved_configuration,
                                                       rstd::move(resolved_profile),
                                                       session.platform,
                                                       rstd::move(external_usage.usage),
                                                       toolchain.argument_parser());
    if (metadata.is_err()) {
        return Err(rstd::into<ProjectError>(rstd::move(metadata).unwrap_err()));
    }
    return Ok(ResolvedProjectMetadata {
        .layout          = rstd::move(layout).unwrap(),
        .metadata        = rstd::move(metadata).unwrap(),
        .external_assets = rstd::move(assets),
    });
}

auto prepare_resolved_build_project(ResolvedProjectSession            session,
                                    const BuildConfiguration&         configuration,
                                    const BuildProfileName&           profile,
                                    ref<rstd::path::Path>             requested_output,
                                    const PackageSourceConfig&        sources,
                                    const PkgConfigProviderConfig&    pkg_config,
                                    const CMakeProviderConfig&        cmake,
                                    ClangToolchain                    toolchain,
                                    ToolResolver&                     tool_resolver,
                                    const ResolvedProcessEnvironment& environment,
                                    usize                             jobs,
                                    const Option<BuildObserver>&      observer = None())
    -> ProjectResult<PreparedBuildProject> {
    auto metadata = resolve_project_metadata(rstd::move(session),
                                             configuration,
                                             profile,
                                             requested_output,
                                             sources,
                                             pkg_config,
                                             cmake,
                                             toolchain,
                                             tool_resolver,
                                             environment,
                                             jobs,
                                             observer);
    if (metadata.is_err()) return Err(rstd::move(metadata).unwrap_err());
    auto resolved = rstd::move(metadata).unwrap();
    return Ok(PreparedBuildProject {
        .toolchain       = rstd::move(toolchain),
        .layout          = rstd::move(resolved.layout),
        .metadata        = rstd::move(resolved.metadata),
        .external_assets = rstd::move(resolved.external_assets),
    });
}

auto resolve_project_metadata(const PackageSelection&           selection,
                              const BuildConfiguration&         configuration,
                              const BuildProfileName&           profile,
                              ref<rstd::path::Path>             requested_output,
                              const PackageSourceConfig&        sources,
                              const LockConfig&                 lock,
                              const PkgConfigProviderConfig&    pkg_config,
                              const CMakeProviderConfig&        cmake,
                              const ClangToolchain&             toolchain,
                              ToolResolver&                     tool_resolver,
                              const ResolvedProcessEnvironment& environment,
                              bool                              locked,
                              PackageSelectionPurpose      purpose  = PackageSelectionPurpose::All,
                              usize                        jobs     = usize(1),
                              const Option<BuildObserver>& observer = None(),
                              Option<WorkspaceCatalog>     catalog  = None())
    -> ProjectResult<ResolvedProjectMetadata> {
    auto session = rstd_try(resolve_project_session(selection,
                                                    configuration,
                                                    sources,
                                                    lock,
                                                    toolchain,
                                                    tool_resolver,
                                                    environment,
                                                    locked,
                                                    purpose,
                                                    jobs,
                                                    observer,
                                                    rstd::move(catalog)));
    return resolve_project_metadata(rstd::move(session),
                                    configuration,
                                    profile,
                                    requested_output,
                                    sources,
                                    pkg_config,
                                    cmake,
                                    toolchain,
                                    tool_resolver,
                                    environment,
                                    jobs,
                                    observer);
}

auto prepare_build_project(const PackageSelection&           selection,
                           const BuildConfiguration&         configuration,
                           const BuildProfileName&           profile,
                           ref<rstd::path::Path>             requested_output,
                           const PackageSourceConfig&        sources,
                           const LockConfig&                 lock,
                           const PkgConfigProviderConfig&    pkg_config,
                           const CMakeProviderConfig&        cmake,
                           ToolResolver&                     tool_resolver,
                           const ResolvedProcessEnvironment& environment,
                           bool                              locked,
                           PackageSelectionPurpose           purpose = PackageSelectionPurpose::All,
                           usize                             jobs    = usize(1),
                           const Option<BuildObserver>&      observer = None(),
                           Option<WorkspaceCatalog>          catalog  = None())
    -> ProjectResult<PreparedBuildProject> {
    auto created = ClangToolchain::create(configuration.toolchain, tool_resolver, environment);
    if (created.is_err()) {
        return Err(rstd::into<ProjectError>(rstd::move(created).unwrap_err()));
    }
    auto toolchain = rstd::move(created).unwrap();
    auto metadata  = resolve_project_metadata(selection,
                                              configuration,
                                              profile,
                                              requested_output,
                                              sources,
                                              lock,
                                              pkg_config,
                                              cmake,
                                              toolchain,
                                              tool_resolver,
                                              environment,
                                              locked,
                                              purpose,
                                              jobs,
                                              observer,
                                              rstd::move(catalog));
    if (metadata.is_err()) return Err(rstd::move(metadata).unwrap_err());
    auto resolved_metadata = rstd::move(metadata).unwrap();
    return Ok(PreparedBuildProject {
        .toolchain       = rstd::move(toolchain),
        .layout          = rstd::move(resolved_metadata.layout),
        .metadata        = rstd::move(resolved_metadata.metadata),
        .external_assets = rstd::move(resolved_metadata.external_assets),
    });
}

auto update_project_dependencies(const UpdateRequest& request) -> ProjectResult<LockStatus> {
    if (request.root.is_empty()) {
        return Err(ProjectError::Message(String::make("update directory is required"_str)));
    }
    auto selection = PackageSelection {
        .root = request.root.clone(),
    };
    auto environment   = rstd_try(ResolvedProcessEnvironment::resolve(request.environment));
    auto tool_resolver = ToolResolver(environment);
    auto jobs          = usize(1);
    auto available     = rstd::thread::available_parallelism();
    if (available.is_ok()) jobs = available->get();
    auto resolved = rstd_try(resolve_project(selection,
                                             PackageSelectionPurpose::All,
                                             request.sources,
                                             request.lock,
                                             false,
                                             GitResolutionMode::Refresh,
                                             nullptr,
                                             tool_resolver,
                                             environment,
                                             jobs,
                                             observer_value(request.observer)));
    return Ok(resolved.lock);
}

} // namespace lito
