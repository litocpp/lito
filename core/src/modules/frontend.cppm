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

auto append_requirement(Vec<RequiredModule>& values, ref<str> name, bool exported) -> empty {
    for (auto& value : values) {
        if (value.logical_name.as_str() != name) continue;
        value.exported = value.exported || exported;
        return {};
    }
    values.push(RequiredModule { .logical_name = String::make(name), .exported = exported });
    return {};
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
        append_requirement(result.required_modules, facts.implementation_module->as_str(), false);
    }
    for (const auto& imported : facts.imports) {
        append_requirement(
            result.required_modules, imported.logical_name.as_str(), imported.exported);
    }
    for (const auto& header : facts.header_inputs) result.header_inputs.push(header.clone());
    return result;
}

} // namespace lito::modules
