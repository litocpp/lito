export module lito.build.configuration;

import rstd;
import lito.error;
import lito.cpp;
import lito.cpp.bmi;
import lito.toolchain.spec;

export namespace lito
{

struct BuildConfiguration {
    ToolchainSpec            toolchain;
    StandardLibrary          standard_library { StandardLibrary::Libstdcxx };
    BmiMode                  bmi_mode { BmiMode::Reduced };
    BmiSourceEmbeddingPolicy bmi_source_embedding { BmiSourceEmbeddingPolicy::ExternalSources };
    String                   language_standard;
    Vec<String>              options;
    Vec<String>              linker_options;

    auto clone() const -> BuildConfiguration {
        return BuildConfiguration {
            .toolchain            = toolchain.clone(),
            .standard_library     = standard_library,
            .bmi_mode             = bmi_mode,
            .bmi_source_embedding = bmi_source_embedding,
            .language_standard    = language_standard.clone(),
            .options              = options.clone(),
            .linker_options       = linker_options.clone(),
        };
    }
};

} // namespace lito
