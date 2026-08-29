module;
#include <rstd/macro.hpp>

export module lito.driver:package.resolver;

import rstd;
import lito.core;
import lito.tools;
import lito.system;
import :source.manager;
import :registry.graph;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;
using namespace lito::system;
using namespace lito::tools;
using namespace rstd::literals;
using StringSet = rstd::collections::BTreeMap<String, empty>;
using namespace lito;

using namespace lito::package;

template<typename T>
auto package_resolution_failure(String message) -> PackageResult<T> {
    return Err(PackageError::Message(rstd::move(message)));
}

template<typename T>
auto package_resolution_failure(ref<str> message) -> PackageResult<T> {
    return Err(PackageError::Message(String::make(message)));
}

auto clone_dependency_source(const lito::source::PackageSourceRequirement& source)
    -> lito::source::PackageSourceRequirement {
    if (source.is_Path()) {
        return lito::source::PackageSourceRequirement::Path(source.as_Path().path.clone());
    }
    if (source.is_Builtin()) {
        return lito::source::PackageSourceRequirement::Builtin(source.as_Builtin().id.clone());
    }
    if (source.is_Registry()) {
        return lito::source::PackageSourceRequirement::Registry(
            source.as_Registry().registry.clone(),
            source.as_Registry().package.clone(),
            source.as_Registry().requirement.clone());
    }
    return lito::source::PackageSourceRequirement::Git(
        source.as_Git().url.clone(),
        lito::source::GitReference {
            .kind  = source.as_Git().reference.kind,
            .value = source.as_Git().reference.value.clone(),
        });
}

struct PackageCoordinate {
    Option<String> version;
    String         source_identity;
    PathBuf        manifest;
};

using CoordinateMap = rstd::collections::BTreeMap<String, PackageCoordinate>;

struct SelectedSourcePackage {
    String                              source_identity;
    lito::source::ResolvedPackageSource source;
    PathBuf                             manifest;
    lito::manifest::PackageManifest     package;
};

struct AcquiredProjectSources {
    usize         primary;
    Option<usize> tests;
};

struct AcquiredDependencySource {
    Option<usize> source;
};

struct PreparedRegistrySource {
    usize                             source {};
    lito::registry::RegistryPackageId package;
    lito::registry::SemanticVersion   version;
};

using PreparedRegistryMap = rstd::collections::BTreeMap<String, PreparedRegistrySource>;

auto same_source_root(ref<rstd::path::Path> left, ref<rstd::path::Path> right) noexcept -> bool {
    return left.starts_with(right) && right.starts_with(left);
}

auto load_package_catalog(ref<rstd::path::Path> root)
    -> PackageResult<lito::workspace::WorkspaceCatalog> {
    auto document = lito::manifest::load_manifest_document(root);
    if (document.is_err()) {
        return Err(rstd::into<PackageError>(rstd::move(document).unwrap_err()));
    }
    auto loaded = rstd::move(document).unwrap();
    if (loaded.kind == lito::manifest::ManifestKind::Workspace && loaded.workspace.is_some()) {
        auto catalog =
            lito::workspace::load_workspace_catalog(rstd::move(loaded.workspace).unwrap());
        if (catalog.is_err()) {
            return Err(rstd::into<PackageError>(rstd::move(catalog).unwrap_err()));
        }
        return Ok(rstd::move(catalog).unwrap());
    }
    if (loaded.kind != lito::manifest::ManifestKind::Package || loaded.package.is_none()) {
        return package_resolution_failure<lito::workspace::WorkspaceCatalog>(
            "source manifest has no package or workspace"_str);
    }
    auto package    = rstd::move(loaded.package).unwrap();
    auto containing = lito::workspace::try_containing_workspace(package);
    if (containing.is_err()) {
        return Err(rstd::into<PackageError>(rstd::move(containing).unwrap_err()));
    }
    if (containing->is_some()) {
        auto catalog = lito::workspace::load_workspace_catalog(
            rstd::move(containing).unwrap().unwrap(), Some(rstd::move(package)));
        if (catalog.is_err()) {
            return Err(rstd::into<PackageError>(rstd::move(catalog).unwrap_err()));
        }
        return Ok(rstd::move(catalog).unwrap());
    }
    auto catalog = lito::workspace::WorkspaceCatalog::single(rstd::move(package));
    if (catalog.is_err()) {
        return Err(rstd::into<PackageError>(rstd::move(catalog).unwrap_err()));
    }
    return Ok(rstd::move(catalog).unwrap());
}

auto package_coordinate(const SelectedSourcePackage& selected) -> PackageResult<PackageCoordinate> {
    if (selected.package.version.source == lito::manifest::PackageVersionSource::Workspace &&
        selected.package.version.value.is_none()) {
        return package_resolution_failure<PackageCoordinate>(rstd::format(
            "package '{}' has an unresolved workspace version", selected.package.name.as_str()));
    }
    auto requires_version = selected.package.script.is_some();
    for (const auto& target : selected.package.targets) {
        const auto kind = lito::manifest::package_target_kind(target);
        if (kind == PackageTargetKind::Library || kind == PackageTargetKind::Plugin ||
            kind == PackageTargetKind::ProcMacro || kind == PackageTargetKind::Binary ||
            kind == PackageTargetKind::Benchmark) {
            requires_version = true;
            break;
        }
    }
    if (selected.package.version.value.is_none() && requires_version) {
        return package_resolution_failure<PackageCoordinate>(
            rstd::format("package '{}' has no version", selected.package.name.as_str()));
    }
    return Ok(PackageCoordinate {
        .version         = selected.package.version.value.clone(),
        .source_identity = selected.source_identity.clone(),
        .manifest        = selected.manifest.clone(),
    });
}

auto package_conflict(ref<str>                 name,
                      const PackageCoordinate& existing,
                      const PackageCoordinate& candidate) -> PackageError {
    auto existing_version = existing.version.is_some() ? existing.version->as_str() : "<none>"_str;
    auto candidate_version =
        candidate.version.is_some() ? candidate.version->as_str() : "<none>"_str;
    return PackageError::Message(
        rstd::format("package conflict for '{}': version '{}' at '{}' from source '{}' conflicts "
                     "with version '{}' at '{}' from source '{}'",
                     name,
                     existing_version,
                     existing.manifest.as_path(),
                     existing.source_identity.as_str(),
                     candidate_version,
                     candidate.manifest.as_path(),
                     candidate.source_identity.as_str()));
}

