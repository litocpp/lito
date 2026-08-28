module;
#include <rstd/enum.hpp>

export module lito.cpp:build.scan;

import rstd;
import lito.frontend;

using namespace rstd::prelude;

export namespace lito::cpp
{

struct RequiredModule {
    String                            logical_name;
    bool                              imported { false };
    bool                              implementation { false };
    Vec<frontend::DependencyLocation> import_locations;
    bool                              exported { false };

    auto clone() const -> RequiredModule {
        return RequiredModule {
            .logical_name     = logical_name.clone(),
            .imported         = imported,
            .implementation   = implementation,
            .import_locations = as<Clone>(import_locations).clone(),
            .exported         = exported,
        };
    }

    auto retained_bytes() const noexcept -> usize {
        auto result = logical_name.capacity() +
                      import_locations.capacity() * usize(sizeof(frontend::DependencyLocation));
        for (const auto& location : import_locations) result += location.path.capacity();
        return result;
    }
};

struct CommonScanResult {
    rstd::path::PathBuf                         source;
    Vec<rstd::path::PathBuf>                    header_inputs;
    Vec<frontend::EmbeddedInput>                embedded_inputs;
    Vec<frontend::ExternalMacroMaterialization> external_macros;
    String                                      preprocessor_environment;
    usize                                       input_bytes {};
};

struct CScanResult {
    CommonScanResult common;
};

struct CppScanResult {
    CommonScanResult                  common;
    Option<frontend::ProvidedModule>  provided;
    Option<String>                    implementation_module;
    Vec<RequiredModule>               required_modules;
    Vec<frontend::ScopedAttributeUse> scoped_attributes;
};

class LanguageScanResult {
    RSTD_ENUM_DEFAULT(LanguageScanResult,
                      (Cpp),
                      (C, (CScanResult facts;)),
                      (Cpp, (CppScanResult facts;)))
};

struct ScanResult {
    usize              unit {};
    LanguageScanResult language;
};

struct SourceScanArtifact {
    LanguageScanResult               language;
    String                           context_identity;
    String                           source_content_identity;
    frontend::FrontendAnalysisOrigin origin { frontend::FrontendAnalysisOrigin::Native };

    auto retained_bytes() const noexcept -> usize {
        const auto& common =
            language.is_C() ? language.as_C().facts.common : language.as_Cpp().facts.common;
        auto bytes = context_identity.capacity() + source_content_identity.capacity() +
                     common.header_inputs.capacity() * usize(sizeof(rstd::path::PathBuf)) +
                     common.embedded_inputs.capacity() * usize(sizeof(frontend::EmbeddedInput)) +
                     common.external_macros.capacity() *
                         usize(sizeof(frontend::ExternalMacroMaterialization)) +
                     common.preprocessor_environment.capacity();
        if (language.is_Cpp()) {
            bytes +=
                language.as_Cpp().facts.required_modules.capacity() * usize(sizeof(RequiredModule));
            bytes += language.as_Cpp().facts.scoped_attributes.capacity() *
                     usize(sizeof(frontend::ScopedAttributeUse));
        }
        return bytes;
    }
};

struct BoundSourceScan {
    ScanResult scan;
    String     source_content_identity;
};

auto bind_scan(SourceScanArtifact artifact, usize unit) -> BoundSourceScan {
    return BoundSourceScan {
        .scan =
            ScanResult {
                .unit     = unit,
                .language = rstd::move(artifact.language),
            },
        .source_content_identity = rstd::move(artifact.source_content_identity),
    };
}

auto scan_common(const ScanResult& result) noexcept -> const CommonScanResult& {
    return result.language.is_C() ? result.language.as_C().facts.common
                                  : result.language.as_Cpp().facts.common;
}

} // namespace lito::cpp
