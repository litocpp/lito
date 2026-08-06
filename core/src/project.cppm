module;
#include <rstd/macro.hpp>

export module tenon.project;

import rstd;
import tenon.model;
import tenon.workspace_resolver;
import tenon.lock_store;
import tenon.package;
import tenon.dependency;
import tenon.toolchain;

using namespace rstd::prelude;

export namespace tenon
{

auto resolve_project_metadata(const PackageSelection&        selection,
                              const BuildConfiguration&      configuration,
                              const PackageSourceConfig&     sources,
                              const PkgConfigProviderConfig& pkg_config,
                              const CMakeProviderConfig&     cmake,
                              const ClangToolchain&          toolchain,
                              bool                           locked,
                              PackageSelectionPurpose        purpose = PackageSelectionPurpose::All)
    -> Result<PackageMetadata> {
    auto lock_session        = rstd_try(load_lock_session(selection.root.as_path(), locked));
    auto resolution          = lock_session.take_resolution_options();
    resolution.sources       = sources.clone();
    auto external_resolution = resolution.clone();
    auto project             = rstd_try(resolve_package_selection(
        selection, purpose, rstd::move(resolution), rstd::addressof(toolchain.target_info())));
    rstd_try(resolve_external_dependency_sources(project.graph, rstd::move(external_resolution)));
    rstd_try(sync_lock(project.graph, rstd::move(lock_session)));
    auto resolved_configuration               = configuration.clone();
    resolved_configuration.toolchain.compiler = PathBuf::from(toolchain.compiler_path());
    resolved_configuration.toolchain.archiver = PathBuf::from(toolchain.archiver_path());
    return adapt_package_graph_metadata(rstd::move(project.graph),
                                        project.selected_package_names,
                                        project.selected_root_names,
                                        resolved_configuration,
                                        pkg_config,
                                        cmake,
                                        toolchain.target_info(),
                                        toolchain.argument_parser());
}

} // namespace tenon
