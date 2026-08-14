export module lito.config.contract;

import rstd;
import lito.error;
import lito.system.environment_contract;
import lito.toolchain.spec;
import lito.source.contract;
import lito.dependency.contract;
import lito.lock.contract;
export import lito.config.error_contract;

using namespace rstd::prelude;

export namespace lito
{

enum class ConfigLoadMode
{
    Enabled,
    Disabled,
};

struct InstallConfig {
    Option<PathBuf> root;
};

struct ProjectConfig {
    PathBuf                 root;
    LockConfig              lock;
    ProcessEnvironmentSpec  environment;
    ToolchainSpec           toolchain;
    PackageSourceConfig     sources;
    PkgConfigProviderConfig pkg_config;
    CMakeProviderConfig     cmake;
    InstallConfig           install;
};

struct ProjectConfigRequest {
    ConfigLoadMode    mode { ConfigLoadMode::Enabled };
    Vec<String>       overrides;
    ToolchainOverride toolchain;
};

struct ConfigQuery {
    PathBuf path;
    String  output;
};

struct ConfigMutation {
    PathBuf path;
    String  key;
};

} // namespace lito
