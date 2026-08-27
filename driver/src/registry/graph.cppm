module;
#include <rstd/macro.hpp>

export module lito.driver:registry.graph;

import rstd;
import lito.core;
import :config.registry;
import :registry.index;
import :registry.release;
import :registry.source;

using namespace rstd::prelude;
using namespace rstd::literals;
using PathBuf = rstd::path::PathBuf;

export namespace lito::registry
{

struct RegistryGraphRequirement {
    Option<String>      registry;
    RegistryPackageName package;
    VersionRequirement  requirement;
    String              source;
};

struct ResolvedRegistryGraphSource {
    RegistryPackageId                   package;
    RegistryReleaseProjection           release;
    lito::source::ResolvedPackageSource source;
    lito::workspace::WorkspaceCatalog   catalog;
};

struct RegistryGraphError {
    String message;
};

template<typename T>
using RegistryGraphResult = Result<T, RegistryGraphError>;

struct RegistryGraphProvider {
    void*                                      context {};
    const lito::source::ResolvedPackageSource* root_source {};
    RegistryGraphResult<Vec<ResolvedRegistryGraphSource>> (
        *resolve)(void*, slice<RegistryGraphRequirement>) noexcept {};
};

class RegistryGraphClient {
    PathBuf                                  cache_root_;
    const lito::config::LitoBootstrapConfig* config_ {};
    RegistryNetworkPolicy                    network_ { RegistryNetworkPolicy::Online };
    RegistryHttpTransport                    http_;
    RegistryBlobTransport                    blobs_;
    bool                                     locked_mode_ {};
    Vec<lito::source::RegistrySourcePin>     locked_;
    Vec<VerifiedPackageIndex>                verified_indices_;
    Vec<RegistryPackageId>                   development_packages_;
    const Vec<PathBuf>*                      source_bundles_ {};

    static auto load_index(void*, const RegistryPackageId&) noexcept -> RegistryIndexLoadResult;
    static auto resolve_callback(void*, slice<RegistryGraphRequirement>) noexcept
        -> RegistryGraphResult<Vec<ResolvedRegistryGraphSource>>;
    auto resolve_locked(Vec<RegistrySolverRequirement> roots)
        -> RegistryGraphResult<Vec<ResolvedRegistryPackage>>;
    auto materialize(Vec<ResolvedRegistryPackage> packages)
        -> RegistryGraphResult<Vec<ResolvedRegistryGraphSource>>;

public:
    RegistryGraphClient(PathBuf                                  cache_root,
                        const lito::config::LitoBootstrapConfig& config,
                        RegistryNetworkPolicy                    network,
                        RegistryHttpTransport                    http,
                        RegistryBlobTransport                    blobs,
                        bool                                     locked_mode,
                        Vec<lito::source::RegistrySourcePin>     locked         = {},
                        const Vec<PathBuf>*                      source_bundles = nullptr)
        : cache_root_(rstd::move(cache_root)),
          config_(rstd::addressof(config)),
          network_(network),
          http_(http),
          blobs_(blobs),
          locked_mode_(locked_mode),
          locked_(rstd::move(locked)),
          source_bundles_(source_bundles) {}

    auto resolve(slice<RegistryGraphRequirement> requirements)
        -> RegistryGraphResult<Vec<ResolvedRegistryGraphSource>>;
    auto resolve_package(const RegistryPackageSpec& spec,
                         Option<String>             registry = None(),
                         ref<str>                   source   = "Registry install"_str)
        -> RegistryGraphResult<Vec<ResolvedRegistryGraphSource>>;
    auto provider() noexcept -> RegistryGraphProvider {
        return RegistryGraphProvider {
            .context = this,
            .resolve = resolve_callback,
        };
    }
};

} // namespace lito::registry

