module;
#include <rstd/macro.hpp>

module lito.driver:script.runtime;

import rstd;
import luato;
import lito.core;
import :package.module_catalog;

using namespace rstd::prelude;
using namespace rstd::literals;

auto script_version(ref<str> value, ref<str> context)
    -> luato::Result<lito::registry::SemanticVersion> {
    return lito::registry::SemanticVersion::parse(value).map_err([&](auto error) {
        return luato::Error::binding(rstd::format("{}: {}", context, error));
    });
}

auto script_version_compare(String left, String right) -> luato::Result<i64> {
    auto lhs      = rstd_try(script_version(left.as_str(), "version_compare.left"_str));
    auto rhs      = rstd_try(script_version(right.as_str(), "version_compare.right"_str));
    auto compared = lhs.cmp(rhs);
    return Ok(compared < 0 ? i64(-1) : compared > 0 ? i64(1) : i64(0));
}

auto script_version_matches(String version, String requirement) -> luato::Result<bool> {
    auto parsed = rstd_try(script_version(version.as_str(), "version_matches.version"_str));
    auto range  = rstd_try(
        lito::registry::VersionRequirement::parse(requirement.as_str()).map_err([](auto error) {
            return luato::Error::binding(rstd::format("version_matches.requirement: {}", error));
        }));
    return Ok(range.matches(parsed));
}

namespace lito
{

auto register_script_utilities(luato::ModuleSpec& module) -> void {
    module.function("version_compare"_Str, &script_version_compare);
    module.function("version_matches"_Str, &script_version_matches);
}

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
