export module lito.driver:config.project;

import rstd;
import lito.core;
import lito.tools;
import lito.system;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;
using namespace lito::system;
using namespace lito::tools;

export namespace lito::config
{

struct InstallConfig {
    Option<PathBuf> root;
};

struct DocConfig {
    Option<PathBuf> litodoc_path;
};

struct ProjectConfig {
    PathBuf                           root;
    lito::lock::LockConfig            lock;
    ProcessEnvironmentSpec            environment;
    lito::tools::ToolSpec             tools;
    ToolchainSpec                     toolchain;
    StandardLibrary                   standard_library { StandardLibrary::Libcxx };
    StandardLibraryRuntime            standard_library_runtime { StandardLibraryRuntime::Dynamic };
    ProjectBuildOptions               build_options;
    lito::source::PackageSourceConfig sources;
    lito::dependency::PkgConfigProviderConfig pkg_config;
    lito::dependency::CMakeProviderConfig     cmake;
    lito::dependency::CMakeBuildOverrideSet   cmake_build_overrides;
    InstallConfig                             install;
    DocConfig                                 doc;
};

struct ProjectConfigDefaults {
    lito::tools::ToolSpec tools;
    ToolchainSpec         toolchain;

    auto clone() const -> ProjectConfigDefaults {
        return ProjectConfigDefaults {
            .tools     = tools.clone(),
            .toolchain = toolchain.clone(),
        };
    }
};

struct ProjectConfigRequest {
    ConfigLoadMode                mode { ConfigLoadMode::Enabled };
    Vec<String>                   overrides;
    EnvironmentFlagPolicy         environment_flags { EnvironmentFlagPolicy::Ignore };
    Option<ProjectConfigDefaults> defaults;
};

struct HostToolCommandConfig {
    PathBuf                root;
    ProcessEnvironmentSpec environment;
    lito::tools::ToolSpec  tools;
    ToolchainSpec          toolchain;
};

} // namespace lito::config