namespace
{

using namespace lito::registry;

template<typename T>
auto graph_failure(String message) -> RegistryGraphResult<T> {
    return Err(RegistryGraphError { .message = rstd::move(message) });
}

template<typename T>
auto graph_failure(ref<str> message) -> RegistryGraphResult<T> {
    return graph_failure<T>(String::make(message));
}

auto configured_registry(const lito::config::LitoBootstrapConfig& config, ref<str> selector)
    -> Option<ref<lito::config::NamedRegistryConfig>> {
    auto named = config.registry(selector);
    if (named.is_some()) return named;
    auto identity = RegistryId::parse(selector);
    if (identity.is_err()) return None();
    for (const auto& registry : *config.registries()) {
        if (registry.identity == *identity) {
            return Some(ref<lito::config::NamedRegistryConfig>::from_raw_parts(&registry));
        }
    }
    return None();
}

auto configured_registry(const lito::config::LitoBootstrapConfig& config,
                         const RegistryId&                        identity)
    -> Option<ref<lito::config::NamedRegistryConfig>> {
    for (const auto& registry : *config.registries()) {
        if (registry.identity == identity) {
            return Some(ref<lito::config::NamedRegistryConfig>::from_raw_parts(&registry));
        }
    }
    return None();
}

auto requirement_registry(const lito::config::LitoBootstrapConfig& config,
                          const RegistryGraphRequirement&          requirement)
    -> RegistryGraphResult<ref<lito::config::NamedRegistryConfig>> {
    auto selected = requirement.registry.is_some()
                        ? configured_registry(config, requirement.registry->as_str())
                        : config.default_registry();
    if (selected.is_none()) {
        return graph_failure<ref<lito::config::NamedRegistryConfig>>(
            requirement.registry.is_some()
                ? rstd::format("Registry dependency '{}' selects unknown Registry '{}'",
                               requirement.package.as_str(),
                               requirement.registry->as_str())
                : rstd::format("Registry dependency '{}' has no configured default Registry",
                               requirement.package.as_str()));
    }
    return Ok(*selected);
}

auto graph_index_error(RegistryIndexError error) -> RegistryGraphError {
    return RegistryGraphError { .message = rstd::move(error.message) };
}

auto graph_artifact_error(RegistryArtifactError error) -> RegistryGraphError {
    return RegistryGraphError { .message = rstd::move(error.message) };
}

struct LockedPackageState {
    RegistryPackageId                 package;
    Vec<RegistryConstraintTrace>      constraints;
    Option<RegistryReleaseProjection> release;
};

auto locked_state_position(const Vec<LockedPackageState>& states,
                           const RegistryPackageName&     package) -> Option<usize> {
    for (usize index {}; index < states.len(); ++index) {
        if (states[index].package.name == package) return Some(index);
    }
    return None();
}

auto locked_pin(const Vec<lito::source::RegistrySourcePin>& pins, const RegistryPackageId& package)
    -> Option<ref<lito::source::RegistrySourcePin>> {
    for (const auto& pin : pins) {
        if (pin.package == package) {
            return Some(ref<lito::source::RegistrySourcePin>::from_raw_parts(rstd::addressof(pin)));
        }
    }
    return None();
}

auto append_locked_constraint(Vec<LockedPackageState>& states,
                              const RegistryPackageId& package,
                              VersionRequirement       requirement,
                              String                   source) -> RegistryGraphResult<empty> {
    auto position = locked_state_position(states, package.name);
    if (position.is_none()) {
        auto constraints = Vec<RegistryConstraintTrace>::make();
        constraints.push(RegistryConstraintTrace {
            .requirement = String::make(requirement.text()),
            .source      = rstd::move(source),
        });
        states.push(LockedPackageState {
            .package     = package.clone(),
            .constraints = rstd::move(constraints),
        });
        return Ok(empty {});
    }
    auto& state = states[*position];
    if (! (state.package == package)) {
        return graph_failure<empty>(
            rstd::format("Registry package '{}' is required from both '{}' and '{}'",
                         package.name.as_str(),
                         state.package.registry.as_str(),
                         package.registry.as_str()));
    }
    if (state.release.is_some() && ! requirement.matches(state.release->version)) {
        return graph_failure<empty>(
            rstd::format("locked Registry package '{}@{}' does not satisfy '{}' from {}",
                         package.name.as_str(),
                         state.release->version.text().as_str(),
                         requirement.text(),
                         source.as_str()));
    }
    state.constraints.push(RegistryConstraintTrace {
        .requirement = String::make(requirement.text()),
        .source      = rstd::move(source),
    });
    return Ok(empty {});
}

} // namespace

