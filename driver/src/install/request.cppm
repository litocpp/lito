export module lito.driver:install.request;

import rstd;
import lito.core;
import :build.request;
import :install.destination;
import :install.source;

using namespace rstd::prelude;

export namespace lito
{

struct InstallRequest {
    ResolvedInstallSource source;
    BuildRequest          build;
    InstallDestination    destination;
    Vec<String>           binaries;
    bool                  force { false };
};

} // namespace lito
