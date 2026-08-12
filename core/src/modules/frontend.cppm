export module lito.modules:frontend;

import rstd;
import lito.error;
import lito.build.plan_contract;
import lito.build.identity;
import lito.source.discovery_contract;
import lito.frontend.result;

using namespace rstd::prelude;

namespace lito::modules
{

auto contains_name(const Vec<String>& values, ref<str> name) -> bool {
    for (const auto& value : values) {
        if (value.as_str() == name) return true;
    }
    return false;
}

} // namespace lito::modules

export namespace lito::modules
{

auto scan_from_frontend(const frontend::FrontendResult& facts, UnitId unit) -> ScanResult {
    auto result = ScanResult {
        .unit                     = unit,
        .preprocessor_environment = facts.preprocessor_environment.clone(),
    };
    if (facts.provided.is_some()) {
        result.provided = Some(ProvidedModule {
            .logical_name = facts.provided->logical_name.clone(),
            .is_interface = facts.provided->is_interface,
        });
    }
    if (facts.implementation_module.is_some()) {
        result.implementation_module = Some(facts.implementation_module->clone());
        result.required_modules.push(facts.implementation_module->clone());
    }
    for (const auto& imported : facts.imports) {
        if (! contains_name(result.required_modules, imported.logical_name.as_str())) {
            result.required_modules.push(imported.logical_name.clone());
        }
    }
    for (const auto& header : facts.header_inputs) result.header_inputs.push(header.clone());
    return result;
}

} // namespace lito::modules
