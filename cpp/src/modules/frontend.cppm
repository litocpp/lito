export module lito.cpp:modules.frontend;

import rstd;
import lito.core;
import :build.scan;
import lito.frontend.result;
import :build.unit;

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

auto scan_from_frontend(const frontend::FrontendResult& facts,
                        UnitId                          unit,
                        lito::manifest::PackageLanguage language) -> Result<ScanResult, String> {
    auto common = CommonScanResult {
        .source                   = facts.source.clone(),
        .external_macros          = as<Clone>(facts.external_macros).clone(),
        .preprocessor_environment = facts.preprocessor_environment.clone(),
        .input_bytes              = facts.input_bytes,
    };
    for (const auto& header : facts.header_inputs) common.header_inputs.push(header.clone());
    common.embedded_inputs = as<Clone>(facts.embedded_inputs).clone();
    if (language == lito::manifest::PackageLanguage::C) {
        if (facts.provided.is_some() || facts.implementation_module.is_some() ||
            ! facts.imports.is_empty()) {
            return Err(String::make("C frontend result contains C++ module facts"_str));
        }
        return Ok(ScanResult {
            .unit     = unit,
            .language = LanguageScanResult::C(CScanResult { .common = rstd::move(common) }),
        });
    }
    auto result = CppScanResult { .common = rstd::move(common) };
    if (facts.provided.is_some()) {
        result.provided = Some(frontend::ProvidedModule {
            .logical_name = facts.provided->logical_name.clone(),
            .is_interface = facts.provided->is_interface,
        });
    }
    if (facts.implementation_module.is_some()) {
        result.implementation_module = Some(facts.implementation_module->clone());
        append_requirement(result.required_modules,
                           facts.implementation_module->as_str(),
                           false,
                           true,
                           None(),
                           false);
    }
    for (const auto& imported : facts.imports) {
        append_requirement(result.required_modules,
                           imported.logical_name.as_str(),
                           true,
                           false,
                           Some(imported.location.clone()),
                           imported.exported);
    }
    return Ok(ScanResult {
        .unit     = unit,
        .language = LanguageScanResult::Cpp(rstd::move(result)),
    });
}

} // namespace lito::cpp
