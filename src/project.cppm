export module tenon.project;

import rstd;
import tenon.model;
import tenon.workspace_resolver;
import tenon.lock_store;
import tenon.package;

using namespace rstd::prelude;

export namespace tenon {

auto resolve_project_metadata(const PackageSelection &selection,
                              const BuildConfiguration &configuration,
                              const PackageSourceConfig &sources, bool locked,
                              PackageSelectionPurpose purpose =
                                  PackageSelectionPurpose::All)
    -> Result<PackageMetadata> {
  auto lock = load_lock_session(selection.root.as_path(), locked);
  if (lock.is_err())
    return Err(rstd::move(lock).unwrap_err());
  auto lock_session = rstd::move(lock).unwrap();
  auto resolution = lock_session.take_resolution_options();
  resolution.sources = sources.clone();
  auto resolved =
      resolve_package_selection(selection, purpose, rstd::move(resolution));
  if (resolved.is_err())
    return Err(rstd::move(resolved).unwrap_err());
  auto project = rstd::move(resolved).unwrap();
  auto synchronized = sync_lock(project.graph, rstd::move(lock_session));
  if (synchronized.is_err()) {
    return Err(rstd::move(synchronized).unwrap_err());
  }
  return adapt_package_graph_metadata(
      rstd::move(project.graph), project.selected_package_names,
      project.selected_root_names, configuration);
}

} // namespace tenon
