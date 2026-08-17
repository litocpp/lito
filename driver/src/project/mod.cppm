module;
#include <rstd/macro.hpp>

module lito.driver:project;

import rstd;
import lito.core;
import :project.error;
import lito.cpp;
import :build.event;
import :build.layout;
import :dependency.catalog;
import :dependency.preparation;
import :dependency.external_source;
import lito.toolchain;
import lito.system;

using namespace rstd::prelude;
using namespace lito::system;
using namespace rstd::literals;

namespace lito
{

struct ProjectResolution {
    lito::package::ResolvedPackageSelection selection;
    lito::lock::LockStatus                  lock;
    PreparedExternalDependencySources       external_sources;
};

} // namespace lito

namespace lito
{

struct StartedProjectResolution {
    lito::package::ResolvedPackageSelection selection;
    lito::lock::LockSession                 lock;
    lito::source::SourceResolutionOptions   external;
};

auto observer_value(const Option<BuildEventSink>& observer) -> BuildEventSink {
    return observer.is_some() ? *observer : BuildEventSink {};
}

auto start_project_resolution(const lito::package::PackageSelection&    selection,
                              lito::package::PackageSelectionPurpose    purpose,
                              const lito::source::PackageSourceConfig&  sources,
                              const lito::lock::LockConfig&             lock,
                              bool                                      locked,
                              lito::source::GitResolutionMode           git,
                              const TargetInfo*                         target,
                              ToolResolver&                             tool_resolver,
                              const ResolvedProcessEnvironment&         environment,
                              usize                                     jobs     = usize(1),
                              BuildEventSink                            observer = {},
                              Option<lito::workspace::WorkspaceCatalog> catalog  = None())
    -> ProjectResult<StartedProjectResolution> {
    auto lock_session =
        rstd_try(lito::lock::load_lock_session(selection.root.as_path(), lock, locked, git));
    auto resolution          = lock_session.take_resolution_options();
    resolution.sources       = sources.clone();
    auto external_resolution = resolution.clone();
    auto project             = rstd_try(
        lito::package::resolve_package_selection_with_environment(selection,
                                                                  purpose,
                                                                  rstd::move(resolution),
                                                                  target,
                                                                  tool_resolver,
                                                                  environment,
                                                                  jobs,
                                                                  source_observer(observer),
                                                                  rstd::move(catalog)));
    return Ok(StartedProjectResolution {
        .selection = rstd::move(project),
        .lock      = rstd::move(lock_session),
        .external  = rstd::move(external_resolution),
    });
}

auto resolve_project(const lito::package::PackageSelection&         selection,
                     lito::package::PackageSelectionPurpose         purpose,
                     const lito::source::PackageSourceConfig&       sources,
                     const lito::lock::LockConfig&                  lock_config,
                     bool                                           locked,
                     lito::source::GitResolutionMode                git,
                     const TargetInfo*                              target,
                     ToolResolver&                                  tool_resolver,
                     const ResolvedProcessEnvironment&              environment,
                     const lito::dependency::CMakeBuildOverrideSet& cmake_build_overrides,
                     usize                                          jobs     = usize(1),
                     BuildEventSink                                 observer = {},
                     Option<lito::workspace::WorkspaceCatalog>      catalog  = None())
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
    auto declared_sources =
        rstd_try(resolve_external_dependency_sources(started.selection.graph,
                                                     rstd::move(started.external),
                                                     tool_resolver,
                                                     environment,
                                                     observer));
    auto lock = rstd_try(lito::lock::sync_lock(started.selection.graph, rstd::move(started.lock)));
    auto external_sources =
        rstd_try(prepare_external_dependency_sources(started.selection.graph,
                                                     started.selection.selected_package_names,
                                                     rstd::move(declared_sources),
                                                     cmake_build_overrides,
                                                     tool_resolver,
                                                     environment,
                                                     jobs,
                                                     observer));
    return Ok(ProjectResolution {
        .selection        = rstd::move(started.selection),
        .lock             = lock,
        .external_sources = rstd::move(external_sources),
    });
}

} // namespace lito

