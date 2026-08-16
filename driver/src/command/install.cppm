export module lito.driver:command.install;

import :install;

export namespace lito
{

auto install(InstallRequest request) -> InstallResult<InstallSummary>;

} // namespace lito