auto lito::registry::RegistryGraphClient::load_index(void*                    context,
                                                     const RegistryPackageId& package) noexcept
    -> RegistryIndexLoadResult {
    auto& self = *static_cast<RegistryGraphClient*>(context);
    if (self.config_ == nullptr) {
        return Err(RegistryIndexError {
            .kind    = RegistryIndexErrorKind::ContextMismatch,
            .package = package.clone(),
            .message = String::make("Registry graph client has no bootstrap config"_str),
        });
    }
    for (const auto& index : self.verified_indices_) {
        if (index.package() == package) return Ok(index.clone());
    }
    auto config = configured_registry(*self.config_, package.registry);
    if (config.is_none()) {
        return Err(RegistryIndexError {
            .kind    = RegistryIndexErrorKind::ContextMismatch,
            .package = package.clone(),
            .message = rstd::format("Registry '{}' is not configured", package.registry.as_str()),
        });
    }
    auto client =
        RegistryIndexClient(self.cache_root_.clone(), **config, self.network_, self.http_);
    auto loaded = client.load(package);
    if (loaded.is_err()) return Err(rstd::move(loaded).unwrap_err());
    self.verified_indices_.push(loaded->clone());
    return Ok(rstd::move(loaded).unwrap());
}

auto lito::registry::RegistryGraphClient::resolve_callback(
    void*                           context,
    slice<RegistryGraphRequirement> requirements) noexcept
    -> RegistryGraphResult<Vec<ResolvedRegistryGraphSource>> {
    return static_cast<RegistryGraphClient*>(context)->resolve(requirements);
}

auto lito::registry::RegistryGraphClient::resolve_locked(Vec<RegistrySolverRequirement> roots)
    -> RegistryGraphResult<Vec<ResolvedRegistryPackage>> {
    auto states = Vec<LockedPackageState>::make();
    for (auto& root : roots) {
        auto appended = append_locked_constraint(
            states, root.package, rstd::move(root.requirement), rstd::move(root.source));
        if (appended.is_err()) return Err(rstd::move(appended).unwrap_err());
    }
    for (usize position {}; position < states.len(); ++position) {
        if (position >= usize(1024)) {
            return graph_failure<Vec<ResolvedRegistryPackage>>(
                "locked Registry graph exceeds 1024 packages"_str);
        }
        auto& state = states[position];
        auto  pin   = locked_pin(locked_, state.package);
        if (pin.is_none()) {
            return graph_failure<Vec<ResolvedRegistryPackage>>(
                rstd::format("--locked has no exact Registry release for package '{}'",
                             state.package.name.as_str()));
        }
        for (const auto& constraint : state.constraints) {
            auto requirement = VersionRequirement::parse(constraint.requirement.as_str());
            if (requirement.is_err()) {
                return graph_failure<Vec<ResolvedRegistryPackage>>(
                    rstd::format("locked Registry constraint for '{}' is invalid: {}",
                                 state.package.name.as_str(),
                                 rstd::move(requirement).unwrap_err()));
            }
            if (! requirement->matches((*pin)->version)) {
                return graph_failure<Vec<ResolvedRegistryPackage>>(
                    rstd::format("locked Registry package '{}@{}' does not satisfy '{}' from {}",
                                 state.package.name.as_str(),
                                 (*pin)->version.text().as_str(),
                                 constraint.requirement.as_str(),
                                 constraint.source.as_str()));
            }
        }
        auto config = configured_registry(*config_, state.package.registry);
        if (config.is_none()) {
            return graph_failure<Vec<ResolvedRegistryPackage>>(
                rstd::format("Registry '{}' is not configured", state.package.registry.as_str()));
        }
        auto client =
            RegistryReleaseClient(cache_root_.clone(), **config, network_, http_, source_bundles_);
        auto loaded = client.load(state.package, (*pin)->release, rstd::addressof((*pin)->version));
        if (loaded.is_err()) return Err(graph_index_error(rstd::move(loaded).unwrap_err()));
        auto release = loaded->release().clone();
        if (! (release.version == (*pin)->version)) {
            return graph_failure<Vec<ResolvedRegistryPackage>>(rstd::format(
                "locked Registry version '{}@{}' resolves to immutable release version '{}'",
                state.package.name.as_str(),
                (*pin)->version.text().as_str(),
                release.version.text().as_str()));
        }
        auto package  = state.package.clone();
        state.release = Some(release.clone());
        for (const auto& dependency : release.dependencies) {
            if (dependency.kind == RegistryDependencyKind::Development) continue;
            auto source   = rstd::format("{}@{} dependency '{}'",
                                         registry_package_id_text(package).as_str(),
                                         release.version.text().as_str(),
                                         dependency.alias.as_str());
            auto appended = append_locked_constraint(
                states, dependency.package, dependency.requirement.clone(), rstd::move(source));
            if (appended.is_err()) return Err(rstd::move(appended).unwrap_err());
        }
    }
    auto result = Vec<ResolvedRegistryPackage>::with_capacity(states.len());
    for (auto& state : states) {
        result.push(ResolvedRegistryPackage {
            .package = rstd::move(state.package),
            .release = rstd::move(state.release).unwrap(),
        });
    }
    return Ok(rstd::move(result));
}

