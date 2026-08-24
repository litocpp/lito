export module lito.cpp:modules.frontend;

import rstd;
import lito.core;
import :build.scan;
import lito.frontend.result;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace lito::cpp
{

auto append_requirement(Vec<RequiredModule>&                 values,
                        ref<str>                             name,
                        bool                                 imported,
                        bool                                 implementation,
                        Option<frontend::DependencyLocation> location,
                        bool                                 exported) -> empty {
    for (auto& value : values) {
        if (value.logical_name.as_str() != name) continue;
        value.imported       = value.imported || imported;
        value.implementation = value.implementation || implementation;
        value.exported       = value.exported || exported;
        if (location.is_some()) value.import_locations.push(rstd::move(location).unwrap());
        return {};
    }
    auto locations = Vec<frontend::DependencyLocation>::make();
    if (location.is_some()) locations.push(rstd::move(location).unwrap());
    values.push(RequiredModule {
        .logical_name     = String::make(name),
        .imported         = imported,
        .implementation   = implementation,
        .import_locations = rstd::move(locations),
        .exported         = exported,
    });
    return {};
}

} // namespace lito::cpp

export namespace lito::cpp
{

auto project_frontend_analysis(frontend::FrontendAnalysis      analysis,
                               lito::manifest::PackageLanguage language)
    -> Result<SourceScanArtifact, String> {
    auto facts  = rstd::move(analysis.result);
    auto common = CommonScanResult {
        .source                   = rstd::move(facts.source),
        .header_inputs            = rstd::move(facts.header_inputs),
        .embedded_inputs          = rstd::move(facts.embedded_inputs),
        .external_macros          = rstd::move(facts.external_macros),
        .preprocessor_environment = rstd::move(facts.preprocessor_environment),
        .input_bytes              = facts.input_bytes,
    };
    if (language == lito::manifest::PackageLanguage::C) {
        if (facts.provided.is_some() || facts.implementation_module.is_some() ||
            ! facts.imports.is_empty()) {
            return Err(String::make("C frontend result contains C++ module facts"_str));
        }
        return Ok(SourceScanArtifact {
            .language         = LanguageScanResult::C(CScanResult { .common = rstd::move(common) }),
            .context_identity = rstd::move(analysis.context_identity),
            .source_content_identity = rstd::move(analysis.receipt),
            .origin                  = analysis.origin,
        });
    }
    auto result = CppScanResult { .common = rstd::move(common) };
    if (facts.provided.is_some()) {
        result.provided = Some(rstd::move(facts.provided).unwrap());
    }
    if (facts.implementation_module.is_some()) {
        auto implementation_module = rstd::move(facts.implementation_module).unwrap();
        append_requirement(
            result.required_modules, implementation_module.as_str(), false, true, None(), false);
        result.implementation_module = Some(rstd::move(implementation_module));
    }
    for (auto& imported : facts.imports) {
        append_requirement(result.required_modules,
                           imported.logical_name.as_str(),
                           true,
                           false,
                           Some(rstd::move(imported.location)),
                           imported.exported);
    }
    return Ok(SourceScanArtifact {
        .language                = LanguageScanResult::Cpp(rstd::move(result)),
        .context_identity        = rstd::move(analysis.context_identity),
        .source_content_identity = rstd::move(analysis.receipt),
        .origin                  = analysis.origin,
    });
}

} // namespace lito::cpp
