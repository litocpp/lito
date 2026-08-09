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

using namespace rstd::prelude;

export namespace lito
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

} // namespace lito