class PackageGraphResolver {
    PathBuf                                        root_directory_;
    lito::source::SourceManager                    sources_;
    Vec<Option<lito::workspace::WorkspaceCatalog>> catalogs_;
    Vec<ResolvedPackage>                           packages_;
    CoordinateMap                                  coordinates_ { CoordinateMap::make() };
    StringSet                                      active_ { StringSet::make() };
    Vec<String>                                    active_path_;
    Vec<PackageDependencyKind>                     active_kinds_;
    lito::registry::RegistryGraphProvider          registry_;
    Vec<lito::registry::RegistryGraphRequirement>  registry_requirements_;
    PreparedRegistryMap registry_sources_ { PreparedRegistryMap::make() };
    StringSet           discovered_ { StringSet::make() };
    usize               jobs_ { usize(1) };

    auto effective_source(const lito::source::PackageSourceRequirement& source,
                          ref<str>                                      expected_name)
        -> PackageResult<lito::source::PackageSourceRequirement> {
        if (! source.is_Builtin()) return Ok(clone_dependency_source(source));
        auto id       = source.as_Builtin().id.as_str();
        auto override = sources_.builtin_package(id);
        if (override.is_some() && (*override)->is_Registry()) {
            auto package = lito::registry::RegistryPackageName::parse(expected_name);
            if (package.is_err()) {
                return package_resolution_failure<lito::source::PackageSourceRequirement>(
                    rstd::format("builtin package '{}' has invalid Registry package name '{}': {}",
                                 id,
                                 expected_name,
                                 rstd::move(package).unwrap_err()));
            }
            return Ok(lito::source::PackageSourceRequirement::Registry(
                (*override)->as_Registry().registry.clone(),
                rstd::move(package).unwrap(),
                (*override)->as_Registry().requirement.clone()));
        }
        if (override.is_some() && (*override)->is_Git()) {
            return Ok(lito::source::PackageSourceRequirement::Git(
                (*override)->as_Git().url.clone(), (*override)->as_Git().reference.clone()));
        }
        if (override.is_some()) {
            return Ok(
                lito::source::PackageSourceRequirement::Path((*override)->as_Path().path.clone()));
        }
        if (registry_.resolve_builtin == nullptr) {
            return package_resolution_failure<lito::source::PackageSourceRequirement>(
                rstd::format("builtin package '{}' has no configured provider", id));
        }
        auto definition = registry_.resolve_builtin(registry_.context, id);
        if (definition.is_err()) {
            return package_resolution_failure<lito::source::PackageSourceRequirement>(
                rstd::move(definition).unwrap_err().message);
        }
        if (definition->package.name.as_str() != expected_name) {
            return package_resolution_failure<lito::source::PackageSourceRequirement>(
                rstd::format("dependency '{}' resolves builtin '{}' as package '{}'",
                             expected_name,
                             id,
                             definition->package.name.as_str()));
        }
        auto requirement = lito::registry::VersionRequirement::parse(
            rstd::format("={}", definition->version.text()).as_str());
        if (requirement.is_err()) {
            return package_resolution_failure<lito::source::PackageSourceRequirement>(
                rstd::format("builtin package '{}' has an invalid embedded version", id));
        }
        return Ok(lito::source::PackageSourceRequirement::Registry(
            Some(String::make(definition->package.registry.as_str())),
            definition->package.name.clone(),
            rstd::move(requirement).unwrap()));
    }

    auto ensure_catalog_slot(usize source) -> void {
        while (catalogs_.len() <= source) catalogs_.push(None());
    }

    auto store_catalog(usize source, lito::workspace::WorkspaceCatalog catalog) -> void {
        ensure_catalog_slot(source);
        if (catalogs_[source].is_none()) catalogs_[source] = Some(rstd::move(catalog));
    }

    auto catalog(usize source) noexcept -> lito::workspace::WorkspaceCatalog& {
        return *catalogs_[source];
    }

    auto catalog(usize source) const noexcept -> const lito::workspace::WorkspaceCatalog& {
        return *catalogs_[source];
    }

    auto acquire_catalog_root(ref<rstd::path::Path>                     root,
                              Option<lito::workspace::WorkspaceCatalog> preloaded = None())
        -> PackageResult<usize> {
        auto loaded =
            preloaded.is_some() ? Ok(rstd::move(preloaded).unwrap()) : load_package_catalog(root);
        if (loaded.is_err()) return Err(rstd::move(loaded).unwrap_err());
        auto catalog  = rstd::move(loaded).unwrap();
        auto acquired = sources_.acquire_root(catalog.root());
        if (acquired.is_err()) {
            return Err(rstd::into<PackageError>(rstd::move(acquired).unwrap_err()));
        }
        auto source = *acquired;
        store_catalog(source, rstd::move(catalog));
        return Ok(source);
    }

    auto ensure_source_catalog(usize source) -> PackageResult<empty> {
        ensure_catalog_slot(source);
        if (catalogs_[source].is_some()) return Ok(empty {});
        auto root    = sources_.source_root(source);
        auto catalog = load_package_catalog(root.as_path());
        if (catalog.is_err()) return Err(rstd::move(catalog).unwrap_err());
        auto resolved = sources_.resolved_source(source);
        if (resolved.kind == lito::source::PackageSourceKind::Git &&
            ! same_source_root(catalog->root(), root.as_path())) {
            return package_resolution_failure<empty>(
                rstd::format("Git source manifest root '{}' does not match checkout root '{}'",
                             catalog->root(),
                             root.as_path()));
        }
        store_catalog(source, rstd::move(catalog).unwrap());
        return Ok(empty {});
    }

