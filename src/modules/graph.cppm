export module tenon.modules:graph;

import rstd;
import tenon.model;

using namespace rstd::prelude;
using namespace rstd::literals;
using ProviderMap = rstd::collections::BTreeMap<String, tenon::UnitId>;
using StringSet   = rstd::collections::BTreeMap<String, empty>;

namespace tenon
{

template<typename T>
auto graph_failure(String message) -> Result<T> {
    return Err(Error::make(ErrorKind::Dependency, rstd::move(message)));
}

template<typename T>
auto graph_failure(ref<str> message) -> Result<T> {
    return Err(Error::make(ErrorKind::Dependency, message));
}

auto contains(const Vec<TargetId>& values, TargetId value) -> bool {
    for (auto item : values) {
        if (item == value) return true;
    }
    return false;
}

auto visit(UnitId unit,
           const Vec<PreparedUnit>& units,
           const Vec<Vec<UnitId>>& direct_inputs,
           Vec<uint8_t>& colors,
           Vec<UnitId>& compile_order) -> Result<empty> {
    auto& color = colors[unit];
    if (color == 2) return Ok(empty {});
    if (color == 1) {
        return graph_failure<empty>(rstd::format(
            "module import cycle at '{}'", units[unit].unit.source.as_path()));
    }

    color = 1;
    for (auto input : direct_inputs[unit]) {
        auto dependency = visit(input, units, direct_inputs, colors, compile_order);
        if (dependency.is_err()) return dependency;
    }
    color = 2;
    compile_order.emplace_back(unit);
    return Ok(empty {});
}

} // namespace tenon

export namespace tenon
{

auto resolve_modules(const PackagePlan& package,
                     const Vec<PreparedUnit>& units,
                     const Vec<ScanResult>& scans) -> Result<ModulePlan> {
    if (units.len() != scans.len()) {
        return graph_failure<ModulePlan>("module graph received mismatched units and scans"_str);
    }

    auto providers = ProviderMap::make();
    for (const auto& scan : scans) {
        if (scan.unit >= units.len()) {
            return graph_failure<ModulePlan>("scan result has invalid unit id"_str);
        }
        if (scan.provided.is_none()) continue;
        const auto& provided = *scan.provided;
        auto existing = providers.get(provided.logical_name.as_str());
        if (existing.is_some()) {
            return graph_failure<ModulePlan>(rstd::format(
                "duplicate module provider '{}': '{}' and '{}'",
                provided.logical_name.as_str(),
                units[**existing].unit.source.as_path(),
                units[scan.unit].unit.source.as_path()));
        }
        if (units[scan.unit].unit.bmi.is_none()) {
            return graph_failure<ModulePlan>(rstd::format(
                "module provider has no BMI artifact: {}", units[scan.unit].unit.source.as_path()));
        }
        providers.insert(provided.logical_name.clone(), scan.unit);
    }

    for (const auto& scan : scans) {
        if (scan.provided.is_none()) continue;
        const auto& provided = *scan.provided;
        auto separator = provided.logical_name.as_str().find(":"_str);
        if (separator.is_none()) continue;
        auto primary_name = provided.logical_name.as_str().split_at(*separator).get<0>();
        auto primary_provider = providers.get(primary_name);
        if (primary_provider.is_none()) {
            return graph_failure<ModulePlan>(rstd::format(
                "partition '{}' has no primary module provider '{}'",
                provided.logical_name.as_str(),
                primary_name));
        }
        const auto primary_target = units[**primary_provider].unit.target;
        const auto& primary_affiliation =
            package.package->targets[primary_target].module_affiliation;
        if (primary_affiliation.is_none() || *primary_affiliation != primary_name) {
            return graph_failure<ModulePlan>(rstd::format(
                "partition '{}' has primary module '{}' with a different named-module affiliation",
                provided.logical_name.as_str(),
                primary_name));
        }
    }

    auto direct_inputs = Vec<Vec<UnitId>>::with_capacity(units.len());
    for (auto unit = UnitId {}; unit < units.len(); ++unit) direct_inputs.emplace_back();
    for (const auto& scan : scans) {
        auto names = StringSet::make();
        for (const auto& required : scan.required_modules) {
            auto provider = providers.get(required.as_str());
            if (provider.is_none()) {
                return graph_failure<ModulePlan>(rstd::format(
                    "missing module provider '{}' imported by '{}'",
                    required.as_str(),
                    units[scan.unit].unit.source.as_path()));
            }
            const auto provider_unit   = **provider;
            const auto importer_target = units[scan.unit].unit.target;
            const auto provider_target = units[provider_unit].unit.target;
            if (importer_target >= package.visible_targets.len() ||
                ! contains(package.visible_targets[importer_target], provider_target)) {
                return graph_failure<ModulePlan>(rstd::format(
                    "module '{}' from target '{}' is not visible to target '{}'",
                    required.as_str(),
                    package.package->targets[provider_target].name.as_str(),
                    package.package->targets[importer_target].name.as_str()));
            }
            if (names.contains_key(required.as_str())) continue;
            names.insert(required.clone(), empty {});
            direct_inputs[scan.unit].emplace_back(provider_unit);
        }
    }

    auto colors = Vec<uint8_t>::with_capacity(units.len());
    for (auto unit = UnitId {}; unit < units.len(); ++unit) colors.emplace_back(0);
    auto compile_order = Vec<UnitId>::make();
    for (auto unit = UnitId {}; unit < units.len(); ++unit) {
        auto ordered = visit(unit, units, direct_inputs, colors, compile_order);
        if (ordered.is_err()) return Err(rstd::move(ordered).unwrap_err());
    }

    return Ok(ModulePlan {
        .compile_order = rstd::move(compile_order),
        .direct_inputs = rstd::move(direct_inputs),
    });
}

} // namespace tenon
