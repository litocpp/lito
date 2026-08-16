export module lito.driver:install.store;

import :install.error;
import :install.destination;
import :install.store.model;

export namespace lito
{

auto create_install_layout(InstallRoot root) -> InstallStoreResult<InstallLayout>;
auto install_artifacts(InstallStoreRequest request) -> InstallStoreResult<InstallStoreSummary>;

} // namespace lito