auto lito::registry::RegistryGraphClient::materialize(Vec<ResolvedRegistryPackage> packages)
    -> RegistryGraphResult<Vec<ResolvedRegistryGraphSource>> {
    auto result = Vec<ResolvedRegistryGraphSource>::with_capacity(packages.len());
    for (auto& selected : packages) {
        auto config = configured_registry(*config_, selected.package.registry);
        if (config.is_none()) {
            return graph_failure<Vec<ResolvedRegistryGraphSource>>(rstd::format(
                "Registry '{}' is not configured", selected.package.registry.as_str()));
        }
        auto sources      = RegistrySourceResolver(cache_root_.clone(),
                                                   (**config).effective_endpoints()->blob.clone(),
                                                   network_,
                                                   blobs_,
                                                   source_bundles_);
        auto materialized = sources.materialize(selected.package, selected.release);
        if (materialized.is_err()) {
            return Err(graph_artifact_error(rstd::move(materialized).unwrap_err()));
        }
        auto source = rstd::move(materialized).unwrap();
        result.push(ResolvedRegistryGraphSource {
            .package = selected.package.clone(),
            .release = selected.release.clone(),
            .source  = rstd::move(source.source),
            .catalog = rstd::move(source.catalog),
        });
    }
    return Ok(rstd::move(result));
}

auto lito::registry::RegistryGraphClient::resolve(slice<RegistryGraphRequirement> requirements)
    -> RegistryGraphResult<Vec<ResolvedRegistryGraphSource>> {
    if (config_ == nullptr) {
        return graph_failure<Vec<ResolvedRegistryGraphSource>>(
            "Registry graph client has no bootstrap config"_str);
    }
    auto roots = Vec<RegistrySolverRequirement>::with_capacity(requirements.len());
    for (const auto& requirement : requirements) {
        auto registry = requirement_registry(*config_, requirement);
        if (registry.is_err()) return Err(rstd::move(registry).unwrap_err());
        roots.push(RegistrySolverRequirement {
            .package =
                RegistryPackageId {
                    .registry = (**registry).identity.clone(),
                    .name     = requirement.package.clone(),
                },
            .requirement = requirement.requirement.clone(),
            .source      = requirement.source.clone(),
        });
    }
    if (locked_mode_) {
        auto selected = resolve_locked(rstd::move(roots));
        if (selected.is_err()) return Err(rstd::move(selected).unwrap_err());
        return materialize(rstd::move(selected).unwrap());
    }
    auto locked = Vec<RegistryLockedPreference>::with_capacity(locked_.len());
    for (const auto& pin : locked_) {
        locked.push(RegistryLockedPreference {
            .package = pin.package.clone(),
            .version = pin.version.clone(),
            .release = pin.release.clone(),
        });
    }
    auto solved = RegistryVersionSolver::solve(
        RegistrySolverInput {
            .roots                = rstd::move(roots),
            .locked               = rstd::move(locked),
            .development_packages = rstd::move(development_packages_),
        },
        RegistryIndexProvider {
            .context = this,
            .load    = load_index,
        });
    if (solved.is_err()) {
        return graph_failure<Vec<ResolvedRegistryGraphSource>>(rstd::format(
            "Registry version resolution failed: {}", rstd::move(solved).unwrap_err()));
    }

    auto graph = rstd::move(solved).unwrap();
    for (auto& selected : graph.packages) {
        auto config = configured_registry(*config_, selected.package.registry);
        if (config.is_none()) {
            return graph_failure<Vec<ResolvedRegistryGraphSource>>(rstd::format(
                "Registry '{}' is not configured", selected.package.registry.as_str()));
        }
        auto releases =
            RegistryReleaseClient(cache_root_.clone(), **config, network_, http_, source_bundles_);
        auto immutable = releases.load(
            selected.package, selected.release.release, rstd::addressof(selected.release.version));
        if (immutable.is_err()) return Err(graph_index_error(rstd::move(immutable).unwrap_err()));
        if (! registry_immutable_release_matches(selected.release, immutable->release())) {
            return graph_failure<Vec<ResolvedRegistryGraphSource>>(
                rstd::format("Registry release '{}' does not match its package index projection",
                             selected.release.release.text()));
        }
        selected.release = immutable->release().clone();
    }
    return materialize(rstd::move(graph.packages));
}

