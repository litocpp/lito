export module lito.driver:config.project;

import rstd;
import lito.core;
import lito.cpp;
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

struct BuildConfigurationRequest {
    ToolchainSpec            toolchain;
    StandardLibrarySelection standard_library { StandardLibrarySelection::Auto };
    StandardLibraryRuntime   standard_library_runtime { StandardLibraryRuntime::Dynamic };
    lito::cpp::BmiMode       bmi_mode { lito::cpp::BmiMode::Reduced };
    lito::cpp::BmiSourceEmbeddingPolicy bmi_source_embedding {
        lito::cpp::BmiSourceEmbeddingPolicy::ExternalSources
    };
    String                      language_standard;
    Option<manifest::CStandard> c_standard;
    ProjectBuildOptions         global_options;
    BuildTargetRequest          target;

    auto clone() const -> BuildConfigurationRequest {
        return BuildConfigurationRequest {
            .toolchain                = toolchain.clone(),
            .standard_library         = standard_library,
            .standard_library_runtime = standard_library_runtime,
            .bmi_mode                 = bmi_mode,
            .bmi_source_embedding     = bmi_source_embedding,
            .language_standard        = language_standard.clone(),
            .c_standard               = c_standard,
            .global_options           = global_options.clone(),
            .target                   = as<Clone>(target).clone(),
        };
    }
};

auto build_configuration_request(lito::cpp::BuildConfiguration configuration)
    -> BuildConfigurationRequest {
    return BuildConfigurationRequest {
        .toolchain                = rstd::move(configuration.toolchain),
        .standard_library         = standard_library_selection(configuration.standard_library),
        .standard_library_runtime = configuration.standard_library_runtime,
        .bmi_mode                 = configuration.bmi_mode,
        .bmi_source_embedding     = configuration.bmi_source_embedding,
        .language_standard        = rstd::move(configuration.language_standard),
        .c_standard               = configuration.c_standard,
        .global_options           = rstd::move(configuration.global_options),
        .target                   = rstd::move(configuration.target),
    };
}

auto resolve_build_configuration(BuildConfigurationRequest request,
                                 StandardLibrary           standard_library)
    -> lito::cpp::BuildConfiguration {
    return lito::cpp::BuildConfiguration {
        .toolchain                = rstd::move(request.toolchain),
        .standard_library         = standard_library,
        .standard_library_runtime = request.standard_library_runtime,
        .bmi_mode                 = request.bmi_mode,
        .bmi_source_embedding     = request.bmi_source_embedding,
        .language_standard        = rstd::move(request.language_standard),
        .c_standard               = request.c_standard,
        .global_options           = rstd::move(request.global_options),
        .target                   = rstd::move(request.target),
    };
}

struct ProjectConfig {
    PathBuf                           root;
    lito::lock::LockConfig            lock;
    ProcessEnvironmentSpec            environment;
    lito::tools::ToolSpec             tools;
    ToolchainSpec                     toolchain;
    StandardLibrarySelection          standard_library { StandardLibrarySelection::Auto };
    StandardLibraryRuntime            standard_library_runtime { StandardLibraryRuntime::Dynamic };
    ProjectBuildOptions               build_options;
    BuildTargetRequest                build_target;
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
