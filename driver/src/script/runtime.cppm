module lito.driver:script.runtime;

import rstd;
import luato;
import :package.module_catalog;

using namespace rstd::prelude;

namespace lito
{

auto loaded_script_identities(const luato::State& state) -> Vec<String> {
    return rstd::iter::from_slice(state.loaded_modules())
        .map([](auto module) {
            return module->identity.clone();
        })
        .collect<Vec<String>>();
}

auto attach_script_modules(luato::State& state, lito::package::ScriptModuleCatalog& modules)
    -> luato::Result<empty> {
    return state.set_module_resolver(
        luato::ModuleResolverSpec::make([&modules](luato::ModuleRequest request) {
            return modules.resolve(rstd::move(request));
        }));
}

} // namespace lito
