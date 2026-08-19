export module lito.cpp:modules.graph;

import rstd;
import lito.core;
import :bmi;
import :build.scan;
import :build.plan;
import :build.unit;
import :modules.convention;
import :modules.error;

using namespace rstd::prelude;
using namespace rstd::literals;
using ProviderMap = rstd::collections::BTreeMap<String, Vec<lito::cpp::UnitId>>;
using StringSet   = rstd::collections::BTreeMap<String, empty>;

namespace lito::cpp
{

template<typename T>
auto graph_failure(String message) -> ModuleResult<T> {
    return Err(ModuleError::Graph(rstd::move(message)));
}

template<typename T>
auto graph_failure(ref<str> message) -> ModuleResult<T> {
    return Err(ModuleError::Graph(String::make(message)));
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

auto provider_for(ref<str>                 logical_name,
                  UnitId                   importer,
                  const ProviderMap&       providers,
                  const Vec<PreparedUnit>& units) -> ModuleResult<UnitId> {
    auto candidates = providers.get(logical_name);
    if (candidates.is_none()) {
        return graph_failure<UnitId>(rstd::format("missing module provider '{}' imported by '{}'",
                                                  logical_name,
                                                  units[importer].unit.source.as_path()));
    }
    for (auto candidate : **candidates) {
        if (units[candidate].unit.owner.is_Project()) return Ok(candidate);
        const auto& module = units[candidate].unit.owner.as_StandardLibrary().module;
        if (module.context_identity.as_str() ==
            units[importer].unit.standard_library_context_identity.as_str()) {
            return Ok(candidate);
        }
    }
    return graph_failure<UnitId>(rstd::format(
        "standard library module '{}' has no provider compatible with source '{}' context",
        logical_name,
        units[importer].unit.source.as_path()));
}

auto directly_visible(const PackagePlan& package, TargetId importer, TargetId provider) -> bool {
    if (importer == provider) return true;
    for (const auto& dependency : package.package->targets[importer].dependencies) {
        if (dependency.visibility != lito::dependency::DependencyVisibility::LinkOnly &&
            dependency.target == package.package->targets[provider].id) {
            return true;
        }
    }
    return false;
}

auto target_index(const PackagePlan& package, const lito::package::PackageTargetId& identity)
    -> Option<TargetId> {
    for (auto target = TargetId {}; target < package.package->targets.len(); ++target) {
        if (package.package->targets[target].id == identity) return Some(target);
    }
    return None();
}

auto publicly_reexported(const PackagePlan&      package,
                         const Vec<Vec<UnitId>>& public_target_units,
                         TargetId                importer,
                         UnitId                  provider) -> bool {
    for (const auto& dependency : package.package->targets[importer].dependencies) {
        if (dependency.visibility == lito::dependency::DependencyVisibility::LinkOnly) continue;
        auto dependency_target = target_index(package, dependency.target);
        if (dependency_target.is_none()) continue;
        if (contains_unit(public_target_units[*dependency_target], provider)) return true;
    }
    return false;
}

auto visit(UnitId                   unit,
           const Vec<PreparedUnit>& units,
           const Vec<Vec<UnitId>>&  direct_inputs,
           Vec<uint8_t>&            colors,
           Vec<UnitId>&             compile_order) -> ModuleResult<empty> {
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

} // namespace lito::cpp

export namespace lito::cpp
{

struct ResolvedSemanticBuildGraph {
    Vec<UnitId>      c_units;
    Vec<UnitId>      cpp_units;
    Vec<UnitId>      compile_order;
    Vec<Vec<UnitId>> direct_inputs;
    Vec<Vec<UnitId>> resolved_inputs;
    Vec<Vec<UnitId>> public_inputs;
};

auto resolve_semantic_build(const PackagePlan&       package,
                            const Vec<PreparedUnit>& units,
                            const Vec<ScanResult>&   scans,
                            const BmiFormatIdentity& format)
    -> ModuleResult<ResolvedSemanticBuildGraph> {
    if (units.len() != scans.len()) {
        return graph_failure<ResolvedSemanticBuildGraph>(
            "semantic graph received mismatched units and scans"_str);
    }
    if (package.public_targets.len() != package.package->targets.len() ||
        package.visible_targets.len() != package.package->targets.len()) {
        return graph_failure<ResolvedSemanticBuildGraph>(
            "semantic graph received an invalid package plan"_str);
    }

    auto c_units   = Vec<UnitId>::make();
    auto cpp_units = Vec<UnitId>::make();
    for (auto unit = UnitId {}; unit < scans.len(); ++unit) {
        const auto& scan = scans[unit];
        if (scan.unit != unit) {
            return graph_failure<ResolvedSemanticBuildGraph>(
                "semantic graph received unordered scan results"_str);
        }
        if (scan.language.is_C() != units[unit].unit.language.is_C() ||
            scan.language.is_C() != units[unit].unit.context->language.is_C()) {
            return graph_failure<ResolvedSemanticBuildGraph>(rstd::format(
                "source '{}' has inconsistent language facts", units[unit].unit.source.as_path()));
        }
        if (scan.language.is_C())
            c_units.emplace_back(unit);
        else
            cpp_units.emplace_back(unit);
    }

    auto providers = ProviderMap::make();
    for (auto unit : cpp_units) {
        const auto& scan  = scans[unit];
        const auto& facts = scan.language.as_Cpp().facts;
        if (facts.provided.is_none()) continue;
        const auto& provided = *facts.provided;
        if (units[scan.unit].unit.owner.is_Project() &&
            is_standard_library_module_name(provided.logical_name.as_str())) {
            return Err(ModuleError::ReservedProvider(units[scan.unit].unit.source.clone(),
                                                     provided.logical_name.clone()));
        }
        auto existing = providers.get_mut(provided.logical_name.as_str());
        if (existing.is_some()) {
            for (auto other : **existing) {
                const auto project_conflict = units[other].unit.owner.is_Project() ||
                                              units[scan.unit].unit.owner.is_Project();
                const auto same_context =
                    units[other].unit.standard_library_context_identity.as_str() ==
                    units[scan.unit].unit.standard_library_context_identity.as_str();
                if (! project_conflict && ! same_context) continue;
                return graph_failure<ResolvedSemanticBuildGraph>(
                    rstd::format("duplicate module provider '{}': '{}' and '{}'",
                                 provided.logical_name.as_str(),
                                 units[other].unit.source.as_path(),
                                 units[scan.unit].unit.source.as_path()));
            }
            (**existing).emplace_back(scan.unit);
            continue;
        }
        auto values = Vec<UnitId>::make();
        values.emplace_back(scan.unit);
        providers.insert(provided.logical_name.clone(), rstd::move(values));
    }

    for (auto unit : cpp_units) {
        const auto& scan  = scans[unit];
        const auto& facts = scan.language.as_Cpp().facts;
        if (facts.provided.is_none()) continue;
        const auto& provided  = *facts.provided;
        auto        separator = provided.logical_name.as_str().find(":"_str);
        if (separator.is_none()) continue;
        auto primary_name     = provided.logical_name.as_str().split_at(*separator).get<0>();
        auto primary_provider = provider_for(primary_name, scan.unit, providers, units);
        if (primary_provider.is_err()) return Err(rstd::move(primary_provider).unwrap_err());
        auto primary_target   = project_target(units[*primary_provider].unit);
        auto partition_target = project_target(units[scan.unit].unit);
        if (primary_target.is_none() || partition_target.is_none()) {
            return graph_failure<ResolvedSemanticBuildGraph>(
                "standard library modules cannot participate in project partitions"_str);
        }
        const auto& primary_affiliation =
            package.package->targets[*primary_target].module_affiliation;
        const auto& partition_affiliation =
            package.package->targets[*partition_target].module_affiliation;
        if (*primary_target != *partition_target &&
            (primary_affiliation.is_none() || partition_affiliation.is_none() ||
             ! module_name_belongs(primary_affiliation->as_str(), primary_name) ||
             ! module_name_belongs(partition_affiliation->as_str(), primary_name))) {
            return graph_failure<ResolvedSemanticBuildGraph>(
                rstd::format("partition '{}' has primary module '{}' with a "
                             "different named-module affiliation",
                             provided.logical_name.as_str(),
                             primary_name));
        }
        const auto& primary_context   = *units[*primary_provider].unit.context;
        const auto& partition_context = *units[scan.unit].unit.context;
        if (! primary_context.language.is_Cpp() || ! partition_context.language.is_Cpp()) {
            return graph_failure<ResolvedSemanticBuildGraph>(
                "C source unexpectedly provided a C++ module"_str);
        }
        const auto& primary_cpp   = primary_context.language.as_Cpp();
        const auto& partition_cpp = partition_context.language.as_Cpp();
        auto        compatibility = check_bmi_compatibility(format,
                                                            primary_cpp.options,
                                                            primary_cpp.public_requirements,
                                                            format,
                                                            partition_cpp.options);
        if (! compatibility.compatible()) {
            const auto& difference = compatibility.differences[usize {}];
            return graph_failure<ResolvedSemanticBuildGraph>(
                rstd::format("partition '{}' and primary module '{}' have incompatible {}: "
                             "primary '{}', partition '{}'",
                             provided.logical_name.as_str(),
                             primary_name,
                             difference.field,
                             difference.provider.as_str(),
                             difference.consumer.as_str()));
        }
    }

    auto direct_inputs   = Vec<Vec<UnitId>>::with_capacity(units.len());
    auto exported_inputs = Vec<Vec<UnitId>>::with_capacity(units.len());
    for (auto unit = UnitId {}; unit < units.len(); ++unit) {
        direct_inputs.emplace_back();
        exported_inputs.emplace_back();
    }
    for (auto unit : cpp_units) {
        const auto& scan  = scans[unit];
        const auto& facts = scan.language.as_Cpp().facts;
        auto        names = StringSet::make();
        for (const auto& required : facts.required_modules) {
            auto provider =
                provider_for(required.logical_name.as_str(), scan.unit, providers, units);
            if (provider.is_err()) return Err(rstd::move(provider).unwrap_err());
            const auto provider_unit   = *provider;
            auto       importer_target = project_target(units[scan.unit].unit);
            auto       provider_target = project_target(units[provider_unit].unit);
            if (importer_target.is_some() && provider_target.is_some() &&
                (*importer_target >= package.visible_targets.len() ||
                 ! contains_target(package.visible_targets[*importer_target], *provider_target))) {
                return graph_failure<ResolvedSemanticBuildGraph>(
                    rstd::format("module '{}' from target '{}' is not visible to target '{}'",
                                 required.logical_name.as_str(),
                                 package.package->targets[*provider_target].id.name.as_str(),
                                 package.package->targets[*importer_target].id.name.as_str()));
            }
            const auto& provider_context = *units[provider_unit].unit.context;
            const auto& importer_context = *units[scan.unit].unit.context;
            if (! provider_context.language.is_Cpp() || ! importer_context.language.is_Cpp()) {
                return graph_failure<ResolvedSemanticBuildGraph>(
                    "C source unexpectedly participated in the C++ module graph"_str);
            }
            const auto& provider_cpp  = provider_context.language.as_Cpp();
            const auto& importer_cpp  = importer_context.language.as_Cpp();
            auto        compatibility = check_bmi_compatibility(format,
                                                                provider_cpp.options,
                                                                provider_cpp.public_requirements,
                                                                format,
                                                                importer_cpp.options);
            if (! compatibility.compatible()) {
                const auto& difference = compatibility.differences[usize {}];
                return graph_failure<ResolvedSemanticBuildGraph>(
                    rstd::format("module '{}' imported by '{}' has incompatible {}: provider '{}', "
                                 "consumer '{}'",
                                 required.logical_name.as_str(),
                                 units[scan.unit].unit.source.as_path(),
                                 difference.field,
                                 difference.provider.as_str(),
                                 difference.consumer.as_str()));
            }
            if (names.contains_key(required.logical_name.as_str())) continue;
            names.insert(required.logical_name.clone(), empty {});
            direct_inputs[scan.unit].emplace_back(provider_unit);
            if (facts.provided.is_some() && facts.provided->is_interface && required.exported) {
                exported_inputs[scan.unit].emplace_back(provider_unit);
            }
        }
    }

    auto colors = Vec<uint8_t>::with_capacity(units.len());
    for (auto unit = UnitId {}; unit < units.len(); ++unit) colors.emplace_back(0);
    auto compile_order = Vec<UnitId>::make();
    for (auto unit : c_units) {
        colors[unit] = uint8_t(2);
        compile_order.emplace_back(unit);
    }
    for (auto unit : cpp_units) {
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

    auto public_inputs = Vec<Vec<UnitId>>::with_capacity(units.len());
    for (auto unit = UnitId {}; unit < units.len(); ++unit) public_inputs.emplace_back();
    for (auto unit : compile_order) {
        for (auto input : exported_inputs[unit]) {
            for (auto transitive : public_inputs[input]) {
                if (! contains_unit(public_inputs[unit], transitive)) {
                    public_inputs[unit].emplace_back(transitive);
                }
            }
            if (! contains_unit(public_inputs[unit], input)) {
                public_inputs[unit].emplace_back(input);
            }
        }
    }

    auto public_target_units = Vec<Vec<UnitId>>::with_capacity(package.package->targets.len());
    for (auto target = TargetId {}; target < package.package->targets.len(); ++target) {
        public_target_units.emplace_back();
    }
    for (auto unit : cpp_units) {
        const auto& scan  = scans[unit];
        const auto& facts = scan.language.as_Cpp().facts;
        if (facts.provided.is_none() || ! facts.provided->is_interface ||
            facts.provided->logical_name.as_str().contains(":"_str)) {
            continue;
        }
        auto target = project_target(units[scan.unit].unit);
        if (target.is_none()) continue;
        if (! contains_unit(public_target_units[*target], scan.unit)) {
            public_target_units[*target].emplace_back(scan.unit);
        }
        for (auto exported : public_inputs[scan.unit]) {
            if (! units[exported].unit.owner.is_Project()) continue;
            if (! contains_unit(public_target_units[*target], exported)) {
                public_target_units[*target].emplace_back(exported);
            }
        }
    }

    for (auto unit : cpp_units) {
        const auto& scan            = scans[unit];
        const auto& facts           = scan.language.as_Cpp().facts;
        auto        importer_target = project_target(units[scan.unit].unit);
        if (importer_target.is_none()) continue;
        for (const auto& required : facts.required_modules) {
            auto provider =
                provider_for(required.logical_name.as_str(), scan.unit, providers, units);
            if (provider.is_err()) return Err(rstd::move(provider).unwrap_err());
            auto provider_unit   = *provider;
            auto provider_target = project_target(units[provider_unit].unit);
            if (provider_target.is_none()) continue;
            if (required.exported &&
                contains_unit(public_target_units[*importer_target], scan.unit) &&
                *importer_target != *provider_target &&
                ! contains_target(package.public_targets[*importer_target], *provider_target)) {
                return graph_failure<ResolvedSemanticBuildGraph>(rstd::format(
                    "module '{}' export-imports module '{}' from target '{}', but that target is "
                    "not a public dependency",
                    facts.provided->logical_name.as_str(),
                    required.logical_name.as_str(),
                    package.package->targets[*provider_target].id.name.as_str()));
            }
            if (directly_visible(package, *importer_target, *provider_target) ||
                publicly_reexported(
                    package, public_target_units, *importer_target, provider_unit)) {
                continue;
            }
            if (package.package->targets[*importer_target].test_attachment.is_some() &&
                *importer_target < package.visible_targets.len() &&
                contains_target(package.visible_targets[*importer_target], *provider_target)) {
                continue;
            }
            return graph_failure<ResolvedSemanticBuildGraph>(rstd::format(
                "module '{}' from target '{}' is not directly visible or re-exported to target "
                "'{}'",
                required.logical_name.as_str(),
                package.package->targets[*provider_target].id.name.as_str(),
                package.package->targets[*importer_target].id.name.as_str()));
        }
    }

    return Ok(ResolvedSemanticBuildGraph {
        .c_units         = rstd::move(c_units),
        .cpp_units       = rstd::move(cpp_units),
        .compile_order   = rstd::move(compile_order),
        .direct_inputs   = rstd::move(direct_inputs),
        .resolved_inputs = rstd::move(resolved_inputs),
        .public_inputs   = rstd::move(public_inputs),
    });
}

} // namespace lito::cpp