auto lito::registry::RegistryGraphClient::resolve_package(const RegistryPackageSpec& spec,
                                                          Option<String>             registry,
                                                          ref<str>                   source)
    -> RegistryGraphResult<Vec<ResolvedRegistryGraphSource>> {
    if (config_ == nullptr) {
        return graph_failure<Vec<ResolvedRegistryGraphSource>>(
            "Registry graph client has no bootstrap config"_str);
    }
    auto selected = registry.is_some() ? configured_registry(*config_, registry->as_str())
                                       : config_->default_registry();
    if (selected.is_none()) {
        return graph_failure<Vec<ResolvedRegistryGraphSource>>(
            registry.is_some()
                ? rstd::format("Registry package '{}' selects unknown Registry '{}'",
                               spec.package.as_str(),
                               registry->as_str())
                : rstd::format("Registry package '{}' has no configured default Registry",
                               spec.package.as_str()));
    }
    auto requirement = Option<VersionRequirement> {};
    if (spec.selector.is_Requirement()) {
        requirement = Some(spec.selector.as_Requirement().requirement.clone());
    } else {
        if (locked_mode_) {
            return graph_failure<Vec<ResolvedRegistryGraphSource>>(
                "--locked Registry install cannot resolve a moving tag"_str);
        }
        auto package = RegistryPackageId {
            .registry = (**selected).identity.clone(),
            .name     = spec.package.clone(),
        };
        auto index = load_index(this, package);
        if (index.is_err()) return Err(graph_index_error(rstd::move(index).unwrap_err()));
        auto version = resolve_registry_tag(*index, spec.selector.as_NamedTag().tag.as_str());
        if (version.is_err()) {
            return graph_failure<Vec<ResolvedRegistryGraphSource>>(
                rstd::format("cannot resolve Registry tag '{}@{}': {}",
                             spec.package.as_str(),
                             spec.selector.as_NamedTag().tag.as_str(),
                             rstd::move(version).unwrap_err()));
        }
        auto exact =
            VersionRequirement::parse(rstd::format("={}", version->text().as_str()).as_str());
        if (exact.is_err()) {
            return graph_failure<Vec<ResolvedRegistryGraphSource>>(rstd::format(
                "cannot construct exact Registry requirement: {}", rstd::move(exact).unwrap_err()));
        }
        requirement = Some(rstd::move(exact).unwrap());
    }
    auto requirements = Vec<RegistryGraphRequirement>::make();
    development_packages_.push(RegistryPackageId {
        .registry = (**selected).identity.clone(),
        .name     = spec.package.clone(),
    });
    requirements.push(RegistryGraphRequirement {
        .registry    = rstd::move(registry),
        .package     = spec.package.clone(),
        .requirement = rstd::move(requirement).unwrap(),
        .source      = String::make(source),
    });
    return resolve(requirements.as_slice());
}
