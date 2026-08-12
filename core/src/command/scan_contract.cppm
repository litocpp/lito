export module lito.command.scan_contract;

import rstd;
import lito.error;
import lito.frontend;
import lito.build.profile_contract;
import lito.build.configuration;
import lito.build.contract;
import lito.system.environment_contract;
import lito.source.contract;
import lito.dependency.contract;
import lito.workspace.contract;

using namespace rstd::prelude;

export namespace lito
{

struct ScanRequest {
    PackageSelection         selection;
    Vec<String>              targets;
    PathBuf                  source;
    ProcessEnvironmentSpec   environment;
    BuildConfiguration       configuration;
    Option<BuildProfileName> profile;
    PackageSourceConfig      sources;
    PkgConfigProviderConfig  pkg_config;
    CMakeProviderConfig      cmake;
    bool                     locked { false };
    Option<BuildObserver>    observer;
};

struct ScanReport {
    String                   target;
    String                   profile;
    PathBuf                  primary_output;
    frontend::FrontendResult result;
};

} // namespace lito
