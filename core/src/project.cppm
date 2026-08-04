module;
#include <rstd/macro.hpp>

export module tenon.project;

import rstd;
import tenon.model;
import tenon.workspace_resolver;
import tenon.lock_store;
import tenon.package;

using namespace rstd::prelude;

export namespace tenon
{

auto resolve_project_metadata(const PackageSelection&        selection,
                              const BuildConfiguration&      configuration,
                              const PackageSourceConfig&     sources,
                              const PkgConfigProviderConfig& pkg_config,
                              const TargetInfo&              target_info,
                              const CppArgumentParser&       argument_parser,
                              bool                           locked,
                              PackageSelectionPurpose        purpose = PackageSelectionPurpose::All)
    -> Result<PackageMetadata> {
    auto lock_session  = rstd_try(load_lock_session(selection.root.as_path(), locked));
    auto resolution    = lock_session.take_resolution_options();
    resolution.sources = sources.clone();
    auto project       = rstd_try(resolve_package_selection(
        selection, purpose, rstd::move(resolution), rstd::addressof(target_info)));
    rstd_try(sync_lock(project.graph, rstd::move(lock_session)));
    return adapt_package_graph_metadata(rstd::move(project.graph),
                                        project.selected_package_names,
                                        project.selected_root_names,
                                        configuration,
                                        pkg_config,
                                        target_info,
                                        argument_parser);
}

} // namespace tenon
