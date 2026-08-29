module;
#include <rstd/macro.hpp>

export module lito.driver:install.selection;

import rstd;
import lito.core;
import :package.selection;
import :install.error;
import :install.recipe;
import :install.package;
import :install.path;
import :dependency.preparation;
import lito.system;

using namespace rstd::prelude;
using namespace lito::system;
using namespace rstd::literals;

namespace lito
{

template<typename T>
auto selection_failure(String message) -> InstallResult<T> {
    return Err(InstallError::Message(rstd::move(message)));
}

template<typename T>
auto selection_failure(ref<str> message) -> InstallResult<T> {
    return selection_failure<T>(String::make(message));
}

auto resolved_package(const lito::package::ResolvedPackageGraph& graph, ref<str> name)
    -> InstallResult<const lito::package::ResolvedPackage*> {
    const lito::package::ResolvedPackage* result = nullptr;
    for (const auto& package : graph.packages) {
        if (package.manifest.name != name) continue;
        if (result != nullptr) {
            return selection_failure<const lito::package::ResolvedPackage*>(
                rstd::format("resolved graph contains package '{}' more than once", name));
        }
        result = rstd::addressof(package);
    }
    if (result == nullptr) {
        return selection_failure<const lito::package::ResolvedPackage*>(
            rstd::format("install package '{}' is missing from the resolved graph", name));
    }
    return Ok(result);
}

auto direct_package(const lito::package::ResolvedPackageSelection& selection, ref<str> name)
    -> bool {
    for (const auto& direct : selection.selected_root_names) {
        if (direct == name) return true;
    }
    return false;
}

auto install_pkg_config_version_operator(lito::dependency::PkgConfigVersionOperator value) noexcept
    -> ref<str> {
    using Operator = lito::dependency::PkgConfigVersionOperator;
    switch (value) {
    case Operator::Equal: return "="_str;
    case Operator::Less: return "<"_str;
    case Operator::Greater: return ">"_str;
    case Operator::LessEqual: return "<="_str;
    case Operator::GreaterEqual: return ">="_str;
    }
    return "="_str;
}

auto pkg_config_requirement(const lito::dependency::PkgConfigExternalDependency& dependency)
    -> InstallResult<String> {
    if (dependency.usage == lito::dependency::PkgConfigDependencyUsage::Compile) {
        return selection_failure<String>(rstd::format(
            "compile-only pkg-config dependency '{}' cannot be represented in a Requires field",
            dependency.alias.as_str()));
    }
    auto result = dependency.requirement.module.clone();
    if (dependency.requirement.version.is_some()) {
        result.push_ascii(' ');
        result.push_str(
            install_pkg_config_version_operator(dependency.requirement.version->comparison));
        result.push_ascii(' ');
        result.push_str(dependency.requirement.version->value.as_str());
    }
    for (auto byte : result.as_str().as_bytes()) {
        if (byte == u8('\n') || byte == u8('\r') || byte == u8(',')) {
            return selection_failure<String>(
                rstd::format("pkg-config dependency '{}' cannot be represented in a Requires field",
                             dependency.alias.as_str()));
        }
    }
    return Ok(rstd::move(result));
}

auto append_unique(Vec<String>& values, String value) -> void {
    for (const auto& existing : values) {
        if (existing == value.as_str()) return;
    }
    values.push(rstd::move(value));
}

auto resolve_pkg_config_file(const InstallRecipe&                  recipe,
                             const InstallPkgConfigRecipe&         requested,
                             const lito::package::ResolvedPackage& owner)
    -> InstallResult<ResolvedInstallPkgConfigFile> {
    const lito::manifest::PackageTargetManifest* manifest_target = nullptr;
    for (const auto& candidate : owner.manifest.targets) {
        if (lito::manifest::package_target_kind(candidate) != requested.target.kind ||
            lito::manifest::package_target_name(candidate) != requested.target.name.as_str())
            continue;
        manifest_target = rstd::addressof(candidate);
        break;
    }
    if (manifest_target == nullptr) {
        return selection_failure<ResolvedInstallPkgConfigFile>(
            rstd::format("unknown pkg_config target '{}'",
                         lito::package::package_target_id_text(requested.target)));
    }
    if (! lito::manifest::package_library_is_shared(*manifest_target)) {
        return selection_failure<ResolvedInstallPkgConfigFile>(
            rstd::format("pkg_config target '{}' is not a shared library",
                         lito::package::package_target_id_text(requested.target)));
    }

    const InstallArtifactRecipe* artifact = nullptr;
    for (const auto& candidate : recipe.artifacts) {
        if (candidate.target != requested.target) continue;
        artifact = rstd::addressof(candidate);
        break;
    }
    if (artifact == nullptr) {
        return selection_failure<ResolvedInstallPkgConfigFile>(
            rstd::format("pkg_config target '{}' must also be selected by artifacts",
                         lito::package::package_target_id_text(requested.target)));
    }
    auto parent = artifact->destination.as_path().parent();
    if (parent.is_none()) {
        return selection_failure<ResolvedInstallPkgConfigFile>(
            rstd::format("installed library destination '{}' has no parent directory",
                         artifact->destination.as_path()));
    }
    auto library_directory = PathBuf::from(*parent);
    auto library_name =
        String::make(lito::manifest::package_target_artifact_name(*manifest_target));
    auto module = requested.module.is_empty() ? library_name.clone() : requested.module.clone();
    auto name   = requested.name.is_empty() ? module.clone() : requested.name.clone();
    auto destination = PathBuf::make();
    if (requested.destination.is_some()) {
        destination = requested.destination->clone();
    } else {
        destination = library_directory.join(PathBuf::from("pkgconfig"_str).as_path());
        destination.push(PathBuf::from(rstd::format("{}.pc", module.as_str()).as_str()).as_path());
    }

    auto public_dependencies  = Vec<String>::make();
    auto private_dependencies = Vec<String>::make();
    for (const auto& alias : requested.dependencies) {
        const lito::dependency::PkgConfigExternalDependency* dependency = nullptr;
        for (const auto& candidate : owner.manifest.pkg_config_external_dependencies) {
            if (candidate.alias == alias.as_str()) {
                dependency = rstd::addressof(candidate);
                break;
            }
        }
        if (dependency == nullptr) {
            return selection_failure<ResolvedInstallPkgConfigFile>(rstd::format(
                "pkg_config target '{}' references undeclared pkg-config dependency '{}'",
                lito::package::package_target_id_text(requested.target),
                alias.as_str()));
        }
        auto requirement = rstd_try(pkg_config_requirement(*dependency));
        if (dependency->visibility == lito::dependency::DependencyVisibility::Public) {
            append_unique(public_dependencies, rstd::move(requirement));
        } else {
            append_unique(private_dependencies, rstd::move(requirement));
        }
    }
    return Ok(ResolvedInstallPkgConfigFile {
        .owner                = recipe.owner.clone(),
        .target               = requested.target.clone(),
        .module               = rstd::move(module),
        .name                 = rstd::move(name),
        .description          = requested.description.clone(),
        .destination          = rstd::move(destination),
        .library_directory    = rstd::move(library_directory),
        .library_name         = rstd::move(library_name),
        .include_directory    = as<Clone>(requested.include_directory).clone(),
        .public_dependencies  = rstd::move(public_dependencies),
        .private_dependencies = rstd::move(private_dependencies),
    });
}

} // namespace lito