    auto acquire_associated_catalog(usize primary_source, ref<str> directory, ProjectRootRole role)
        -> PackageResult<Option<usize>> {
        auto& primary    = catalog(primary_source);
        auto  associated = lito::workspace::try_load_associated_catalog(primary, directory, role);
        if (associated.is_err()) {
            return Err(rstd::into<PackageError>(rstd::move(associated).unwrap_err()));
        }
        if (associated->is_none()) return Ok(None());
        auto catalog = rstd::move(associated).unwrap().unwrap();
        auto root    = PathBuf::from(catalog.root());
        auto source  = acquire_catalog_root(root.as_path(), Some(rstd::move(catalog)));
        if (source.is_err()) return Err(rstd::move(source).unwrap_err());
        return Ok(Some(*source));
    }

    auto acquire_frontier(Vec<lito::source::PackageSourceFetchRequest> requests)
        -> PackageResult<Vec<AcquiredDependencySource>> {
        auto prepared = Vec<lito::source::PackageSourceFetchRequest>::with_capacity(requests.len());
        auto positions = Vec<usize>::make();
        auto catalogs =
            Vec<Option<lito::workspace::WorkspaceCatalog>>::with_capacity(requests.len());
        auto result = Vec<Option<AcquiredDependencySource>>::with_capacity(requests.len());
        for (usize index {}; index < requests.len(); ++index) result.push(None());
        for (usize index {}; index < requests.len(); ++index) {
            auto& request = requests[index];
            if (request.source.is_Builtin()) {
                return package_resolution_failure<Vec<AcquiredDependencySource>>(
                    "builtin dependency was not projected to an effective source"_str);
            }
            if (request.source.is_Registry()) {
                auto prepared_registry = registry_sources_.get(request.name.as_str());
                if (prepared_registry.is_none()) {
                    return package_resolution_failure<Vec<AcquiredDependencySource>>(rstd::format(
                        "Registry dependency '{}' was not selected by the Registry graph resolver",
                        request.name.as_str()));
                }
                const auto& selected = **prepared_registry;
                const auto& declared = request.source.as_Registry();
                if (selected.package.name != declared.package ||
                    ! declared.requirement.matches(selected.version)) {
                    return package_resolution_failure<Vec<AcquiredDependencySource>>(rstd::format(
                        "Registry dependency '{}' does not accept selected version '{}'",
                        request.name.as_str(),
                        selected.version.text()));
                }
                if (declared.registry.is_some()) {
                    auto identity = lito::registry::RegistryId::parse(declared.registry->as_str());
                    if (identity.is_ok() && ! (*identity == selected.package.registry)) {
                        return package_resolution_failure<Vec<AcquiredDependencySource>>(
                            rstd::format(
                                "Registry dependency '{}' selected Registry '{}' instead of '{}'",
                                request.name.as_str(),
                                selected.package.registry.as_str(),
                                declared.registry->as_str()));
                    }
                }
                result[index] = Some(AcquiredDependencySource {
                    .source = Some(usize(selected.source)),
                });
                continue;
            }
            positions.push(usize(index));
            if (request.source.is_Git()) {
                prepared.push(rstd::move(request));
                catalogs.push(None());
                continue;
            }
            auto requested = request.declaring_root.join(request.source.as_Path().path.as_path());
            auto catalog   = load_package_catalog(requested.as_path());
            if (catalog.is_err()) return Err(rstd::move(catalog).unwrap_err());
            auto root = PathBuf::from(catalog->root());
            prepared.push(lito::source::PackageSourceFetchRequest {
                .owner  = rstd::move(request.owner),
                .name   = rstd::move(request.name),
                .source = lito::source::PackageSourceRequirement::Path(PathBuf::from("."_str)),
                .declaring_root = rstd::move(root),
            });
            catalogs.push(Some(rstd::move(catalog).unwrap()));
        }
        auto acquired = sources_.acquire_frontier(rstd::move(prepared), jobs_);
        if (acquired.is_err()) {
            return Err(rstd::into<PackageError>(rstd::move(acquired).unwrap_err()));
        }
        auto acquired_sources = rstd::move(acquired).unwrap();
        for (usize index {}; index < acquired_sources.len(); ++index) {
            if (catalogs[index].is_some()) {
                store_catalog(acquired_sources[index], rstd::move(catalogs[index]).unwrap());
            } else {
                auto ensured = ensure_source_catalog(acquired_sources[index]);
                if (ensured.is_err()) return Err(rstd::move(ensured).unwrap_err());
            }
            result[positions[index]] = Some(AcquiredDependencySource {
                .source = Some(acquired_sources[index]),
            });
        }
        auto completed = Vec<AcquiredDependencySource>::with_capacity(result.len());
        for (auto& item : result) {
            if (item.is_none()) {
                return package_resolution_failure<Vec<AcquiredDependencySource>>(
                    "dependency source acquisition result is missing"_str);
            }
            completed.push(rstd::move(item).unwrap());
        }
        return Ok(rstd::move(completed));
    }

