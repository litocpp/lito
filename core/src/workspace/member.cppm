module;
#include <rstd/macro.hpp>

export module lito.core:workspace.member;

import rstd;
import :workspace.error;
import :source.git;
import :source.requirement;
import :dependency.cmake;
import :dependency.pkg_config;
import :manifest;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;
using namespace rstd::literals;
using namespace lito;
using namespace lito::workspace;

template<typename T>
auto workspace_failure(String message) -> WorkspaceResult<T> {
    return Err(WorkspaceError::Message(rstd::move(message)));
}

auto same_path(ref<rstd::path::Path> left, ref<rstd::path::Path> right) noexcept -> bool {
    return left.starts_with(right) && right.starts_with(left);
}

export namespace lito::workspace
{

auto workspace_member_directory(const lito::manifest::WorkspaceManifest& workspace,
                                ref<rstd::path::Path>                    declared,
                                ref<str> context) -> WorkspaceResult<PathBuf> {
    auto requested = workspace.root.join(declared);
    auto canonical = rstd::fs::canonicalize(requested.as_path());
    if (canonical.is_err()) {
        return Err(WorkspaceError::Io(rstd::format("resolve {} directory", context),
                                      rstd::move(requested),
                                      rstd::move(canonical).unwrap_err()));
    }
    auto directory = rstd::move(canonical).unwrap();
    if (directory.as_path().strip_prefix(workspace.root.as_path()).is_none()) {
        return workspace_failure<PathBuf>(
            rstd::format("{} directory '{}' is outside workspace root", context, declared));
    }
    return Ok(rstd::move(directory));
}

auto resolve_workspace_member_version(lito::manifest::PackageManifest&         manifest,
                                      const lito::manifest::WorkspaceManifest& workspace)
    -> WorkspaceResult<empty> {
    if (manifest.version.source != lito::manifest::PackageVersionSource::Workspace) {
        return Ok(empty {});
    }
    if (workspace.package.version.is_none()) {
        return workspace_failure<empty>(
            rstd::format("workspace member '{}' inherits package.version but "
                         "workspace.package.version is not set",
                         manifest.name.as_str()));
    }
    manifest.version.value = Some(workspace.package.version->clone());
    return Ok(empty {});
}

auto resolve_workspace_member_license(lito::manifest::PackageManifest&         manifest,
                                      const lito::manifest::WorkspaceManifest& workspace)
    -> WorkspaceResult<empty> {
    if (manifest.license.source != lito::manifest::PackageLicenseSource::Workspace) {
        return Ok(empty {});
    }
    if (workspace.package.license.is_none()) {
        return workspace_failure<empty>(
            rstd::format("workspace member '{}' inherits package.license but "
                         "workspace.package.license is not set",
                         manifest.name.as_str()));
    }
    manifest.license.value = Some(workspace.package.license->clone());
    return Ok(empty {});
}

auto clone_package_source(const lito::source::PackageSourceRequirement& source)
    -> lito::source::PackageSourceRequirement {
    if (source.is_Path()) {
        return lito::source::PackageSourceRequirement::Path(source.as_Path().path.clone());
    }
    return lito::source::PackageSourceRequirement::Git(
        source.as_Git().url.clone(),
        lito::source::GitReference {
            .kind  = source.as_Git().reference.kind,
            .value = source.as_Git().reference.value.clone(),
        });
}

auto clone_pkg_config_requirement(
    const lito::dependency::PkgConfigDependencyRequirement& requirement)
    -> lito::dependency::PkgConfigDependencyRequirement {
    auto version = Option<lito::dependency::PkgConfigVersionRequirement> {};
    if (requirement.version.is_some()) {
        version = Some(lito::dependency::PkgConfigVersionRequirement {
            .comparison = requirement.version->comparison,
            .value      = requirement.version->value.clone(),
        });
    }
    return lito::dependency::PkgConfigDependencyRequirement {
        .module  = requirement.module.clone(),
        .version = rstd::move(version),
        .mode    = requirement.mode,
    };
}

auto resolve_workspace_member_dependencies(lito::manifest::PackageManifest&         manifest,
                                           const lito::manifest::WorkspaceManifest& workspace)
    -> WorkspaceResult<empty> {
    const auto resolve_package_dependencies =
        [&](const Vec<lito::manifest::WorkspaceDependencyReference>& references,
            Vec<lito::manifest::DeclaredDependency>&                 dependencies,
            ref<str> kind) -> WorkspaceResult<empty> {
        for (const auto& reference : references) {
            const lito::manifest::WorkspaceDependencyDefinition* definition = nullptr;
            for (const auto& candidate : workspace.dependencies) {
                if (candidate.name == reference.name) {
                    definition = rstd::addressof(candidate);
                    break;
                }
            }
            if (definition == nullptr) {
                return workspace_failure<empty>(
                    rstd::format("workspace member '{}' inherits {} dependency '{}' but "
                                 "workspace.dependencies has no matching definition",
                                 manifest.name.as_str(),
                                 kind,
                                 reference.name.as_str()));
            }
            dependencies.push(lito::manifest::DeclaredDependency {
                .name             = reference.name.clone(),
                .source           = clone_package_source(definition->source),
                .visibility       = reference.visibility,
                .features         = reference.features.clone(),
                .default_features = reference.default_features,
                .declaration_root = Some(workspace.root.clone()),
            });
        }
        return Ok(empty {});
    };
    rstd_try(resolve_package_dependencies(
        manifest.workspace_dependencies, manifest.dependencies, "normal"_str));
    rstd_try(resolve_package_dependencies(
        manifest.workspace_dev_dependencies, manifest.dev_dependencies, "development"_str));
    manifest.workspace_dependencies.clear();
    manifest.workspace_dev_dependencies.clear();

    for (const auto& reference : manifest.workspace_runtime_dependencies) {
        const lito::manifest::WorkspaceDependencyDefinition* definition = nullptr;
        for (const auto& candidate : workspace.dependencies) {
            if (candidate.name == reference.name) {
                definition = rstd::addressof(candidate);
                break;
            }
        }
        if (definition == nullptr) {
            return workspace_failure<empty>(
                rstd::format("workspace member '{}' inherits runtime dependency '{}' but "
                             "workspace.dependencies has no matching definition",
                             manifest.name.as_str(),
                             reference.name.as_str()));
        }
        manifest.runtime_dependencies.push(lito::manifest::DeclaredRuntimeDependency {
            .name             = reference.name.clone(),
            .source           = clone_package_source(definition->source),
            .declaration_root = Some(workspace.root.clone()),
        });
    }
    manifest.workspace_runtime_dependencies.clear();

    for (const auto& reference : manifest.workspace_pkg_config_external_dependencies) {
        const lito::manifest::WorkspacePkgConfigExternalDependencyDefinition* definition = nullptr;
        for (const auto& candidate : workspace.pkg_config_external_dependencies) {
            if (candidate.alias == reference.alias) {
                definition = rstd::addressof(candidate);
                break;
            }
        }
        if (definition == nullptr) {
            return workspace_failure<empty>(rstd::format(
                "workspace member '{}' inherits pkg-config dependency '{}' but "
                "workspace.external-dependencies.pkg-config has no matching definition",
                manifest.name.as_str(),
                reference.alias.as_str()));
        }
        manifest.pkg_config_external_dependencies.push(
            lito::dependency::PkgConfigExternalDependency {
                .alias       = reference.alias.clone(),
                .requirement = clone_pkg_config_requirement(definition->requirement),
                .visibility  = reference.visibility,
            });
    }
    manifest.workspace_pkg_config_external_dependencies.clear();

    for (const auto& reference : manifest.workspace_cmake_external_dependencies) {
        const lito::manifest::WorkspaceCMakeExternalDependencyDefinition* definition = nullptr;
        for (const auto& candidate : workspace.cmake_external_dependencies) {
            if (candidate.alias == reference.alias) {
                definition = rstd::addressof(candidate);
                break;
            }
        }
        if (definition == nullptr) {
            return workspace_failure<empty>(
                rstd::format("workspace member '{}' inherits CMake dependency '{}' but "
                             "workspace.external-dependencies.cmake has no matching definition",
                             manifest.name.as_str(),
                             reference.alias.as_str()));
        }
        auto adapter = Option<PathBuf> {};
        if (definition->adapter.is_some()) adapter = Some(definition->adapter->clone());
        auto config_directory = Option<PathBuf> {};
        if (definition->config_directory.is_some()) {
            config_directory = Some(definition->config_directory->clone());
        }
        auto cache = Vec<lito::dependency::CMakeCacheEntry>::with_capacity(definition->cache.len());
        for (const auto& entry : definition->cache) {
            cache.push(lito::dependency::CMakeCacheEntry { .name  = entry.name.clone(),
                                                           .value = entry.value.clone() });
        }
        auto targets =
            Vec<lito::dependency::CMakeTargetRequirement>::with_capacity(reference.targets.len());
        for (const auto& target : reference.targets) {
            targets.push(lito::dependency::CMakeTargetRequirement {
                .name       = target.name.clone(),
                .visibility = target.visibility,
            });
        }
        manifest.cmake_external_dependencies.push(lito::dependency::CMakeDependencyRequirement {
            .alias            = reference.alias.clone(),
            .package          = definition->package.clone(),
            .source           = definition->source.clone(),
            .integration      = definition->integration,
            .add_subdirectory = definition->add_subdirectory,
            .adapter          = rstd::move(adapter),
            .config_directory = rstd::move(config_directory),
            .cache            = rstd::move(cache),
            .targets          = rstd::move(targets),
            .declaration_root = Some(workspace.root.clone()),
            .adapter_root     = Some(workspace.root.clone()),
        });
    }
    manifest.workspace_cmake_external_dependencies.clear();
    return Ok(empty {});
}

auto resolve_workspace_member(lito::manifest::PackageManifest&         manifest,
                              const lito::manifest::WorkspaceManifest& workspace)
    -> WorkspaceResult<empty> {
    rstd_try(resolve_workspace_member_version(manifest, workspace));
    rstd_try(resolve_workspace_member_license(manifest, workspace));
    return resolve_workspace_member_dependencies(manifest, workspace);
}

auto resolve_containing_workspace_version(lito::manifest::PackageManifest& manifest)
    -> WorkspaceResult<empty> {
    if (manifest.version.source != lito::manifest::PackageVersionSource::Workspace) {
        return Ok(empty {});
    }

    auto directory = manifest.root.clone();
    while (directory.pop()) {
        auto located = lito::manifest::try_locate_manifest(directory.as_path());
        if (located.is_err()) {
            return Err(WorkspaceError::Manifest(
                lito::manifest::ManifestError::Locate(rstd::move(located).unwrap_err())));
        }
        if (located->is_none()) continue;

        auto document = lito::manifest::load_manifest_document(directory.as_path());
        if (document.is_err()) {
            return Err(rstd::into<WorkspaceError>(rstd::move(document).unwrap_err()));
        }
        auto loaded = rstd::move(document).unwrap();
        if (loaded.kind != lito::manifest::ManifestKind::Workspace || loaded.workspace.is_none())
            continue;
        auto workspace = rstd::move(loaded.workspace).unwrap();
        for (const auto& declared : workspace.members) {
            auto member =
                workspace_member_directory(workspace, declared.as_path(), "workspace member"_str);
            if (member.is_err()) return Err(rstd::move(member).unwrap_err());
            if (! same_path(member->as_path(), manifest.root.as_path())) continue;
            return resolve_workspace_member_version(manifest, workspace);
        }
    }

    return workspace_failure<empty>(
        rstd::format("package '{}' inherits package.version but no containing "
                     "workspace lists directory '{}'",
                     manifest.name.as_str(),
                     manifest.root.as_path()));
}

} // namespace lito::workspace