export namespace lito
{

auto resolve_install_packages(const lito::package::ResolvedPackageSelection& selection,
                              const TargetInfo& target) -> InstallResult<Vec<PackageInstallInput>> {
    auto result = Vec<PackageInstallInput>::make();
    for (const auto& name : selection.install_package_names) {
        const auto* package = rstd_try(resolved_package(selection.graph, name.as_str()));
        if (! package->manifest.target.matches(target)) {
            return selection_failure<Vec<PackageInstallInput>>(
                rstd::format("install package '{}' does not support target '{}'",
                             name.as_str(),
                             target.triple.as_str()));
        }
        if (package->manifest.version.value.is_none()) {
            return selection_failure<Vec<PackageInstallInput>>(
                rstd::format("package '{}' has no installable version", name.as_str()));
        }
        auto binaries = Vec<PackageInstallTarget>::make();
        for (const auto& candidate : package->manifest.targets) {
            if (lito::manifest::package_target_kind(candidate) !=
                lito::package::PackageTargetKind::Binary)
                continue;
            binaries.push(PackageInstallTarget {
                .target =
                    lito::package::PackageTargetId {
                        .package = package->manifest.name.clone(),
                        .kind    = lito::package::PackageTargetKind::Binary,
                        .name    = String::make(lito::manifest::package_target_name(candidate)),
                    },
                .artifact_name =
                    String::make(lito::manifest::package_target_artifact_name(candidate)),
            });
        }
        if (package->manifest.install_script.is_none() && binaries.is_empty()) {
            return selection_failure<Vec<PackageInstallInput>>(
                rstd::format("runtime package '{}' has no install target", name.as_str()));
        }
        auto script = Option<PathBuf> {};
        if (package->manifest.install_script.is_some()) {
            script = Some(package->manifest.install_script->clone());
        }
        auto runtime_dependencies = Vec<InstallRuntimeDependency>::make();
        for (const auto& dependency : package->runtime_dependencies) {
            const auto* runtime =
                rstd_try(resolved_package(selection.graph, dependency.name.as_str()));
            runtime_dependencies.push(InstallRuntimeDependency {
                .name            = dependency.name.clone(),
                .source_identity = runtime->source_identity.clone(),
            });
        }
        auto script_dependencies = Vec<String>::make();
        for (const auto& dependency : package->dependencies) {
            if (dependency.is_Script()) {
                script_dependencies.push(dependency.as_Script().value.name.clone());
            }
        }
        auto script_packages = Vec<lito::package::ResolvedScriptPackageView>::make();
        for (const auto& provider : selection.graph.packages) {
            if (provider.manifest.script.is_none()) continue;
            auto dependencies = Vec<String>::make();
            for (const auto& dependency : provider.dependencies) {
                if (dependency.is_Script()) {
                    dependencies.push(dependency.as_Script().value.name.clone());
                }
            }
            script_packages.push(lito::package::ResolvedScriptPackageView {
                .name = provider.manifest.name.clone(),
                .require_name =
                    lito::manifest::script_require_name(provider.manifest.name.as_str()),
                .source_identity = provider.source_identity.clone(),
                .supports        = provider.manifest.script->supports.clone(),
                .root            = provider.manifest.root.clone(),
                .embedded_source = provider.embedded_source.is_some()
                                       ? Some(provider.embedded_source->clone())
                                       : Option<lito::source::SourceTree> {},
                .dependencies    = rstd::move(dependencies),
            });
        }
        result.push(PackageInstallInput {
            .name                 = package->manifest.name.clone(),
            .version              = package->manifest.version.value->clone(),
            .root                 = package->manifest.root.clone(),
            .manifest_path        = package->manifest.manifest_path.clone(),
            .script               = rstd::move(script),
            .binaries             = rstd::move(binaries),
            .source               = package->source.clone(),
            .runtime_dependencies = rstd::move(runtime_dependencies),
            .script_dependencies  = rstd::move(script_dependencies),
            .script_packages      = rstd::move(script_packages),
            .direct               = direct_package(selection, name.as_str()),
        });
    }
    if (result.is_empty()) {
        return selection_failure<Vec<PackageInstallInput>>(
            "project has no selected install package"_str);
    }
    return Ok(rstd::move(result));
}

auto resolve_install_build_requirements(const lito::package::ResolvedPackageSelection& selection,
                                        const Vec<InstallRecipe>&                      recipes,
                                        const TargetInfo&                              target)
    -> InstallResult<InstallBuildRequirements> {
    auto requirements = InstallBuildRequirements {};
    for (const auto& recipe : recipes) {
        const auto* owner = rstd_try(resolved_package(selection.graph, recipe.owner.as_str()));
        if (! owner->manifest.target.matches(target)) {
            return selection_failure<InstallBuildRequirements>(
                rstd::format("install recipe owner '{}' does not support target '{}'",
                             recipe.owner.as_str(),
                             target.triple.as_str()));
        }
        for (usize artifact_index {}; artifact_index < recipe.artifacts.len(); ++artifact_index) {
            const auto& artifact = recipe.artifacts[artifact_index];
            if (artifact.target.package != recipe.owner.as_str()) {
                return selection_failure<InstallBuildRequirements>(rstd::format(
                    "install recipe '{}' cannot install artifact owned by package '{}'",
                    recipe.owner.as_str(),
                    artifact.target.package.as_str()));
            }
            for (usize prior {}; prior < artifact_index; ++prior) {
                if (recipe.artifacts[prior].target == artifact.target) {
                    return selection_failure<InstallBuildRequirements>(
                        rstd::format("install recipe '{}' repeats artifact target '{}'",
                                     recipe.owner.as_str(),
                                     lito::package::package_target_id_text(artifact.target)));
                }
            }
            auto found = false;
            for (const auto& candidate : owner->manifest.targets) {
                if (lito::manifest::package_target_kind(candidate) == artifact.target.kind &&
                    lito::manifest::package_target_name(candidate) ==
                        artifact.target.name.as_str()) {
                    found = true;
                    break;
                }
            }
            if (! found) {
                return selection_failure<InstallBuildRequirements>(
                    rstd::format("unknown install artifact target '{}'",
                                 lito::package::package_target_id_text(artifact.target)));
            }
            if (! artifact.runtime_search.is_empty()) {
                if (artifact.target.kind != lito::package::PackageTargetKind::Binary) {
                    return selection_failure<InstallBuildRequirements>(rstd::format(
                        "install runtime search is only supported for binary target '{}'",
                        lito::package::package_target_id_text(artifact.target)));
                }
                if (target.platform != lito::system::TargetPlatform::Linux) {
                    return selection_failure<InstallBuildRequirements>(rstd::format(
                        "install runtime search for '{}' requires a Linux target, got '{}'",
                        lito::package::package_target_id_text(artifact.target),
                        target.triple.as_str()));
                }
                auto assets = Vec<InstallRuntimeSearchAsset>::make();
                for (const auto& reference : artifact.runtime_search) {
                    const InstallExternalAssetRecipe* matched = nullptr;
                    for (const auto& candidate : recipe.external_assets) {
                        if (candidate.dependency != reference.dependency.as_str() ||
                            candidate.set != reference.set.as_str())
                            continue;
                        if (matched != nullptr) {
                            return selection_failure<InstallBuildRequirements>(rstd::format(
                                "install recipe '{}' has ambiguous external asset '{}:{}'",
                                recipe.owner.as_str(),
                                reference.dependency.as_str(),
                                reference.set.as_str()));
                        }
                        matched = rstd::addressof(candidate);
                    }
                    if (matched == nullptr) {
                        return selection_failure<InstallBuildRequirements>(
                            rstd::format("install artifact '{}' runtime search references "
                                         "undeclared external asset '{}:{}'",
                                         lito::package::package_target_id_text(artifact.target),
                                         reference.dependency.as_str(),
                                         reference.set.as_str()));
                    }
                    assets.push(InstallRuntimeSearchAsset {
                        .dependency  = reference.dependency.clone(),
                        .set         = reference.set.clone(),
                        .destination = matched->destination.clone(),
                    });
                }
                requirements.runtime_search.push(InstallArtifactRuntimeSearchRequirement {
                    .target                  = artifact.target.clone(),
                    .package_source_identity = owner->source_identity.clone(),
                    .destination             = artifact.destination.clone(),
                    .assets                  = rstd::move(assets),
                });
            }
            auto duplicate = false;
            for (const auto& selected : requirements.targets) {
                if (selected == artifact.target) {
                    duplicate = true;
                    break;
                }
            }
            if (! duplicate) requirements.targets.push(artifact.target.clone());
        }
        for (const auto& requested : recipe.pkg_config) {
            if (requested.target.package != recipe.owner.as_str()) {
                return selection_failure<InstallBuildRequirements>(rstd::format(
                    "install recipe '{}' cannot export pkg_config for target owned by package '{}'",
                    recipe.owner.as_str(),
                    requested.target.package.as_str()));
            }
            auto resolved = rstd_try(resolve_pkg_config_file(recipe, requested, *owner));
            for (const auto& existing : requirements.pkg_config) {
                if (existing.owner == resolved.owner.as_str() &&
                    existing.destination.as_path() == resolved.destination.as_path()) {
                    return selection_failure<InstallBuildRequirements>(
                        rstd::format("install recipe '{}' repeats pkg_config destination '{}'",
                                     recipe.owner.as_str(),
                                     resolved.destination.as_path()));
                }
            }
            requirements.pkg_config.push(rstd::move(resolved));
        }
    }
    return Ok(rstd::move(requirements));
}

auto resolve_install_artifact_link_variants(InstallBuildRequirements&   requirements,
                                            const ExternalAssetCatalog& catalog)
    -> InstallResult<empty> {
    if (! requirements.artifact_link_variants.is_empty()) {
        return selection_failure<empty>("install artifact link variants were already resolved"_str);
    }
    for (const auto& requirement : requirements.runtime_search) {
        auto paths        = Vec<lito::artifact::OriginRelativeRuntimePath>::make();
        auto materialized = false;
        auto provided     = false;
        for (const auto& asset : requirement.assets) {
            auto set = catalog.resolve(asset.dependency.as_str(), asset.set.as_str());
            if (set.is_err()) {
                return selection_failure<empty>(rstd::move(set).unwrap_err());
            }
            const auto* resolved_set = *set;
            if (resolved_set->disposition == ExternalAssetDisposition::Provided) {
                provided = true;
                continue;
            }
            materialized   = true;
            auto validated = install_runtime_search_path(requirement.destination.as_path(),
                                                         asset.destination.as_path());
            if (validated.is_err()) {
                return selection_failure<empty>(
                    rstd::format("install runtime search from '{}' to '{}' is invalid: {}",
                                 requirement.destination.as_path(),
                                 asset.destination.as_path(),
                                 rstd::move(validated).unwrap_err()));
            }
            paths.push(rstd::move(validated).unwrap());
        }
        if (materialized && provided) {
            return selection_failure<empty>(rstd::format(
                "install artifact '{}' cannot mix provided and materialized runtime asset sets",
                lito::package::package_target_id_text(requirement.target)));
        }
        if (! materialized) continue;
        auto runpath = lito::artifact::make_elf_runpath(rstd::move(paths));
        if (runpath.is_err()) {
            return selection_failure<empty>(
                rstd::format("install runtime search for '{}' is invalid: {}",
                             lito::package::package_target_id_text(requirement.target),
                             rstd::move(runpath).unwrap_err()));
        }
        auto identity = String::make("lito-install-link-v3\n"_str);
        identity.push_str(lito::package::package_target_id_text(requirement.target).as_str());
        identity.push_ascii('\n');
        identity.push_str(rstd::format("package-source={}:{}\n",
                                       requirement.package_source_identity.len(),
                                       requirement.package_source_identity.as_str())
                              .as_str());
        identity.push_str(lito::artifact::elf_runpath_identity(*runpath).as_str());
        requirements.artifact_link_variants.push(RequestedArtifactLinkVariant {
            .target = requirement.target.clone(),
            .policy =
                InstallArtifactLinkPolicy {
                    .runtime_search = rstd::move(runpath).unwrap(),
                    .identity       = rstd::move(identity),
                },
        });
    }
    return Ok(empty {});
}

} // namespace lito