    auto discover_dependencies(ref<str> key, const lito::manifest::PackageManifest& package)
        -> PackageResult<empty> {
        if (discovered_.contains_key(key)) return Ok(empty {});
        discovered_.insert(String::make(key), empty {});
        auto       requests = Vec<lito::source::PackageSourceFetchRequest>::make();
        auto       names    = Vec<String>::make();
        const auto append   = [&](const auto& dependency) -> PackageResult<empty> {
            auto effective =
                rstd_try(effective_source(dependency.source, dependency.name.as_str()));
            if (effective.is_Registry()) {
                const auto& source = effective.as_Registry();
                registry_requirements_.push(lito::registry::RegistryGraphRequirement {
                    .registry    = source.registry.is_some() ? Some(source.registry->clone())
                                                             : Option<String> {},
                    .package     = source.package.clone(),
                    .requirement = source.requirement.clone(),
                    .source      = rstd::format("package '{}' dependency '{}'",
                                                package.name.as_str(),
                                                dependency.name.as_str()),
                });
                return Ok(empty {});
            }
            auto declaring_root = package.root.clone();
            if (dependency.declaration_root.is_some()) {
                declaring_root = dependency.declaration_root->clone();
            }
            names.push(dependency.name.clone());
            requests.push(lito::source::PackageSourceFetchRequest {
                .owner          = package.name.clone(),
                .name           = dependency.name.clone(),
                .source         = rstd::move(effective),
                .declaring_root = rstd::move(declaring_root),
            });
            return Ok(empty {});
        };
        for (const auto& dependency : package.dependencies) rstd_try(append(dependency));
        for (const auto& dependency : package.dev_dependencies) rstd_try(append(dependency));
        for (const auto& dependency : package.runtime_dependencies) rstd_try(append(dependency));
        auto pmacro_support = rstd_try(pmacro_support_dependency(package));
        if (pmacro_support.is_some()) rstd_try(append(*pmacro_support));
        auto acquired = rstd_try(acquire_frontier(rstd::move(requests)));
        for (usize index {}; index < acquired.len(); ++index) {
            auto source    = *acquired[index].source;
            auto child_key = rstd::format("{}\n{}", sources_.source_identity(source), names[index]);
            auto child     = catalog(source).package(names[index].as_str());
            if (child.is_none()) {
                return package_resolution_failure<empty>(
                    rstd::format("source '{}' has no package named '{}'",
                                 sources_.source_identity(source),
                                 names[index].as_str()));
            }
            rstd_try(discover_dependencies(child_key.as_str(), **child));
        }
        return Ok(empty {});
    }

    auto discover_source(usize source) -> PackageResult<empty> {
        auto names = package_names(source);
        for (const auto& name : names) {
            auto key     = rstd::format("{}\n{}", sources_.source_identity(source), name.as_str());
            auto package = catalog(source).package(name.as_str());
            if (package.is_none()) {
                return package_resolution_failure<empty>(
                    rstd::format("source '{}' has no package named '{}'",
                                 sources_.source_identity(source),
                                 name.as_str()));
            }
            rstd_try(discover_dependencies(key.as_str(), **package));
        }
        return Ok(empty {});
    }

    auto prepare_registry_impl(const AcquiredProjectSources& roots) -> PackageResult<empty> {
        rstd_try(discover_source(roots.primary));
        if (roots.tests.is_some()) rstd_try(discover_source(*roots.tests));
        if (registry_requirements_.is_empty()) return Ok(empty {});
        if (registry_.resolve == nullptr) {
            return package_resolution_failure<empty>(
                "Registry dependencies require configured Registry resolution"_str);
        }
        auto resolved = registry_.resolve(registry_.context, registry_requirements_.as_slice());
        if (resolved.is_err()) {
            return package_resolution_failure<empty>(rstd::move(resolved).unwrap_err().message);
        }
        for (auto& selected : rstd::move(resolved).unwrap()) {
            auto name = String::make(selected.package.name.as_str());
            if (registry_sources_.contains_key(name.as_str())) {
                return package_resolution_failure<empty>(rstd::format(
                    "Registry graph resolver returned package '{}' more than once", name.as_str()));
            }
            if (! same_source_root(selected.catalog.root(),
                                   selected.source.root_directory.as_path())) {
                return package_resolution_failure<empty>(rstd::format(
                    "Registry package '{}' catalog root does not match its materialized source",
                    name.as_str()));
            }
            auto version = selected.version.clone();
            auto package = selected.package.clone();
            auto source  = sources_.acquire_resolved(rstd::move(selected.source));
            if (source.is_err()) {
                return Err(rstd::into<PackageError>(rstd::move(source).unwrap_err()));
            }
            store_catalog(*source, rstd::move(selected.catalog));
            registry_sources_.insert(rstd::move(name),
                                     PreparedRegistrySource {
                                         .source  = *source,
                                         .package = rstd::move(package),
                                         .version = rstd::move(version),
                                     });
        }
        for (const auto& requirement : registry_requirements_) {
            if (! registry_sources_.contains_key(requirement.package.as_str())) {
                return package_resolution_failure<empty>(
                    rstd::format("Registry graph resolver omitted required package '{}'",
                                 requirement.package.as_str()));
            }
        }
        return Ok(empty {});
    }

    auto take_package(usize source, ref<str> name) -> PackageResult<SelectedSourcePackage> {
        auto manifest = catalog(source).take_package(name);
        if (manifest.is_none()) {
            if (catalog(source).names().len() == usize(1)) {
                return package_resolution_failure<SelectedSourcePackage>(
                    rstd::format("dependency '{}' resolves to package '{}' from source '{}'",
                                 name,
                                 catalog(source).names()[usize {}].as_str(),
                                 sources_.source_identity(source)));
            }
            return package_resolution_failure<SelectedSourcePackage>(rstd::format(
                "source '{}' has no package named '{}'", sources_.source_identity(source), name));
        }
        auto package     = rstd::move(manifest).unwrap();
        auto source_root = sources_.source_root(source);
        auto relative    = package.manifest_path.as_path().strip_prefix(source_root.as_path());
        if (relative.is_none()) {
            return package_resolution_failure<SelectedSourcePackage>(
                rstd::format("package manifest '{}' is outside source '{}'",
                             package.manifest_path.as_path(),
                             sources_.source_identity(source)));
        }
        return Ok(SelectedSourcePackage {
            .source_identity = String::make(sources_.source_identity(source)),
            .source          = sources_.resolved_source(source),
            .manifest        = PathBuf::from(*relative),
            .package         = rstd::move(package),
        });
    }

    auto resolved_package(ref<str> name) const noexcept -> const ResolvedPackage* {
        for (const auto& package : packages_) {
            if (package.manifest.name == name) return rstd::addressof(package);
        }
        return nullptr;
    }

    auto package_has_proc_macro(const lito::manifest::PackageManifest& package) const noexcept
        -> bool {
        for (const auto& target : package.targets) {
            if (lito::manifest::package_target_kind(target) == PackageTargetKind::ProcMacro)
                return true;
        }
        return false;
    }

    auto package_has_plugin(const lito::manifest::PackageManifest& package) const noexcept -> bool {
        for (const auto& target : package.targets) {
            if (lito::manifest::package_target_kind(target) == PackageTargetKind::Plugin)
                return true;
        }
        return false;
    }

