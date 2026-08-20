export module lito.driver:install.request;

import rstd;
import lito.core;
import :build.request;
import :install.destination;
import :install.source;

using namespace rstd::prelude;

export namespace lito
{

enum class InstallBuildMode
{
    Build,
    ReuseCompleted,
};

struct InstallRequest {
    ResolvedInstallSource source;
    BuildRequest          build;
    InstallDestination    destination;
    Vec<String>           binaries;
    InstallBuildMode      build_mode { InstallBuildMode::Build };
    bool                  force { false };
};

} // namespace lito
