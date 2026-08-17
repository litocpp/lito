export module lito.core:config.project;

import rstd;
import lito.system;
import :config.error;
import :config.toolchain;
import :source.config;
import :dependency.cmake;
import :dependency.pkg_config;
import :lock.config;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;
using namespace lito::system;

export namespace lito::config
{

enum class ConfigLoadMode
{
    Enabled,
    LocalDisabled,
    Disabled = LocalDisabled,
};

struct InstallConfig {
    Option<PathBuf> root;
};

struct DocConfig {
    Option<PathBuf> litodoc_path;
};

struct ProjectConfig {
    PathBuf                                   root;
    lito::lock::LockConfig                    lock;
    ProcessEnvironmentSpec                    environment;
    ToolchainSpec                             toolchain;
    StandardLibrary                           standard_library { StandardLibrary::Libcxx };
    Vec<String>                               build_options;
    lito::source::PackageSourceConfig         sources;
    lito::dependency::PkgConfigProviderConfig pkg_config;
    lito::dependency::CMakeProviderConfig     cmake;
    InstallConfig                             install;
    DocConfig                                 doc;
};

struct ProjectConfigRequest {
    ConfigLoadMode    mode { ConfigLoadMode::Enabled };
    Vec<String>       overrides;
    ToolchainOverride toolchain;
    Option<String>    toolchain_standard_library;
};

} // namespace lito::config