    auto validate_pmacro_support_contract(const lito::manifest::DeclaredDependency& declaration,
                                          const lito::manifest::PackageManifest&    provider) const
        -> PackageResult<empty> {
        if (! declaration.source.is_Builtin() ||
            declaration.source.as_Builtin().id != "pmacro"_str) {
            return Ok(empty {});
        }
        if (package_has_plugin(provider)) return Ok(empty {});
        return package_resolution_failure<empty>(
            rstd::format("builtin package 'pmacro' at '{}' must provide [plugin]",
                         provider.manifest_path.as_path()));
    }

    auto pmacro_support_dependency(const lito::manifest::PackageManifest& package)
        -> PackageResult<Option<lito::manifest::DeclaredDependency>> {
        if (! package_has_proc_macro(package)) {
            return Ok(None<lito::manifest::DeclaredDependency>());
        }
        for (const auto& dependency : package.dependencies) {
            if (dependency.name == "pmacro"_str) {
                return package_resolution_failure<
                    Option<lito::manifest::DeclaredDependency>>(rstd::format(
                    "pmacro package '{}' must not declare the compiler-support package 'pmacro'; "
                    "configure builtin package 'pmacro' instead",
                    package.name.as_str()));
            }
        }
        for (const auto& dependency : package.dev_dependencies) {
            if (dependency.name == "pmacro"_str) {
                return package_resolution_failure<
                    Option<lito::manifest::DeclaredDependency>>(rstd::format(
                    "pmacro package '{}' must not declare the compiler-support package 'pmacro'; "
                    "configure builtin package 'pmacro' instead",
                    package.name.as_str()));
            }
        }
        return Ok(Some(lito::manifest::DeclaredDependency {
            .name   = String::make("pmacro"_str),
            .source = lito::source::PackageSourceRequirement::Builtin(String::make("pmacro"_str)),
            .visibility       = None(),
            .features         = None(),
            .default_features = None(),
            .declaration_root = None(),
        }));
    }

    auto resolve_acquired(AcquiredDependencySource& acquired,
                          ref<str>                  expected_name,
                          PackageDependencyKind     incoming) -> PackageResult<String> {
        return resolve(*acquired.source, expected_name, incoming);
    }

    auto classify_dependency(const lito::manifest::DeclaredDependency& declaration,
                             PackageDependencyKind                     kind)
        -> PackageResult<ResolvedRequiredDependency> {
        const auto* provider = resolved_package(declaration.name.as_str());
        if (provider == nullptr) {
            return package_resolution_failure<ResolvedRequiredDependency>(rstd::format(
                "resolved dependency '{}' is missing from the package graph", declaration.name));
        }
        rstd_try(validate_pmacro_support_contract(declaration, provider->manifest));
        if (provider->manifest.script.is_some()) {
            if (kind == PackageDependencyKind::Development) {
                return package_resolution_failure<ResolvedRequiredDependency>(rstd::format(
                    "development dependency '{}' resolves to a script package, but development "
                    "Lua hosts are not supported",
                    declaration.name.as_str()));
            }
            auto       fields = String::make();
            const auto append = [&](ref<str> name) -> void {
                if (! fields.is_empty()) fields.push_str(", "_str);
                fields.push_str(name);
            };
            if (declaration.visibility.is_some()) append("visibility"_str);
            if (declaration.features.is_some()) append("features"_str);
            if (declaration.default_features.is_some()) append("default-features"_str);
            if (! fields.is_empty()) {
                return package_resolution_failure<ResolvedRequiredDependency>(rstd::format(
                    "dependency '{}' resolves to script package '{}', so C++ fields {} are not "
                    "allowed",
                    declaration.name.as_str(),
                    provider->manifest.manifest_path.as_path(),
                    fields.as_str()));
            }
            auto require_name =
                lito::manifest::script_require_name(provider->manifest.name.as_str());
            if (require_name == "@lito"_str) {
                return package_resolution_failure<ResolvedRequiredDependency>(
                    rstd::format("script package '{}' uses reserved require name '@lito'",
                                 provider->manifest.name.as_str()));
            }
            return Ok(ResolvedRequiredDependency::Script(ResolvedScriptDependency {
                .name            = provider->manifest.name.clone(),
                .require_name    = rstd::move(require_name),
                .source_identity = provider->source_identity.clone(),
                .supports        = provider->manifest.script->supports.clone(),
            }));
        }
        if (package_has_plugin(provider->manifest)) {
            if (declaration.visibility.is_some()) {
                return package_resolution_failure<ResolvedRequiredDependency>(rstd::format(
                    "dependency '{}' resolves to plugin package '{}', so visibility is not "
                    "allowed",
                    declaration.name.as_str(),
                    provider->manifest.manifest_path.as_path()));
            }
            auto features = declaration.features.is_some() ? declaration.features->clone()
                                                           : Vec<String>::make();
            return Ok(ResolvedRequiredDependency::Plugin(ResolvedPluginDependency {
                .name     = provider->manifest.name.clone(),
                .features = rstd::move(features),
                .default_features =
                    declaration.default_features.is_some() ? *declaration.default_features : true,
            }));
        }
        if (package_has_proc_macro(provider->manifest)) {
            if (declaration.visibility.is_some()) {
                return package_resolution_failure<ResolvedRequiredDependency>(rstd::format(
                    "dependency '{}' resolves to pmacro package '{}', so visibility is not "
                    "allowed",
                    declaration.name.as_str(),
                    provider->manifest.manifest_path.as_path()));
            }
            auto features = declaration.features.is_some() ? declaration.features->clone()
                                                           : Vec<String>::make();
            return Ok(ResolvedRequiredDependency::Pmacro(ResolvedPmacroDependency {
                .name     = provider->manifest.name.clone(),
                .features = rstd::move(features),
                .default_features =
                    declaration.default_features.is_some() ? *declaration.default_features : true,
            }));
        }
        if (! lito::manifest::package_has_library_target(provider->manifest) &&
            ! lito::manifest::package_has_host_tool_target(provider->manifest)) {
            return package_resolution_failure<ResolvedRequiredDependency>(rstd::format(
                "dependency '{}' resolves to package '{}' which exposes neither a C/C++ library "
                "nor a host tool, script, plugin or pmacro contract",
                declaration.name.as_str(),
                provider->manifest.manifest_path.as_path()));
        }
        auto features =
            declaration.features.is_some() ? declaration.features->clone() : Vec<String>::make();
        return Ok(ResolvedRequiredDependency::Cpp(ResolvedCppDependency {
            .name       = provider->manifest.name.clone(),
            .visibility = declaration.visibility.is_some()
                              ? *declaration.visibility
                              : lito::dependency::DependencyVisibility::Private,
            .features   = rstd::move(features),
            .default_features =
                declaration.default_features.is_some() ? *declaration.default_features : true,
        }));
    }

public:
    explicit PackageGraphResolver(ref<rstd::path::Path>                 root_directory,
                                  lito::source::SourceResolutionOptions options,
                                  lito::tools::ToolResolver*            resolver,
                                  const ResolvedProcessEnvironment&     environment,
                                  usize                                 jobs,
                                  lito::source::SourceEventSink         observer,
                                  lito::registry::RegistryGraphProvider registry)
        : root_directory_(PathBuf::from(root_directory)),
          sources_(root_directory, rstd::move(options), resolver, environment, observer),
          registry_(registry),
          jobs_(jobs) {}

