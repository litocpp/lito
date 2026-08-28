export module lito.cpp:modules.graph;

import rstd;
import lito.core;
import :bmi;
import :build.scan;
import :build.plan;
import :build.unit;
import :header;
import :modules.convention;
import :modules.error;
import :package.metadata;
import :package.spec;

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
    const auto& importer_target = package.package->targets[importer];
    const auto& provider_target = package.package->targets[provider];
    for (const auto& dependency : importer_target.dependencies) {
        if (dependency.visibility != lito::dependency::DependencyVisibility::LinkOnly &&
            dependency.target == provider_target.id) {
            return true;
        }
    }
    if (importer_target.artifact_kind == ArtifactKind::ProcMacroProvider &&
        provider_target.artifact_kind == ArtifactKind::CompilerPlugin) {
        for (const auto& dependency : importer_target.plugin_dependencies) {
            if (dependency.package == provider_target.id.package.as_str()) return true;
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

using DiscoveryUnitId  = usize;
using HeaderArtifactId = usize;

struct IncrementalModuleRequirement {
    RequiredModule          requirement;
    String                  provider_key;
    Option<DiscoveryUnitId> provider;
};

struct IncrementalScanUnit {
    Option<TargetId>                  target;
    String                            target_identity;
    PathBuf                           source;
    String                            context_identity;
    String                            standard_library_context_identity;
    bool                              cpp { false };
    bool                              complete { false };
    Option<frontend::ProvidedModule>  provided;
    Vec<IncrementalModuleRequirement> requirements;
    Vec<HeaderArtifactId>             header_inputs;
};

struct IncrementalHeaderArtifact {
    String               physical_identity;
    Vec<PathBuf>         paths;
    HeaderClassification classification;
    Vec<DiscoveryUnitId> consumers;
};

struct SemanticScanGraphStatistics {
    usize headers {};
    usize project_headers {};
    usize external_headers {};
    usize toolchain_headers {};
    usize unknown_headers {};
    usize ambiguous_headers {};
    usize header_edges {};
    usize pending_peak {};
    usize unresolved_peak {};
    usize reactivations {};
    usize incremental_retained_bytes {};
    usize resolved_retained_bytes {};
};

struct IncrementalSemanticScanGraph {
    Vec<IncrementalScanUnit>       units;
    Vec<IncrementalHeaderArtifact> headers;
    SemanticScanGraphStatistics    statistics;
};

auto incremental_graph_retained_bytes(const Vec<IncrementalScanUnit>&       units,
                                      const Vec<IncrementalHeaderArtifact>& headers) noexcept
    -> usize {
    auto result = units.capacity() * usize(sizeof(IncrementalScanUnit)) +
                  headers.capacity() * usize(sizeof(IncrementalHeaderArtifact));
    for (const auto& unit : units) {
        result += unit.target_identity.capacity() + unit.source.capacity() +
                  unit.context_identity.capacity() +
                  unit.standard_library_context_identity.capacity() +
                  unit.requirements.capacity() * usize(sizeof(IncrementalModuleRequirement)) +
                  unit.header_inputs.capacity() * usize(sizeof(HeaderArtifactId));
        if (unit.provided.is_some()) result += unit.provided->logical_name.capacity();
        for (const auto& requirement : unit.requirements) {
            result +=
                requirement.provider_key.capacity() + requirement.requirement.retained_bytes();
        }
    }
    for (const auto& header : headers) {
        result += header.physical_identity.capacity() +
                  header.paths.capacity() * usize(sizeof(PathBuf)) +
                  header.classification.retained_bytes() +
                  header.consumers.capacity() * usize(sizeof(DiscoveryUnitId));
        for (const auto& path : header.paths) result += path.capacity();
    }
    return result;
}

class SemanticScanGraphBuilder {
    using UnitMap          = rstd::collections::BTreeMap<String, DiscoveryUnitId>;
    using ProviderRegistry = rstd::collections::BTreeMap<String, Vec<DiscoveryUnitId>>;
    using HeaderMap        = rstd::collections::BTreeMap<String, HeaderArtifactId>;

public:
    static auto make(const PackageMetadata&          package,
                     const ResolvedNativeTargetPlan& plan,
                     const HeaderOwnershipIndex&     ownership) -> SemanticScanGraphBuilder;
    static auto make(const PackageSpec&          package,
                     const PackagePlan&          plan,
                     const HeaderOwnershipIndex& ownership) -> SemanticScanGraphBuilder;

    auto register_project_unit(TargetId              target,
                               ref<rstd::path::Path> source,
                               ref<str>              context_identity,
                               ref<str> standard_library_context_identity) -> DiscoveryUnitId;

    auto complete(DiscoveryUnitId unit, const SourceScanArtifact& artifact)
        -> Result<empty, String>;

    auto seal_ready_targets() -> Vec<String>;

    auto finish_discovery() -> Result<Vec<String>, String>;

    auto finalize(const Vec<PreparedUnit>& units,
                  const Vec<ScanResult>& scans) && -> Result<IncrementalSemanticScanGraph, String>;

private:
    struct TargetState {
        lito::package::PackageTargetId identity;
        usize                          pending {};
        bool                           selected { false };
        bool                           sealed { false };
    };

    SemanticScanGraphBuilder(const HeaderOwnershipIndex& ownership,
                             Vec<TargetState>            targets,
                             Vec<Vec<TargetId>>          reverse_importers,
                             Vec<String>                 owned_domains)
        : ownership_(rstd::addressof(ownership)),
          targets_(rstd::move(targets)),
          reverse_importers_(rstd::move(reverse_importers)),
          owned_domains_(rstd::move(owned_domains)),
          units_(Vec<IncrementalScanUnit>::make()),
          unit_index_(UnitMap::make()),
          providers_(ProviderRegistry::make()),
          headers_(Vec<IncrementalHeaderArtifact>::make()),
          header_index_(HeaderMap::make()) {}

    auto register_completed_standard_unit(const PreparedUnit& unit, const ScanResult& scan)
        -> Result<DiscoveryUnitId, String>;
    auto unit_key(TargetId target, ref<rstd::path::Path> source) const -> String;
    auto provider_key(ref<str> logical_name, ref<str> context_identity) const -> String;
    auto add_header(DiscoveryUnitId unit, ref<rstd::path::Path> path)
        -> Result<HeaderArtifactId, String>;
    auto bind_provider(ref<str> key, DiscoveryUnitId provider) -> void;
    auto clone_unit(const IncrementalScanUnit& unit) const -> IncrementalScanUnit;

    const HeaderOwnershipIndex*    ownership_ {};
    Vec<TargetState>               targets_;
    Vec<Vec<TargetId>>             reverse_importers_;
    Vec<String>                    owned_domains_;
    Vec<IncrementalScanUnit>       units_;
    UnitMap                        unit_index_;
    ProviderRegistry               providers_;
    Vec<IncrementalHeaderArtifact> headers_;
    HeaderMap                      header_index_;
    usize                          pending_ {};
    usize                          pending_peak_ {};
    usize                          unresolved_ {};
    usize                          unresolved_peak_ {};
    usize                          reactivations_ {};
};

auto SemanticScanGraphBuilder::make(const PackageMetadata&          package,
                                    const ResolvedNativeTargetPlan& plan,
                                    const HeaderOwnershipIndex&     ownership)
    -> SemanticScanGraphBuilder {
    auto selected = Vec<bool>::with_capacity(package.targets.len());
    auto targets  = Vec<TargetState>::with_capacity(package.targets.len());
    auto reverse  = Vec<Vec<TargetId>>::with_capacity(package.targets.len());
    for (auto target = TargetId {}; target < package.targets.len(); ++target) {
        selected.emplace_back(false);
        reverse.emplace_back();
    }
    for (auto target : plan.target_order) selected[target] = true;
    for (auto target = TargetId {}; target < package.targets.len(); ++target) {
        targets.push(TargetState {
            .identity = package.targets[target].id.clone(),
            .selected = selected[target],
            .sealed   = ! selected[target],
        });
    }
    for (auto importer : plan.target_order) {
        for (auto provider : plan.visible_targets[importer]) {
            if (provider == importer || ! selected[provider]) continue;
            auto repeated = false;
            for (auto existing : reverse[provider]) {
                if (existing == importer) repeated = true;
            }
            if (! repeated) reverse[provider].emplace_back(importer);
        }
    }
    auto domains = Vec<String>::make();
    for (const auto& root : ownership.roots()) {
        auto domain = header_retention_domain(HeaderClassification {
            .owner  = as<Clone>(root.owner).clone(),
            .access = as<Clone>(root.access).clone(),
        });
        if (domain.is_none()) continue;
        auto repeated = false;
        for (const auto& existing : domains) {
            if (existing == domain->as_str()) repeated = true;
        }
        if (! repeated) domains.push(rstd::move(domain).unwrap());
    }
    return SemanticScanGraphBuilder(
        ownership, rstd::move(targets), rstd::move(reverse), rstd::move(domains));
}

auto SemanticScanGraphBuilder::make(const PackageSpec&          package,
                                    const PackagePlan&          plan,
                                    const HeaderOwnershipIndex& ownership)
    -> SemanticScanGraphBuilder {
    auto selected = Vec<bool>::with_capacity(package.targets.len());
    auto targets  = Vec<TargetState>::with_capacity(package.targets.len());
    auto reverse  = Vec<Vec<TargetId>>::with_capacity(package.targets.len());
    for (auto target = TargetId {}; target < package.targets.len(); ++target) {
        selected.emplace_back(false);
        reverse.emplace_back();
    }
    for (auto target : plan.target_order) selected[target] = true;
    for (auto target = TargetId {}; target < package.targets.len(); ++target) {
        targets.push(TargetState {
            .identity = package.targets[target].id.clone(),
            .selected = selected[target],
            .sealed   = ! selected[target],
        });
    }
    for (auto importer : plan.target_order) {
        for (auto provider : plan.visible_targets[importer]) {
            if (provider == importer || ! selected[provider]) continue;
            auto repeated = false;
            for (auto existing : reverse[provider]) {
                if (existing == importer) repeated = true;
            }
            if (! repeated) reverse[provider].emplace_back(importer);
        }
    }
    auto domains = Vec<String>::make();
    for (const auto& root : ownership.roots()) {
        auto domain = header_retention_domain(HeaderClassification {
            .owner  = as<Clone>(root.owner).clone(),
            .access = as<Clone>(root.access).clone(),
        });
        if (domain.is_none()) continue;
        auto repeated = false;
        for (const auto& existing : domains) {
            if (existing == domain->as_str()) repeated = true;
        }
        if (! repeated) domains.push(rstd::move(domain).unwrap());
    }
    return SemanticScanGraphBuilder(
        ownership, rstd::move(targets), rstd::move(reverse), rstd::move(domains));
}

auto SemanticScanGraphBuilder::unit_key(TargetId target, ref<rstd::path::Path> source) const
    -> String {
    return rstd::format("{}:{}", target, source);
}

auto SemanticScanGraphBuilder::provider_key(ref<str> logical_name, ref<str> context_identity) const
    -> String {
    if (is_standard_library_module_name(logical_name)) {
        return rstd::format("stdlib:{}:{}", context_identity, logical_name);
    }
    return rstd::format("project:{}", logical_name);
}

auto SemanticScanGraphBuilder::register_project_unit(TargetId              target,
                                                     ref<rstd::path::Path> source,
                                                     ref<str>              context_identity,
                                                     ref<str> standard_library_context_identity)
    -> DiscoveryUnitId {
    auto key      = unit_key(target, source);
    auto existing = unit_index_.get(key.as_str());
    if (existing.is_some()) return **existing;
    auto unit = units_.len();
    units_.push(IncrementalScanUnit {
        .target           = Some(target),
        .target_identity  = lito::package::package_target_id_text(targets_[target].identity),
        .source           = PathBuf::from(source),
        .context_identity = String::make(context_identity),
        .standard_library_context_identity = String::make(standard_library_context_identity),
    });
    unit_index_.insert(rstd::move(key), unit);
    if (targets_[target].sealed) {
        targets_[target].sealed = false;
        ++reactivations_;
    }
    ++targets_[target].pending;
    ++pending_;
    if (pending_ > pending_peak_) pending_peak_ = pending_;
    return unit;
}

auto SemanticScanGraphBuilder::bind_provider(ref<str> key, DiscoveryUnitId provider) -> void {
    auto found = providers_.get_mut(key);
    if (found.is_some()) {
        (**found).emplace_back(provider);
    } else {
        auto values = Vec<DiscoveryUnitId>::make();
        values.emplace_back(provider);
        providers_.insert(String::make(key), rstd::move(values));
    }
    auto candidates = providers_.get(key);
    if (candidates.is_none() || (**candidates).len() != usize(1)) return;
    for (auto& unit : units_) {
        for (auto& requirement : unit.requirements) {
            if (requirement.provider.is_some() || requirement.provider_key != key) continue;
            requirement.provider = Some(provider);
            --unresolved_;
        }
    }
}

auto SemanticScanGraphBuilder::add_header(DiscoveryUnitId unit, ref<rstd::path::Path> path)
    -> Result<HeaderArtifactId, String> {
    auto metadata = rstd::fs::metadata(path);
    if (metadata.is_err()) {
        return Err(rstd::format(
            "cannot identify scanned header '{}': {}", path, rstd::move(metadata).unwrap_err()));
    }
    auto physical       = rstd::format("{}:{}", metadata->dev(), metadata->ino());
    auto found          = header_index_.get(physical.as_str());
    auto classification = ownership_->classify(path);
    auto header         = HeaderArtifactId {};
    if (found.is_some()) {
        header = **found;
        merge_header_classification(headers_[header].classification, classification);
        auto repeated = false;
        for (const auto& existing : headers_[header].paths) {
            if (existing.as_path() == path) repeated = true;
        }
        if (! repeated) headers_[header].paths.push(PathBuf::from(path));
    } else {
        header     = headers_.len();
        auto paths = Vec<PathBuf>::make();
        paths.push(PathBuf::from(path));
        headers_.push(IncrementalHeaderArtifact {
            .physical_identity = physical.clone(),
            .paths             = rstd::move(paths),
            .classification    = rstd::move(classification),
        });
        header_index_.insert(rstd::move(physical), header);
    }
    auto repeated = false;
    for (auto consumer : headers_[header].consumers) {
        if (consumer == unit) repeated = true;
    }
    if (! repeated) headers_[header].consumers.emplace_back(unit);
    return Ok(header);
}

auto SemanticScanGraphBuilder::complete(DiscoveryUnitId unit, const SourceScanArtifact& artifact)
    -> Result<empty, String> {
    if (unit >= units_.len()) return Err(String::make("scan graph received unknown unit"_str));
    auto& node = units_[unit];
    if (node.complete) return Ok(empty {});
    if (node.context_identity != artifact.context_identity.as_str()) {
        return Err(
            rstd::format("scan graph unit '{}' has a mismatched context", node.source.as_path()));
    }
    node.cpp           = artifact.language.is_Cpp();
    const auto& common = artifact.language.is_C() ? artifact.language.as_C().facts.common
                                                  : artifact.language.as_Cpp().facts.common;
    for (const auto& path : common.header_inputs) {
        auto header = add_header(unit, path.as_path());
        if (header.is_err()) return Err(rstd::move(header).unwrap_err());
        node.header_inputs.push(rstd::move(header).unwrap());
    }
    if (artifact.language.is_Cpp()) {
        const auto& facts = artifact.language.as_Cpp().facts;
        if (facts.provided.is_some()) {
            node.provided = Some(as<Clone>(*facts.provided).clone());
            auto key      = node.target.is_some()
                                ? rstd::format("project:{}", facts.provided->logical_name.as_str())
                                : provider_key(facts.provided->logical_name.as_str(),
                                               node.context_identity.as_str());
            bind_provider(key.as_str(), unit);
        }
        for (const auto& required : facts.required_modules) {
            auto key        = provider_key(required.logical_name.as_str(),
                                           node.standard_library_context_identity.as_str());
            auto provider   = Option<DiscoveryUnitId> {};
            auto candidates = providers_.get(key.as_str());
            if (candidates.is_some() && ! (**candidates).is_empty()) {
                auto selected = (**candidates)[usize {}];
                provider      = Some(rstd::move(selected));
            } else {
                ++unresolved_;
                if (unresolved_ > unresolved_peak_) unresolved_peak_ = unresolved_;
            }
            node.requirements.push(IncrementalModuleRequirement {
                .requirement  = required.clone(),
                .provider_key = rstd::move(key),
                .provider     = provider,
            });
        }
    }
    node.complete = true;
    if (node.target.is_some()) {
        --targets_[*node.target].pending;
        --pending_;
    }
    return Ok(empty {});
}

auto SemanticScanGraphBuilder::seal_ready_targets() -> Vec<String> {
    auto released = Vec<String>::make();
    auto changed  = true;
    while (changed) {
        changed = false;
        for (auto target = TargetId {}; target < targets_.len(); ++target) {
            auto& state = targets_[target];
            if (! state.selected || state.sealed || state.pending != usize {}) continue;
            auto importers_sealed = true;
            for (auto importer : reverse_importers_[target]) {
                if (! targets_[importer].sealed) importers_sealed = false;
            }
            if (! importers_sealed) continue;
            state.sealed = true;
            changed      = true;
            released.push(header_target_retention_domain(state.identity));
        }
    }
    return released;
}

auto SemanticScanGraphBuilder::finish_discovery() -> Result<Vec<String>, String> {
    if (pending_ != usize {}) {
        return Err(rstd::format("scan graph discovery closed with {} pending units", pending_));
    }
    auto released = seal_ready_targets();
    for (auto target = TargetId {}; target < targets_.len(); ++target) {
        auto& state = targets_[target];
        if (! state.selected || state.sealed) continue;
        state.sealed = true;
        released.push(header_target_retention_domain(state.identity));
    }
    for (const auto& domain : owned_domains_) {
        auto repeated = false;
        for (const auto& existing : released) {
            if (existing == domain.as_str()) repeated = true;
        }
        if (! repeated) released.push(domain.clone());
    }
    return Ok(rstd::move(released));
}

auto SemanticScanGraphBuilder::register_completed_standard_unit(const PreparedUnit& unit,
                                                                const ScanResult&   scan)
    -> Result<DiscoveryUnitId, String> {
    auto id = units_.len();
    units_.push(IncrementalScanUnit {
        .target           = None(),
        .target_identity  = rstd::format("standard-library:{}", unit.unit.source.as_path()),
        .source           = unit.unit.source.clone(),
        .context_identity = unit.unit.context->scan_id.clone(),
        .standard_library_context_identity = unit.unit.standard_library_context_identity.clone(),
    });
    auto& node         = units_[id];
    node.cpp           = scan.language.is_Cpp();
    const auto& common = scan_common(scan);
    for (const auto& path : common.header_inputs) {
        auto header = add_header(id, path.as_path());
        if (header.is_err()) return Err(rstd::move(header).unwrap_err());
        node.header_inputs.push(rstd::move(header).unwrap());
    }
    if (scan.language.is_Cpp()) {
        const auto& facts = scan.language.as_Cpp().facts;
        if (facts.provided.is_some()) {
            node.provided = Some(as<Clone>(*facts.provided).clone());
            auto key      = provider_key(facts.provided->logical_name.as_str(),
                                         node.standard_library_context_identity.as_str());
            bind_provider(key.as_str(), id);
        }
        for (const auto& required : facts.required_modules) {
            auto key        = provider_key(required.logical_name.as_str(),
                                           node.standard_library_context_identity.as_str());
            auto provider   = Option<DiscoveryUnitId> {};
            auto candidates = providers_.get(key.as_str());
            if (candidates.is_some() && ! (**candidates).is_empty()) {
                auto selected = (**candidates)[usize {}];
                provider      = Some(rstd::move(selected));
            } else {
                ++unresolved_;
                if (unresolved_ > unresolved_peak_) unresolved_peak_ = unresolved_;
            }
            node.requirements.push(IncrementalModuleRequirement {
                .requirement  = required.clone(),
                .provider_key = rstd::move(key),
                .provider     = provider,
            });
        }
    }
    node.complete = true;
    return Ok(id);
}

auto SemanticScanGraphBuilder::clone_unit(const IncrementalScanUnit& unit) const
    -> IncrementalScanUnit {
    auto requirements = Vec<IncrementalModuleRequirement>::with_capacity(unit.requirements.len());
    for (const auto& requirement : unit.requirements) {
        requirements.push(IncrementalModuleRequirement {
            .requirement  = requirement.requirement.clone(),
            .provider_key = requirement.provider_key.clone(),
            .provider     = requirement.provider.clone(),
        });
    }
    return IncrementalScanUnit {
        .target                            = unit.target.clone(),
        .target_identity                   = unit.target_identity.clone(),
        .source                            = unit.source.clone(),
        .context_identity                  = unit.context_identity.clone(),
        .standard_library_context_identity = unit.standard_library_context_identity.clone(),
        .cpp                               = unit.cpp,
        .complete                          = unit.complete,
        .provided      = unit.provided.is_some() ? Some(as<Clone>(*unit.provided).clone()) : None(),
        .requirements  = rstd::move(requirements),
        .header_inputs = unit.header_inputs.clone(),
    };
}

auto SemanticScanGraphBuilder::finalize(
    const Vec<PreparedUnit>& units,
    const Vec<ScanResult>&   scans) && -> Result<IncrementalSemanticScanGraph, String> {
    if (units.len() != scans.len()) {
        return Err(String::make("scan graph received mismatched final units and scans"_str));
    }
    for (auto unit = UnitId {}; unit < units.len(); ++unit) {
        if (units[unit].unit.owner.is_StandardLibrary()) {
            auto registered = register_completed_standard_unit(units[unit], scans[unit]);
            if (registered.is_err()) return Err(rstd::move(registered).unwrap_err());
        }
    }
    auto mapping = Vec<Option<UnitId>>::with_capacity(units_.len());
    for (auto ignored = usize {}; ignored < units_.len(); ++ignored) mapping.push(None());
    auto aligned_ids = Vec<DiscoveryUnitId>::with_capacity(units.len());
    for (auto unit = UnitId {}; unit < units.len(); ++unit) {
        auto found = Option<DiscoveryUnitId> {};
        if (units[unit].unit.owner.is_Project()) {
            auto key   = unit_key(units[unit].unit.owner.as_Project().target,
                                  units[unit].unit.source.as_path());
            auto entry = unit_index_.get(key.as_str());
            if (entry.is_some()) {
                auto selected = **entry;
                found         = Some(rstd::move(selected));
            }
        } else {
            for (auto candidate = DiscoveryUnitId {}; candidate < units_.len(); ++candidate) {
                if (mapping[candidate].is_some() || units_[candidate].target.is_some()) continue;
                if (units_[candidate].source.as_path() == units[unit].unit.source.as_path() &&
                    units_[candidate].standard_library_context_identity.as_str() ==
                        units[unit].unit.standard_library_context_identity.as_str()) {
                    found = Some(candidate);
                    break;
                }
            }
        }
        if (found.is_none()) {
            return Err(rstd::format("scan graph has no unit for source '{}'",
                                    units[unit].unit.source.as_path()));
        }
        mapping[*found] = Some(unit);
        aligned_ids.emplace_back(*found);
    }
    auto aligned = Vec<IncrementalScanUnit>::with_capacity(units.len());
    for (auto discovery : aligned_ids) {
        auto node = clone_unit(units_[discovery]);
        for (auto& requirement : node.requirements) {
            if (requirement.provider.is_none()) continue;
            auto mapped          = mapping[*requirement.provider];
            requirement.provider = mapped;
        }
        aligned.push(rstd::move(node));
    }
    for (auto& header : headers_) {
        auto consumers = Vec<DiscoveryUnitId>::make();
        for (auto consumer : header.consumers) {
            if (mapping[consumer].is_some()) consumers.emplace_back(*mapping[consumer]);
        }
        header.consumers = rstd::move(consumers);
    }
    auto statistics    = SemanticScanGraphStatistics {};
    statistics.headers = headers_.len();
    for (const auto& header : headers_) {
        const auto& owner = header.classification.owner;
        if (owner.is_ProjectPackage())
            ++statistics.project_headers;
        else if (owner.is_ExternalTarget())
            ++statistics.external_headers;
        else if (owner.is_Toolchain())
            ++statistics.toolchain_headers;
        else if (owner.is_Ambiguous())
            ++statistics.ambiguous_headers;
        else
            ++statistics.unknown_headers;
    }
    for (const auto& unit : aligned) statistics.header_edges += unit.header_inputs.len();
    statistics.pending_peak               = pending_peak_;
    statistics.unresolved_peak            = unresolved_peak_;
    statistics.reactivations              = reactivations_;
    statistics.incremental_retained_bytes = incremental_graph_retained_bytes(aligned, headers_);
    return Ok(IncrementalSemanticScanGraph {
        .units      = rstd::move(aligned),
        .headers    = rstd::move(headers_),
        .statistics = statistics,
    });
}

struct ResolvedSemanticBuildGraph {
    Vec<UnitId>                 c_units;
    Vec<UnitId>                 cpp_units;
    Vec<UnitId>                 compile_order;
    Vec<Vec<UnitId>>            direct_inputs;
    Vec<Vec<UnitId>>            resolved_inputs;
    Vec<Vec<UnitId>>            public_inputs;
    SemanticScanGraphStatistics statistics;
};

auto resolved_graph_retained_bytes(const ResolvedSemanticBuildGraph& graph) noexcept -> usize {
    auto result = graph.c_units.capacity() * usize(sizeof(UnitId)) +
                  graph.cpp_units.capacity() * usize(sizeof(UnitId)) +
                  graph.compile_order.capacity() * usize(sizeof(UnitId)) +
                  graph.direct_inputs.capacity() * usize(sizeof(Vec<UnitId>)) +
                  graph.resolved_inputs.capacity() * usize(sizeof(Vec<UnitId>)) +
                  graph.public_inputs.capacity() * usize(sizeof(Vec<UnitId>));
    for (const auto& inputs : graph.direct_inputs) {
        result += inputs.capacity() * usize(sizeof(UnitId));
    }
    for (const auto& inputs : graph.resolved_inputs) {
        result += inputs.capacity() * usize(sizeof(UnitId));
    }
    for (const auto& inputs : graph.public_inputs) {
        result += inputs.capacity() * usize(sizeof(UnitId));
    }
    return result;
}

auto resolve_semantic_build(const PackagePlan&           package,
                            const Vec<PreparedUnit>&     units,
                            const Vec<ScanResult>&       scans,
                            IncrementalSemanticScanGraph incremental,
                            const BmiFormatIdentity&     format)
    -> ModuleResult<ResolvedSemanticBuildGraph> {
    if (units.len() != scans.len() || units.len() != incremental.units.len()) {
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
            scan.language.is_C() != units[unit].unit.context->language.is_C() ||
            scan.language.is_C() == incremental.units[unit].cpp) {
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
        const auto& facts = incremental.units[unit];
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
        const auto& facts = incremental.units[unit];
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
        const auto& facts = incremental.units[unit];
        auto        names = StringSet::make();
        for (const auto& resolved_requirement : facts.requirements) {
            const auto& required = resolved_requirement.requirement;
            auto        provider =
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
        const auto& facts = incremental.units[unit];
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
        const auto& facts           = incremental.units[unit];
        auto        importer_target = project_target(units[scan.unit].unit);
        if (importer_target.is_none()) continue;
        for (const auto& resolved_requirement : facts.requirements) {
            const auto& required = resolved_requirement.requirement;
            auto        provider =
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

    auto resolved = ResolvedSemanticBuildGraph {
        .c_units         = rstd::move(c_units),
        .cpp_units       = rstd::move(cpp_units),
        .compile_order   = rstd::move(compile_order),
        .direct_inputs   = rstd::move(direct_inputs),
        .resolved_inputs = rstd::move(resolved_inputs),
        .public_inputs   = rstd::move(public_inputs),
        .statistics      = incremental.statistics,
    };
    resolved.statistics.resolved_retained_bytes = resolved_graph_retained_bytes(resolved);
    return Ok(rstd::move(resolved));
}

} // namespace lito::cpp
