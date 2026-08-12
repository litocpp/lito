export module lito.config.contract;

import rstd;
import lito.error;
import lito.system.environment_contract;
import lito.toolchain.spec;
import lito.source.contract;
import lito.dependency.contract;
import lito.lock.contract;

export namespace lito
{

enum class ConfigLoadMode
{
    Enabled,
    Disabled,
};

struct ProjectConfig {
    PathBuf                 root;
    LockConfig              lock;
    ProcessEnvironmentSpec  environment;
    ToolchainSpec           toolchain;
    PackageSourceConfig     sources;
    PkgConfigProviderConfig pkg_config;
    CMakeProviderConfig     cmake;
};

} // namespace lito