    auto prepare_registry(const AcquiredProjectSources& roots) -> PackageResult<empty> {
        return prepare_registry_impl(roots);
    }

    auto acquire_root(ref<rstd::path::Path> root, Option<lito::workspace::WorkspaceCatalog> catalog)
        -> PackageResult<AcquiredProjectSources> {
        auto primary = acquire_catalog_root(root, rstd::move(catalog));
        if (primary.is_err()) return Err(rstd::move(primary).unwrap_err());
        if (registry_.root_source != nullptr) {
            auto replaced = sources_.replace_resolved(*primary, registry_.root_source->clone());
            if (replaced.is_err()) {
                return Err(rstd::into<PackageError>(rstd::move(replaced).unwrap_err()));
            }
        }
        auto tests =
            acquire_associated_catalog(*primary, "tests"_str, ProjectRootRole::AssociatedTest);
        if (tests.is_err()) return Err(rstd::move(tests).unwrap_err());
        return Ok(AcquiredProjectSources {
            .primary = *primary,
            .tests   = rstd::move(tests).unwrap(),
        });
    }

    auto package_names(usize source) const -> Vec<String> {
        auto result = Vec<String>::with_capacity(catalog(source).names().len());
        for (const auto& name : catalog(source).names()) result.push(name.clone());
        return result;
    }

    auto default_package_names(usize source) const -> Vec<String> {
        auto result = Vec<String>::with_capacity(catalog(source).default_names().len());
        for (const auto& name : catalog(source).default_names()) result.push(name.clone());
        return result;
    }

    auto source_name(usize source) const noexcept -> ref<str> { return catalog(source).name(); }

    auto source_identity(usize source) const noexcept -> ref<str> {
        return sources_.source_identity(source);
    }

    auto source_manifest(usize source) const -> PathBuf {
        return PathBuf::from(catalog(source).manifest_path());
    }

    auto source_is_workspace(usize source) const noexcept -> bool {
        return catalog(source).is_workspace();
    }

    auto source_profile(usize source) const -> lito::manifest::ProjectProfile {
        return catalog(source).profile();
    }

