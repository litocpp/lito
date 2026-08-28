module;
#include <rstd/macro.hpp>

export module lito.driver:registry.graph;

import rstd;
import lito.core;
import :config.registry;
import :registry.index;
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
    SemanticVersion                     version;
    lito::source::ResolvedPackageSource source;
    lito::workspace::WorkspaceCatalog   catalog;
};

struct RegistryGraphError {
    String message;
};

template<typename T>
using RegistryGraphResult = Result<T, RegistryGraphError>;

struct BuiltinRegistryPackage {
    RegistryPackageId package;
    SemanticVersion   version;
};

struct RegistryGraphProvider {
    void*                                      context {};
    const lito::source::ResolvedPackageSource* root_source {};
    RegistryGraphResult<Vec<ResolvedRegistryGraphSource>> (
        *resolve)(void*, slice<RegistryGraphRequirement>) noexcept {};
    RegistryGraphResult<BuiltinRegistryPackage> (*resolve_builtin)(void*, ref<str>) noexcept {};
};

class RegistryGraphClient {
    PathBuf                                  cache_root_;
    const lito::config::LitoBootstrapConfig* config_ {};
    RegistryNetworkPolicy                    network_ { RegistryNetworkPolicy::Online };
    RegistryHttpTransport                    http_;
    RegistryBlobTransport                    blobs_;
    bool                                     locked_mode_ {};
    Vec<lito::source::RegistrySourcePin>     locked_;
    Vec<RegistryPackageIndex>                indices_;
    Vec<RegistryPackageId>                   development_packages_;
    const Vec<PathBuf>*                      source_bundles_ {};

    static auto load_index(void*, const RegistryPackageId&) noexcept -> RegistryIndexLoadResult;
    static auto resolve_callback(void*, slice<RegistryGraphRequirement>) noexcept
        -> RegistryGraphResult<Vec<ResolvedRegistryGraphSource>>;
    auto resolve_locked(Vec<RegistrySolverRequirement> roots)
        -> RegistryGraphResult<Vec<lito::source::RegistrySourcePin>>;
    auto materialize(Vec<lito::source::RegistrySourcePin> packages)
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
    auto add_index(RegistryPackageIndex index) -> void { indices_.push(rstd::move(index)); }
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

auto graph_artifact_error(RegistryArtifactError error) -> RegistryGraphError {
    return RegistryGraphError { .message = rstd::move(error.message) };
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
    for (const auto& index : self.indices_) {
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
    self.indices_.push(loaded->clone());
    return Ok(rstd::move(loaded).unwrap());
}

auto lito::registry::RegistryGraphClient::resolve_callback(
    void*                           context,
    slice<RegistryGraphRequirement> requirements) noexcept
    -> RegistryGraphResult<Vec<ResolvedRegistryGraphSource>> {
    return static_cast<RegistryGraphClient*>(context)->resolve(requirements);
}

auto lito::registry::RegistryGraphClient::resolve_locked(Vec<RegistrySolverRequirement> roots)
    -> RegistryGraphResult<Vec<lito::source::RegistrySourcePin>> {
    for (const auto& root : roots) {
        auto pin = locked_pin(locked_, root.package);
        if (pin.is_none()) {
            return graph_failure<Vec<lito::source::RegistrySourcePin>>(
                rstd::format("--locked has no exact Registry release for package '{}'",
                             root.package.name.as_str()));
        }
        if (! root.requirement.matches((*pin)->version)) {
            return graph_failure<Vec<lito::source::RegistrySourcePin>>(
                rstd::format("locked Registry package '{}@{}' does not satisfy '{}' from {}",
                             root.package.name.as_str(),
                             (*pin)->version.text().as_str(),
                             root.requirement.text(),
                             root.source.as_str()));
        }
    }
    auto result = Vec<lito::source::RegistrySourcePin>::with_capacity(locked_.len());
    for (const auto& pin : locked_) result.push(pin.clone());
    return Ok(rstd::move(result));
}

auto lito::registry::RegistryGraphClient::materialize(Vec<lito::source::RegistrySourcePin> packages)
    -> RegistryGraphResult<Vec<ResolvedRegistryGraphSource>> {
    auto result = Vec<ResolvedRegistryGraphSource>::with_capacity(packages.len());
    for (auto& selected : packages) {
        auto config = configured_registry(*config_, selected.package.registry);
        if (config.is_none()) {
            return graph_failure<Vec<ResolvedRegistryGraphSource>>(rstd::format(
                "Registry '{}' is not configured", selected.package.registry.as_str()));
        }
        auto sources = RegistrySourceResolver(cache_root_.clone(),
                                              (**config).effective_endpoints()->blob.clone(),
                                              network_,
                                              blobs_,
                                              source_bundles_);
        auto materialized =
            sources.materialize(selected.package, selected.version, selected.checksum);
        if (materialized.is_err()) {
            return Err(graph_artifact_error(rstd::move(materialized).unwrap_err()));
        }
        auto source = rstd::move(materialized).unwrap();
        result.push(ResolvedRegistryGraphSource {
            .package = selected.package.clone(),
            .version = selected.version.clone(),
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
            .package  = pin.package.clone(),
            .version  = pin.version.clone(),
            .checksum = pin.checksum.clone(),
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

    auto graph    = rstd::move(solved).unwrap();
    auto selected = Vec<lito::source::RegistrySourcePin>::with_capacity(graph.packages.len());
    for (auto& package : graph.packages) {
        selected.push(lito::source::RegistrySourcePin {
            .package  = rstd::move(package.package),
            .version  = rstd::move(package.release.version),
            .checksum = rstd::move(package.release.checksum),
        });
    }
    return materialize(rstd::move(selected));
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
        return graph_failure<Vec<ResolvedRegistryGraphSource>>(
            "Registry named tags are not supported"_str);
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