namespace lito
{

auto resolve_project_selection(const lito::package::PackageSelection&   selection,
                               lito::package::PackageSelectionPurpose   purpose,
                               const lito::source::PackageSourceConfig& sources,
                               const lito::lock::LockConfig&            lock,
                               bool                                     locked,
                               ToolResolver&                            tool_resolver,
                               const ResolvedProcessEnvironment&        environment,
                               usize                                    jobs     = usize(1),
                               const Option<BuildEventSink>&            observer = None())
    -> ProjectResult<lito::package::ResolvedPackageSelection> {
    auto started = start_project_resolution(selection,
                                            purpose,
                                            sources,
                                            lock,
                                            locked,
                                            lito::source::GitResolutionMode::ReuseLocked,
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
    BuildPlatform        platform;
    BuildLayout          layout;
    cpp::PackageMetadata metadata;
    ExternalAssetCatalog external_assets;
};

struct ResolvedProjectMetadata {
    BuildPlatform        platform;
    BuildLayout          layout;
    cpp::PackageMetadata metadata;
    ExternalAssetCatalog external_assets;
};

struct ResolvedProjectSession {
    ProjectResolution     project;
    cpp::CppArgumentLayer build_arguments;
    BuildPlatform         platform;
};

auto resolve_project_session(const lito::package::PackageSelection&         selection,
                             const cpp::BuildConfiguration&                 configuration,
                             const lito::source::PackageSourceConfig&       sources,
                             const lito::lock::LockConfig&                  lock,
                             const ClangToolchain&                          toolchain,
                             ToolResolver&                                  tool_resolver,
                             const ResolvedProcessEnvironment&              environment,
                             const lito::dependency::CMakeBuildOverrideSet& cmake_build_overrides,
                             bool                                           locked,
                             lito::package::PackageSelectionPurpose         purpose,
                             usize                                          jobs,
                             const Option<BuildEventSink>&                  observer = None(),
                             Option<lito::workspace::WorkspaceCatalog>      catalog  = None())
    -> ProjectResult<ResolvedProjectSession> {
    auto build_arguments =
        rstd_try(parse_build_arguments(configuration, toolchain.argument_parser()));
    auto host     = rstd_try(detect_host_info());
    auto platform = rstd_try(resolve_build_platform(
        host, toolchain.target_info(), cpp::explicit_cpp_target(build_arguments)));
    auto project  = rstd_try(resolve_project(selection,
                                             purpose,
                                             sources,
                                             lock,
                                             locked,
                                             lito::source::GitResolutionMode::ReuseLocked,
                                             rstd::addressof(platform.effective_target),
                                             tool_resolver,
                                             environment,
                                             cmake_build_overrides,
                                             jobs,
                                             observer_value(observer),
                                             rstd::move(catalog)));
    return Ok(ResolvedProjectSession {
        .project         = rstd::move(project),
        .build_arguments = rstd::move(build_arguments),
        .platform        = rstd::move(platform),
    });
}

auto resolve_project_metadata(ResolvedProjectSession                           session,
                              const cpp::BuildConfiguration&                   configuration,
                              const lito::manifest::BuildProfileName&          profile,
                              ref<rstd::path::Path>                            requested_output,
                              const lito::source::PackageSourceConfig&         sources,
                              const lito::dependency::PkgConfigProviderConfig& pkg_config,
                              const lito::dependency::CMakeProviderConfig&     cmake,
                              const ClangToolchain&                            toolchain,
                              ToolResolver&                                    tool_resolver,
                              const ResolvedProcessEnvironment&                environment,
                              usize                                            jobs,
                              const Option<BuildEventSink>&                    observer = None())
    -> ProjectResult<ResolvedProjectMetadata> {
    auto external_sources                = rstd::move(session.project.external_sources);
    auto project                         = rstd::move(session.project.selection);
    auto resolved_configuration          = configuration.clone();
    resolved_configuration.toolchain.cc  = PathBuf::from(toolchain.cc_path());
    resolved_configuration.toolchain.cxx = PathBuf::from(toolchain.cxx_path());
    resolved_configuration.toolchain.ld  = PathBuf::from(toolchain.ld_path());
    resolved_configuration.toolchain.ar  = PathBuf::from(toolchain.ar_path());
    auto resolved_profile = rstd_try(cpp::make_profile_spec(resolved_configuration,
                                                            project.graph.profile,
                                                            profile,
                                                            rstd::move(session.build_arguments)));
    auto standard_library =
        toolchain.resolve_standard_library(resolved_profile.cpp, session.platform.effective_target);
    if (standard_library.is_err()) {
        return Err(rstd::into<ProjectError>(rstd::move(standard_library).unwrap_err()));
    }
    resolved_profile.cpp.abi.resolved_standard_library =
        Some(rstd::move(standard_library).unwrap());
    auto layout = BuildLayout::create(
        project.graph.root_directory.as_path(), requested_output, resolved_profile.name.as_str());
    if (layout.is_err()) {
        return Err(rstd::into<ProjectError>(rstd::move(layout).unwrap_err()));
    }
    auto external_usage = rstd_try(resolve_external_usage_catalog(project.graph,
                                                                  project.selected_package_names,
                                                                  external_sources,
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
    auto metadata       = cpp::adapt_package_graph_metadata(rstd::move(project.graph),
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
        .platform        = rstd::move(session.platform),
        .layout          = rstd::move(layout).unwrap(),
        .metadata        = rstd::move(metadata).unwrap(),
        .external_assets = rstd::move(assets),
    });
}

auto prepare_resolved_build_project(ResolvedProjectSession                   session,
                                    const cpp::BuildConfiguration&           configuration,
                                    const lito::manifest::BuildProfileName&  profile,
                                    ref<rstd::path::Path>                    requested_output,
                                    const lito::source::PackageSourceConfig& sources,
                                    const lito::dependency::PkgConfigProviderConfig& pkg_config,
                                    const lito::dependency::CMakeProviderConfig&     cmake,
                                    ClangToolchain                                   toolchain,
                                    ToolResolver&                                    tool_resolver,
                                    const ResolvedProcessEnvironment&                environment,
                                    usize                                            jobs,
                                    const Option<BuildEventSink>& observer = None())
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
        .platform        = rstd::move(resolved.platform),
        .layout          = rstd::move(resolved.layout),
        .metadata        = rstd::move(resolved.metadata),
        .external_assets = rstd::move(resolved.external_assets),
    });
}