    auto resolve(usize                 source,
                 ref<str>              expected_name,
                 PackageDependencyKind incoming = PackageDependencyKind::Normal)
        -> PackageResult<String> {
        auto source_identity = String::make(sources_.source_identity(source));
        auto existing        = coordinates_.get(expected_name);
        if (existing.is_some() && (**existing).source_identity == source_identity) {
            if (active_.contains_key(expected_name)) {
                auto cycle = PackageDependencyCycleError {};
                auto start = usize {};
                while (start < active_path_.len() && active_path_[start] != expected_name) ++start;
                for (auto index = start; index < active_path_.len(); ++index) {
                    cycle.packages.push(active_path_[index].clone());
                    if (index + usize(1) < active_path_.len()) {
                        cycle.edges.push(PackageDependencyCycleEdge {
                            .package    = active_path_[index].clone(),
                            .dependency = active_path_[index + usize(1)].clone(),
                            .kind       = active_kinds_[index + usize(1)],
                        });
                    }
                }
                cycle.packages.push(String::make(expected_name));
                cycle.edges.push(PackageDependencyCycleEdge {
                    .package    = active_path_[active_path_.len() - usize(1)].clone(),
                    .dependency = String::make(expected_name),
                    .kind       = incoming,
                });
                return Err(PackageError::Cycle(rstd::move(cycle)));
            }
            return Ok(String::make(expected_name));
        }

        auto selected = take_package(source, expected_name);
        if (selected.is_err()) {
            return Err(rstd::move(selected).unwrap_err());
        }
        auto loaded = rstd::move(selected).unwrap();
        if (loaded.package.name.as_str() != expected_name) {
            return package_resolution_failure<String>(
                rstd::format("dependency '{}' resolves to package '{}' from source '{}'",
                             expected_name,
                             loaded.package.name.as_str(),
                             source_identity.as_str()));
        }
        auto pmacro_support = rstd_try(pmacro_support_dependency(loaded.package));
        if (pmacro_support.is_some()) {
            loaded.package.dependencies.push(rstd::move(pmacro_support).unwrap());
        }
        auto coordinate = package_coordinate(loaded);
        if (coordinate.is_err()) return Err(rstd::move(coordinate).unwrap_err());
        auto candidate = rstd::move(coordinate).unwrap();
        if (existing.is_some()) {
            return Err(package_conflict(expected_name, **existing, candidate));
        }

        coordinates_.insert(loaded.package.name.clone(),
                            PackageCoordinate {
                                .version         = candidate.version.clone(),
                                .source_identity = candidate.source_identity.clone(),
                                .manifest        = candidate.manifest.clone(),
                            });
        active_.insert(loaded.package.name.clone(), empty {});
        active_path_.push(loaded.package.name.clone());
        active_kinds_.push(rstd::move(incoming));

        auto fetch_requests = Vec<lito::source::PackageSourceFetchRequest>::with_capacity(
            loaded.package.dependencies.len() + loaded.package.dev_dependencies.len() +
            loaded.package.runtime_dependencies.len());
        const auto append_fetch_requests =
            [&](const Vec<lito::manifest::DeclaredDependency>& declarations)
            -> PackageResult<empty> {
            for (const auto& dependency : declarations) {
                auto declaring_root = loaded.package.root.clone();
                if (dependency.declaration_root.is_some()) {
                    declaring_root = dependency.declaration_root->clone();
                }
                fetch_requests.push(lito::source::PackageSourceFetchRequest {
                    .owner = loaded.package.name.clone(),
                    .name  = dependency.name.clone(),
                    .source =
                        rstd_try(effective_source(dependency.source, dependency.name.as_str())),
                    .declaring_root = rstd::move(declaring_root),
                });
            }
            return Ok(empty {});
        };
        rstd_try(append_fetch_requests(loaded.package.dependencies));
        rstd_try(append_fetch_requests(loaded.package.dev_dependencies));
        for (const auto& dependency : loaded.package.runtime_dependencies) {
            auto declaring_root = loaded.package.root.clone();
            if (dependency.declaration_root.is_some()) {
                declaring_root = dependency.declaration_root->clone();
            }
            fetch_requests.push(lito::source::PackageSourceFetchRequest {
                .owner  = loaded.package.name.clone(),
                .name   = dependency.name.clone(),
                .source = rstd_try(effective_source(dependency.source, dependency.name.as_str())),
                .declaring_root = rstd::move(declaring_root),
            });
        }
        auto fetched_sources = rstd_try(acquire_frontier(rstd::move(fetch_requests)));
        auto source_offset   = usize {};
        auto dependencies =
            Vec<ResolvedRequiredDependency>::with_capacity(loaded.package.dependencies.len());
        for (const auto& dependency : loaded.package.dependencies) {
            rstd_try(resolve_acquired(fetched_sources[source_offset++],
                                      dependency.name.as_str(),
                                      PackageDependencyKind::Normal));
            dependencies.push(
                rstd_try(classify_dependency(dependency, PackageDependencyKind::Normal)));
        }
        rstd::slice_::sort_unstable_by(
            dependencies.as_mut_slice().as_mut_ref(),
            [](const ResolvedRequiredDependency& left, const ResolvedRequiredDependency& right) {
                return resolved_dependency_name_value(left) < resolved_dependency_name_value(right);
            });
        if (package_has_plugin(loaded.package)) {
            for (const auto& dependency : dependencies) {
                if (! dependency.is_Plugin()) continue;
                return package_resolution_failure<String>(
                    rstd::format("plugin package '{}' cannot depend on plugin package '{}'",
                                 loaded.package.name.as_str(),
                                 dependency.as_Plugin().value.name.as_str()));
            }
        }
        for (usize index {}; index < dependencies.len(); ++index) {
            if (! dependencies[index].is_Script()) continue;
            for (usize other = index + usize(1); other < dependencies.len(); ++other) {
                if (! dependencies[other].is_Script()) continue;
                const auto& left  = dependencies[index].as_Script().value;
                const auto& right = dependencies[other].as_Script().value;
                if (left.require_name != right.require_name.as_str()) continue;
                return package_resolution_failure<String>(rstd::format(
                    "dependencies '{}' and '{}' of package '{}' normalize to the same Lua require "
                    "name '{}'",
                    left.name.as_str(),
                    right.name.as_str(),
                    loaded.package.name.as_str(),
                    left.require_name.as_str()));
            }
        }

        auto dev_dependencies =
            Vec<ResolvedRequiredDependency>::with_capacity(loaded.package.dev_dependencies.len());
        for (const auto& dependency : loaded.package.dev_dependencies) {
            rstd_try(resolve_acquired(fetched_sources[source_offset++],
                                      dependency.name.as_str(),
                                      PackageDependencyKind::Development));
            dev_dependencies.push(
                rstd_try(classify_dependency(dependency, PackageDependencyKind::Development)));
        }
        rstd::slice_::sort_unstable_by(
            dev_dependencies.as_mut_slice().as_mut_ref(),
            [](const ResolvedRequiredDependency& left, const ResolvedRequiredDependency& right) {
                return resolved_dependency_name_value(left) < resolved_dependency_name_value(right);
            });
        auto runtime_dependencies = Vec<ResolvedRuntimeDependency>::with_capacity(
            loaded.package.runtime_dependencies.len());
        for (const auto& dependency : loaded.package.runtime_dependencies) {
            auto dependency_name = resolve_acquired(fetched_sources[source_offset++],
                                                    dependency.name.as_str(),
                                                    PackageDependencyKind::Runtime);
            if (dependency_name.is_err()) {
                return Err(rstd::move(dependency_name).unwrap_err());
            }
            runtime_dependencies.push(ResolvedRuntimeDependency {
                .name = rstd::move(dependency_name).unwrap(),
            });
        }
        rstd::slice_::sort_unstable_by(
            runtime_dependencies.as_mut_slice().as_mut_ref(),
            [](const ResolvedRuntimeDependency& left, const ResolvedRuntimeDependency& right) {
                return left.name < right.name;
            });

        active_.remove(loaded.package.name.as_str());
        active_path_.pop();
        active_kinds_.pop();
        packages_.push(ResolvedPackage {
            .source_identity      = rstd::move(loaded.source_identity),
            .source               = rstd::move(loaded.source),
            .source_manifest      = rstd::move(loaded.manifest),
            .manifest             = rstd::move(loaded.package),
            .embedded_source      = None(),
            .dependencies         = rstd::move(dependencies),
            .dev_dependencies     = rstd::move(dev_dependencies),
            .runtime_dependencies = rstd::move(runtime_dependencies),
            .features             = {},
        });
        return Ok(String::make(expected_name));
    }

