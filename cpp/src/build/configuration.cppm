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
    BmiMode                       bmi_mode { BmiMode::Reduced };
    BmiSourceEmbeddingPolicy bmi_source_embedding { BmiSourceEmbeddingPolicy::ExternalSources };
    String                   language_standard;
    Option<lito::manifest::CStandard> c_standard;
    Vec<String>                       options;
    Vec<String>                       linker_options;

    auto clone() const -> BuildConfiguration {
        return BuildConfiguration {
            .toolchain            = toolchain.clone(),
            .standard_library     = standard_library,
            .bmi_mode             = bmi_mode,
            .bmi_source_embedding = bmi_source_embedding,
            .language_standard    = language_standard.clone(),
            .c_standard           = c_standard,
            .options              = options.clone(),
            .linker_options       = linker_options.clone(),
        };
    }
};

} // namespace lito::cpp