auto resolve_project_metadata(
    const lito::package::PackageSelection&           selection,
    const cpp::BuildConfiguration&                   configuration,
    const lito::manifest::BuildProfileName&          profile,
    ref<rstd::path::Path>                            requested_output,
    const lito::source::PackageSourceConfig&         sources,
    const lito::lock::LockConfig&                    lock,
    const lito::dependency::PkgConfigProviderConfig& pkg_config,
    const lito::dependency::CMakeProviderConfig&     cmake,
    const lito::dependency::CMakeBuildOverrideSet&   cmake_build_overrides,
    const ClangToolchain&                            toolchain,
    ToolResolver&                                    tool_resolver,
    const ResolvedProcessEnvironment&                environment,
    bool                                             locked,
    lito::package::PackageSelectionPurpose    purpose = lito::package::PackageSelectionPurpose::All,
    usize                                     jobs    = usize(1),
    const Option<BuildEventSink>&             observer = None(),
    Option<lito::workspace::WorkspaceCatalog> catalog  = None())
    -> ProjectResult<ResolvedProjectMetadata> {
    auto session = rstd_try(resolve_project_session(selection,
                                                    configuration,
                                                    sources,
                                                    lock,
                                                    toolchain,
                                                    tool_resolver,
                                                    environment,
                                                    cmake_build_overrides,
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

auto prepare_build_project(
    const lito::package::PackageSelection&           selection,
    const cpp::BuildConfiguration&                   configuration,
    const lito::manifest::BuildProfileName&          profile,
    ref<rstd::path::Path>                            requested_output,
    const lito::source::PackageSourceConfig&         sources,
    const lito::lock::LockConfig&                    lock,
    const lito::dependency::PkgConfigProviderConfig& pkg_config,
    const lito::dependency::CMakeProviderConfig&     cmake,
    const lito::dependency::CMakeBuildOverrideSet&   cmake_build_overrides,
    ToolResolver&                                    tool_resolver,
    const ResolvedProcessEnvironment&                environment,
    bool                                             locked,
    lito::package::PackageSelectionPurpose    purpose = lito::package::PackageSelectionPurpose::All,
    usize                                     jobs    = usize(1),
    const Option<BuildEventSink>&             observer = None(),
    Option<lito::workspace::WorkspaceCatalog> catalog  = None())
    -> ProjectResult<PreparedBuildProject> {
    auto created = ClangToolchain::create(configuration.toolchain, tool_resolver, environment);
    if (created.is_err()) {
        return Err(rstd::into<ProjectError>(rstd::move(created).unwrap_err()));
    }
    auto toolchain = rstd::move(created).unwrap();
    emit_build_toolchain(observer, toolchain);
    auto metadata = resolve_project_metadata(selection,
                                             configuration,
                                             profile,
                                             requested_output,
                                             sources,
                                             lock,
                                             pkg_config,
                                             cmake,
                                             cmake_build_overrides,
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
        .platform        = rstd::move(resolved_metadata.platform),
        .layout          = rstd::move(resolved_metadata.layout),
        .metadata        = rstd::move(resolved_metadata.metadata),
        .external_assets = rstd::move(resolved_metadata.external_assets),
    });
}

auto update_project_dependencies(ref<rstd::path::Path>                    root,
                                 const ProcessEnvironmentSpec&            environment_spec,
                                 const lito::lock::LockConfig&            lock,
                                 const lito::source::PackageSourceConfig& sources,
                                 const Option<BuildEventSink>&            observer)
    -> ProjectResult<lito::lock::LockStatus> {
    if (root.is_empty()) {
        return Err(ProjectError::Message(String::make("update directory is required"_str)));
    }
    auto selection = lito::package::PackageSelection {
        .root = PathBuf::from(root),
    };
    auto environment   = rstd_try(ResolvedProcessEnvironment::resolve(environment_spec));
    auto tool_resolver = ToolResolver(environment);
    auto jobs          = usize(1);
    auto available     = rstd::thread::available_parallelism();
    if (available.is_ok()) jobs = available->get();
    auto resolved = rstd_try(resolve_project(selection,
                                             lito::package::PackageSelectionPurpose::All,
                                             sources,
                                             lock,
                                             false,
                                             lito::source::GitResolutionMode::Refresh,
                                             nullptr,
                                             tool_resolver,
                                             environment,
                                             lito::dependency::CMakeBuildOverrideSet {},
                                             jobs,
                                             observer_value(observer)));
    return Ok(resolved.lock);
}

} // namespace lito
