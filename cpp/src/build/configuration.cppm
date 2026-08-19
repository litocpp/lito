export module lito.cpp:build.configuration;

import rstd;
import lito.core;
import :bmi.artifact;

using namespace rstd::prelude;

export namespace lito::cpp
{

struct BuildConfiguration {
    lito::config::ToolchainSpec   toolchain;
    lito::config::StandardLibrary standard_library { lito::config::StandardLibrary::Libstdcxx };
    lito::config::StandardLibraryRuntime standard_library_runtime {
        lito::config::StandardLibraryRuntime::Dynamic
    };
    BmiMode                  bmi_mode { BmiMode::Reduced };
    BmiSourceEmbeddingPolicy bmi_source_embedding { BmiSourceEmbeddingPolicy::ExternalSources };
    String                   language_standard;
    Option<lito::manifest::CStandard> c_standard;
    lito::config::ProjectBuildOptions global_options;

    auto clone() const -> BuildConfiguration {
        return BuildConfiguration {
            .toolchain                = toolchain.clone(),
            .standard_library         = standard_library,
            .standard_library_runtime = standard_library_runtime,
            .bmi_mode                 = bmi_mode,
            .bmi_source_embedding     = bmi_source_embedding,
            .language_standard        = language_standard.clone(),
            .c_standard               = c_standard,
            .global_options           = global_options.clone(),
        };
    }
};

} // namespace lito::cpp
