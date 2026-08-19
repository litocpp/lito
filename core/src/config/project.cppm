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

struct BuildOptionInput {
    Vec<String> arguments;
    String      source;

    auto clone() const -> BuildOptionInput {
        return BuildOptionInput {
            .arguments = arguments.clone(),
            .source    = source.clone(),
        };
    }
};

struct ProjectBuildOptions {
    Vec<BuildOptionInput> cpp;
    Vec<BuildOptionInput> c;
    Vec<BuildOptionInput> linker;

    auto clone() const -> ProjectBuildOptions {
        auto clone_inputs = [](const Vec<BuildOptionInput>& inputs) {
            auto result = Vec<BuildOptionInput>::with_capacity(inputs.len());
            for (const auto& input : inputs) result.push(input.clone());
            return result;
        };
        return ProjectBuildOptions {
            .cpp    = clone_inputs(cpp),
            .c      = clone_inputs(c),
            .linker = clone_inputs(linker),
        };
    }
};

enum class EnvironmentFlagPolicy
{
    Ignore,
    Append,
};

struct ProjectConfig {
    PathBuf                                   root;
    lito::lock::LockConfig                    lock;
    ProcessEnvironmentSpec                    environment;
    ToolSpec                                  tools;
    ToolchainSpec                             toolchain;
    StandardLibrary                           standard_library { StandardLibrary::Libcxx };
    ProjectBuildOptions                       build_options;
    lito::source::PackageSourceConfig         sources;
    lito::dependency::PkgConfigProviderConfig pkg_config;
    lito::dependency::CMakeProviderConfig     cmake;
    lito::dependency::CMakeBuildOverrideSet   cmake_build_overrides;
    InstallConfig                             install;
    DocConfig                                 doc;
};

struct ProjectConfigRequest {
    ConfigLoadMode        mode { ConfigLoadMode::Enabled };
    Vec<String>           overrides;
    EnvironmentFlagPolicy environment_flags { EnvironmentFlagPolicy::Ignore };
};

} // namespace lito::config
