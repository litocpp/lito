export module tenon.module_graph;

import rstd;
import tenon.model;

namespace tenon::module_detail
{

using ProviderMap = rstd::collections::BTreeMap<String, UnitId>;
using StringSet   = rstd::collections::BTreeMap<String, rstd::empty>;

template<typename T>
auto failure(String message) -> Result<T> {
    return rstd::Err(Error::make(ErrorKind::Dependency, rstd::move(message)));
}

template<typename T>
auto failure(rstd::ref<rstd::str> message) -> Result<T> {
    return rstd::Err(Error::make(ErrorKind::Dependency, message));
}

auto contains(const Vec<TargetId>& values, TargetId value) -> bool {
    for (auto item : values) {
        if (item == value) return true;
    }
    return false;
}

auto visit(UnitId unit,
           const Vec<PreparedUnit>& units,
           const Vec<Vec<ResolvedModuleArtifact>>& direct_inputs,
           Vec<rstd::uint8_t>& colors,
           Vec<UnitId>& compile_order) -> Result<rstd::empty> {
    auto& color = colors[unit];
    if (color == 2) return rstd::Ok(rstd::empty {});
    if (color == 1) {
        return failure<rstd::empty>(rstd::format(
            "module import cycle at '{}'", units[unit].unit.source.as_path()));
    }

    color = 1;
    for (const auto& input : direct_inputs[unit]) {
        auto dependency = visit(input.provider, units, direct_inputs, colors, compile_order);
        if (dependency.is_err()) return dependency;
    }
    color = 2;
    compile_order.emplace_back(unit);
    return rstd::Ok(rstd::empty {});
}

auto clone_artifact(const ResolvedModuleArtifact& artifact) -> ResolvedModuleArtifact {
    return ResolvedModuleArtifact {
        .logical_name = artifact.logical_name.clone(),
        .provider = artifact.provider,
        .bmi = artifact.bmi.clone(),
    };
}

auto append_artifact(Vec<ResolvedModuleArtifact>& output,
                     StringSet& seen,
                     const ResolvedModuleArtifact& artifact) -> void {
    if (seen.contains_key(artifact.logical_name.as_str())) return;
    seen.insert(artifact.logical_name.clone(), rstd::empty {});
    output.push(clone_artifact(artifact));
}

} // namespace tenon::module_detail

export namespace tenon
{

auto resolve_modules(const PackagePlan& package,
                     const Vec<PreparedUnit>& units,
                     const Vec<ScanResult>& scans) -> Result<ModulePlan> {
    using namespace module_detail;
    using namespace rstd::literals;

    if (units.len() != scans.len()) {
        return failure<ModulePlan>("module graph received mismatched units and scans"_str);
    }

    auto providers = ProviderMap::make();
    for (const auto& scan : scans) {
        if (scan.unit >= units.len()) {
            return failure<ModulePlan>("scan result has invalid unit id"_str);
        }
        if (scan.provided.is_none()) continue;
        const auto& provided = *scan.provided;
        auto existing = providers.get(provided.logical_name.as_str());
        if (existing.is_some()) {
            return failure<ModulePlan>(rstd::format(
                "duplicate module provider '{}': '{}' and '{}'",
                provided.logical_name.as_str(),
                units[**existing].unit.source.as_path(),
                units[scan.unit].unit.source.as_path()));
        }
        if (units[scan.unit].unit.bmi.is_none()) {
            return failure<ModulePlan>(rstd::format(
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
            return failure<ModulePlan>(rstd::format(
                "partition '{}' has no primary module provider '{}'",
                provided.logical_name.as_str(),
                primary_name));
        }
        const auto primary_target = units[**primary_provider].unit.target;
        const auto partition_target = units[scan.unit].unit.target;
        const auto& primary_affiliation =
            package.package->targets[primary_target].module_affiliation;
        const auto& partition_affiliation =
            package.package->targets[partition_target].module_affiliation;
        if (primary_affiliation.is_none() || partition_affiliation.is_none() ||
            *primary_affiliation != (*partition_affiliation).as_str()) {
            return failure<ModulePlan>(rstd::format(
                "partition '{}' and primary module '{}' have different named-module affiliations",
                provided.logical_name.as_str(),
                primary_name));
        }
    }

    auto direct_inputs = Vec<Vec<ResolvedModuleArtifact>>::with_capacity(units.len());
    for (auto unit = UnitId {}; unit < units.len(); ++unit) direct_inputs.emplace_back();
    for (const auto& scan : scans) {
        auto names = StringSet::make();
        for (const auto& required : scan.required_modules) {
            auto provider = providers.get(required.as_str());
            if (provider.is_none()) {
                return failure<ModulePlan>(rstd::format(
                    "missing module provider '{}' imported by '{}'",
                    required.as_str(),
                    units[scan.unit].unit.source.as_path()));
            }
            const auto provider_unit   = **provider;
            const auto importer_target = units[scan.unit].unit.target;
            const auto provider_target = units[provider_unit].unit.target;
            if (importer_target >= package.visible_targets.len() ||
                ! contains(package.visible_targets[importer_target], provider_target)) {
                return failure<ModulePlan>(rstd::format(
                    "module '{}' from target '{}' is not visible to target '{}'",
                    required.as_str(),
                    package.package->targets[provider_target].name.as_str(),
                    package.package->targets[importer_target].name.as_str()));
            }
            if (names.contains_key(required.as_str())) continue;
            names.insert(required.clone(), rstd::empty {});
            direct_inputs[scan.unit].push(ResolvedModuleArtifact {
                .logical_name = required.clone(),
                .provider = provider_unit,
                .bmi = (*units[provider_unit].unit.bmi).clone(),
            });
        }
    }

    auto colors = Vec<rstd::uint8_t>::with_capacity(units.len());
    for (auto unit = UnitId {}; unit < units.len(); ++unit) colors.emplace_back(0);
    auto compile_order = Vec<UnitId>::make();
    for (auto unit = UnitId {}; unit < units.len(); ++unit) {
        auto ordered = visit(unit, units, direct_inputs, colors, compile_order);
        if (ordered.is_err()) return rstd::Err(rstd::move(ordered).unwrap_err());
    }

    auto transitive_inputs = Vec<Vec<ResolvedModuleArtifact>>::with_capacity(units.len());
    for (auto unit = UnitId {}; unit < units.len(); ++unit) transitive_inputs.emplace_back();
    for (auto unit : compile_order) {
        auto seen = StringSet::make();
        for (const auto& input : direct_inputs[unit]) {
            append_artifact(transitive_inputs[unit], seen, input);
        }
        for (const auto& input : direct_inputs[unit]) {
            for (const auto& dependency : transitive_inputs[input.provider]) {
                append_artifact(transitive_inputs[unit], seen, dependency);
            }
        }
    }

    return rstd::Ok(ModulePlan {
        .compile_order = rstd::move(compile_order),
        .direct_inputs = rstd::move(direct_inputs),
        .transitive_inputs = rstd::move(transitive_inputs),
    });
}

} // namespace tenon
