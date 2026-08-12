export module lito.config.contract;

import rstd;
import lito.error;
import lito.system.environment_contract;
import lito.toolchain.spec;
import lito.source.contract;
import lito.dependency.contract;

export namespace lito
{

struct ProjectConfig {
    PathBuf                 root;
    ProcessEnvironmentSpec  environment;
    ToolchainSpec           toolchain;
    PackageSourceConfig     sources;
    PkgConfigProviderConfig pkg_config;
    CMakeProviderConfig     cmake;
};

} // namespace lito
