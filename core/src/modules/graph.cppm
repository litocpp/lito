export module lito.modules:graph;

import rstd;
import lito.model;
import :convention;

using namespace rstd::prelude;
using namespace rstd::literals;
using ProviderMap = rstd::collections::BTreeMap<String, lito::UnitId>;
using StringSet   = rstd::collections::BTreeMap<String, empty>;

namespace lito
{

template<typename T>
auto graph_failure(String message) -> Result<T> {
    return Err(Error::make(ErrorKind::Dependency, rstd::move(message)));
}

template<typename T>
auto graph_failure(ref<str> message) -> Result<T> {
    return Err(Error::make(ErrorKind::Dependency, message));
}

auto contains_target(const Vec<TargetId>& values, TargetId value) -> bool {
    for (auto item : values) {
        if (item == value) return true;
    }
    return false;
}

auto contains_unit(const Vec<UnitId>& values, UnitId value) -> bool {
    for (auto item : values) {
        if (item == value) return true;
    }
    return false;
}

auto visit(UnitId                   unit,
           const Vec<PreparedUnit>& units,
           const Vec<Vec<UnitId>>&  direct_inputs,
           Vec<uint8_t>&            colors,
           Vec<UnitId>&             compile_order) -> Result<empty> {
    auto& color = colors[unit];
    if (color == 2) return Ok(empty {});
    if (color == 1) {
        return graph_failure<empty>(
            rstd::format("module import cycle at '{}'", units[unit].unit.source.as_path()));
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

} // namespace lito

export namespace lito
{

auto resolve_modules(const PackagePlan&       package,
                     const Vec<PreparedUnit>& units,
                     const Vec<ScanResult>&   scans,
                     const BmiFormatIdentity& format) -> Result<ModulePlan> {
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
        auto        existing = providers.get(provided.logical_name.as_str());
        if (existing.is_some()) {
            return graph_failure<ModulePlan>(
                rstd::format("duplicate module provider '{}': '{}' and '{}'",
                             provided.logical_name.as_str(),
                             units[**existing].unit.source.as_path(),
                             units[scan.unit].unit.source.as_path()));
        }
        providers.insert(provided.logical_name.clone(), scan.unit);
    }

    for (const auto& scan : scans) {
        if (scan.provided.is_none()) continue;
        const auto& provided  = *scan.provided;
        auto        separator = provided.logical_name.as_str().find(":"_str);
        if (separator.is_none()) continue;
        auto primary_name     = provided.logical_name.as_str().split_at(*separator).get<0>();
        auto primary_provider = providers.get(primary_name);
        if (primary_provider.is_none()) {
            return graph_failure<ModulePlan>(
                rstd::format("partition '{}' has no primary module provider '{}'",
                             provided.logical_name.as_str(),
                             primary_name));
        }
        const auto  primary_target   = units[**primary_provider].unit.target;
        const auto  partition_target = units[scan.unit].unit.target;
        const auto& primary_affiliation =
            package.package->targets[primary_target].module_affiliation;
        const auto& partition_affiliation =
            package.package->targets[partition_target].module_affiliation;
        if (primary_affiliation.is_none() || partition_affiliation.is_none() ||
            ! module_name_belongs(primary_affiliation->as_str(), primary_name) ||
            ! module_name_belongs(partition_affiliation->as_str(), primary_name)) {
            return graph_failure<ModulePlan>(
                rstd::format("partition '{}' has primary module '{}' with a "
                             "different named-module affiliation",
                             provided.logical_name.as_str(),
                             primary_name));
        }
        const auto& primary_context   = *units[**primary_provider].unit.context;
        const auto& partition_context = *units[scan.unit].unit.context;
        auto        compatibility     = check_bmi_compatibility(format,
                                                                primary_context.cpp,
                                                                primary_context.public_requirements,
                                                                format,
                                                                partition_context.cpp);
        if (! compatibility.compatible()) {
            const auto& difference = compatibility.differences[usize {}];
            return graph_failure<ModulePlan>(
                rstd::format("partition '{}' and primary module '{}' have incompatible {}: "
                             "primary '{}', partition '{}'",
                             provided.logical_name.as_str(),
                             primary_name,
                             bmi_compatibility_field_name(difference.field),
                             difference.provider.as_str(),
                             difference.consumer.as_str()));
        }
    }

    auto direct_inputs = Vec<Vec<UnitId>>::with_capacity(units.len());
    for (auto unit = UnitId {}; unit < units.len(); ++unit) direct_inputs.emplace_back();
    for (const auto& scan : scans) {
        auto names = StringSet::make();
        for (const auto& required : scan.required_modules) {
            auto provider = providers.get(required.as_str());
            if (provider.is_none()) {
                return graph_failure<ModulePlan>(
                    rstd::format("missing module provider '{}' imported by '{}'",
                                 required.as_str(),
                                 units[scan.unit].unit.source.as_path()));
            }
            const auto provider_unit   = **provider;
            const auto importer_target = units[scan.unit].unit.target;
            const auto provider_target = units[provider_unit].unit.target;
            if (importer_target >= package.visible_targets.len() ||
                ! contains_target(package.visible_targets[importer_target], provider_target)) {
                return graph_failure<ModulePlan>(
                    rstd::format("module '{}' from target '{}' is not visible to target '{}'",
                                 required.as_str(),
                                 package.package->targets[provider_target].name.as_str(),
                                 package.package->targets[importer_target].name.as_str()));
            }
            auto compatibility =
                check_bmi_compatibility(format,
                                        units[provider_unit].unit.context->cpp,
                                        units[provider_unit].unit.context->public_requirements,
                                        format,
                                        units[scan.unit].unit.context->cpp);
            if (! compatibility.compatible()) {
                const auto& difference = compatibility.differences[usize {}];
                return graph_failure<ModulePlan>(
                    rstd::format("module '{}' imported by '{}' has incompatible {}: provider '{}', "
                                 "consumer '{}'",
                                 required.as_str(),
                                 units[scan.unit].unit.source.as_path(),
                                 bmi_compatibility_field_name(difference.field),
                                 difference.provider.as_str(),
                                 difference.consumer.as_str()));
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

    auto resolved_inputs = Vec<Vec<UnitId>>::with_capacity(units.len());
    for (auto unit = UnitId {}; unit < units.len(); ++unit) resolved_inputs.emplace_back();
    for (auto unit : compile_order) {
        for (auto input : direct_inputs[unit]) {
            for (auto transitive : resolved_inputs[input]) {
                if (! contains_unit(resolved_inputs[unit], transitive)) {
                    resolved_inputs[unit].emplace_back(transitive);
                }
            }
            if (! contains_unit(resolved_inputs[unit], input)) {
                resolved_inputs[unit].emplace_back(input);
            }
        }
    }

    return Ok(ModulePlan {
        .compile_order   = rstd::move(compile_order),
        .direct_inputs   = rstd::move(direct_inputs),
        .resolved_inputs = rstd::move(resolved_inputs),
    });
}

} // namespace lito