    auto finish(String                         name,
                Vec<ResolvedProjectRoot>       roots,
                Vec<String>                    default_roots,
                PathBuf                        manifest_path,
                bool                           root_is_workspace,
                lito::manifest::ProjectProfile profile) -> ResolvedPackageGraph {
        rstd::slice_::sort_unstable_by(
            packages_.as_mut_slice().as_mut_ref(),
            [](const ResolvedPackage& left, const ResolvedPackage& right) {
                return left.manifest.name < right.manifest.name;
            });
        rstd::slice_::sort_unstable_by(
            roots.as_mut_slice().as_mut_ref(),
            [](const ResolvedProjectRoot& left, const ResolvedProjectRoot& right) {
                return left.name < right.name;
            });
        return ResolvedPackageGraph {
            .name              = rstd::move(name),
            .roots             = rstd::move(roots),
            .default_roots     = rstd::move(default_roots),
            .root_directory    = rstd::move(root_directory_),
            .manifest_path     = rstd::move(manifest_path),
            .root_is_workspace = root_is_workspace,
            .profile           = rstd::move(profile),
            .sources           = sources_.finish(),
            .packages          = rstd::move(packages_),
        };
    }
};

export namespace lito::package
{

auto resolve_package_graph_with_environment_impl(
    ref<rstd::path::Path>                     requested_root,
    lito::source::SourceResolutionOptions     options,
    lito::tools::ToolResolver*                tool_resolver,
    const ResolvedProcessEnvironment&         environment,
    usize                                     jobs     = usize(1),
    lito::source::SourceEventSink             observer = {},
    Option<lito::workspace::WorkspaceCatalog> catalog  = None(),
    lito::registry::RegistryGraphProvider registry = {}) -> PackageResult<ResolvedPackageGraph> {
    if (jobs == usize {}) {
        return package_resolution_failure<ResolvedPackageGraph>(
            String::make("source fetch jobs must be greater than zero"_str));
    }
    auto canonical = rstd::fs::canonicalize(requested_root);
    if (canonical.is_err()) {
        return Err(
            PackageError::System(SystemError::Io(String::make("resolve package graph root"_str),
                                                 PathBuf::from(requested_root),
                                                 rstd::move(canonical).unwrap_err())));
    }
    auto root     = rstd::move(canonical).unwrap();
    auto resolver = PackageGraphResolver(
        root.as_path(), rstd::move(options), tool_resolver, environment, jobs, observer, registry);
    auto source = resolver.acquire_root(root.as_path(), rstd::move(catalog));
    if (source.is_err()) return Err(rstd::move(source).unwrap_err());
    auto project_sources   = rstd::move(source).unwrap();
    auto prepared_registry = resolver.prepare_registry(project_sources);
    if (prepared_registry.is_err()) return Err(rstd::move(prepared_registry).unwrap_err());
    auto root_source       = project_sources.primary;
    auto project_name      = String::make(resolver.source_name(root_source));
    auto manifest_path     = resolver.source_manifest(root_source);
    auto root_is_workspace = resolver.source_is_workspace(root_source);
    auto default_roots     = resolver.default_package_names(root_source);
    auto profile           = resolver.source_profile(root_source);
    auto roots             = Vec<ResolvedProjectRoot>::make();
    auto resolve_roots     = [&](usize source_index, ProjectRootRole role) -> PackageResult<empty> {
        auto names = resolver.package_names(source_index);
        for (const auto& name : names) {
            auto resolved_name = resolver.resolve(source_index, name.as_str());
            if (resolved_name.is_err()) return Err(rstd::move(resolved_name).unwrap_err());
            roots.push(ResolvedProjectRoot {
                .name            = rstd::move(resolved_name).unwrap(),
                .source_identity = String::make(resolver.source_identity(source_index)),
                .role            = role,
            });
        }
        return Ok(empty {});
    };
    auto primary_role =
        root_is_workspace ? ProjectRootRole::WorkspaceMember : ProjectRootRole::PrimaryPackage;
    auto resolved_primary = resolve_roots(root_source, primary_role);
    if (resolved_primary.is_err()) return Err(rstd::move(resolved_primary).unwrap_err());
    if (project_sources.tests.is_some()) {
        auto resolved_tests =
            resolve_roots(*project_sources.tests, ProjectRootRole::AssociatedTest);
        if (resolved_tests.is_err()) return Err(rstd::move(resolved_tests).unwrap_err());
    }
    return Ok(resolver.finish(rstd::move(project_name),
                              rstd::move(roots),
                              rstd::move(default_roots),
                              rstd::move(manifest_path),
                              root_is_workspace,
                              rstd::move(profile)));
}

auto resolve_package_graph_with_environment(
    ref<rstd::path::Path>                     requested_root,
    lito::source::SourceResolutionOptions     options,
    lito::tools::ToolResolver&                tool_resolver,
    const ResolvedProcessEnvironment&         environment,
    usize                                     jobs     = usize(1),
    lito::source::SourceEventSink             observer = {},
    Option<lito::workspace::WorkspaceCatalog> catalog  = None(),
    lito::registry::RegistryGraphProvider registry = {}) -> PackageResult<ResolvedPackageGraph> {
    return resolve_package_graph_with_environment_impl(requested_root,
                                                       rstd::move(options),
                                                       rstd::addressof(tool_resolver),
                                                       environment,
                                                       jobs,
                                                       observer,
                                                       rstd::move(catalog),
                                                       registry);
}

auto resolve_package_graph(ref<rstd::path::Path>                 requested_root,
                           lito::source::SourceResolutionOptions options = {})
    -> PackageResult<ResolvedPackageGraph> {
    auto environment = ResolvedProcessEnvironment::resolve(ProcessEnvironmentSpec {});
    if (environment.is_err()) {
        return Err(rstd::into<PackageError>(rstd::move(environment).unwrap_err()));
    }
    auto resolver = lito::tools::ToolResolver(*environment);
    return resolve_package_graph_with_environment(
        requested_root, rstd::move(options), resolver, *environment);
}

} // namespace lito::package
