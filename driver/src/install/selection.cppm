module;
#include <rstd/macro.hpp>

export module lito.driver:install.selection;

import rstd;
import lito.core;
import :install.error;
import :install.recipe;
import :install.package;
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

auto resolved_package(const ResolvedPackageGraph& graph, ref<str> name)
    -> InstallResult<const ResolvedPackage*> {
    const ResolvedPackage* result = nullptr;
    for (const auto& package : graph.packages) {
        if (package.manifest.name != name) continue;
        if (result != nullptr) {
            return selection_failure<const ResolvedPackage*>(
                rstd::format("resolved graph contains package '{}' more than once", name));
        }
        result = rstd::addressof(package);
    }
    if (result == nullptr) {
        return selection_failure<const ResolvedPackage*>(
            rstd::format("install package '{}' is missing from the resolved graph", name));
    }
    return Ok(result);
}

auto direct_package(const ResolvedPackageSelection& selection, ref<str> name) -> bool {
    for (const auto& direct : selection.selected_root_names) {
        if (direct == name) return true;
    }
    return false;
}

} // namespace lito

export namespace lito
{

auto resolve_install_packages(const ResolvedPackageSelection& selection, const TargetInfo& target)
    -> InstallResult<Vec<PackageInstallInput>> {
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
            if (package_target_kind(candidate) != PackageTargetKind::Binary) continue;
            binaries.push(PackageInstallTarget {
                .target =
                    PackageTargetId {
                        .package = package->manifest.name.clone(),
                        .kind    = PackageTargetKind::Binary,
                        .name    = String::make(package_target_name(candidate)),
                    },
                .artifact_name = String::make(package_target_artifact_name(candidate)),
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
        result.push(PackageInstallInput {
            .name                 = package->manifest.name.clone(),
            .version              = package->manifest.version.value->clone(),
            .root                 = package->manifest.root.clone(),
            .manifest_path        = package->manifest.manifest_path.clone(),
            .script               = rstd::move(script),
            .binaries             = rstd::move(binaries),
            .source               = package->source.clone(),
            .runtime_dependencies = rstd::move(runtime_dependencies),
            .direct               = direct_package(selection, name.as_str()),
        });
    }
    if (result.is_empty()) {
        return selection_failure<Vec<PackageInstallInput>>(
            "project has no selected install package"_str);
    }
    return Ok(rstd::move(result));
}

auto resolve_install_build_requirements(const ResolvedPackageSelection& selection,
                                        const Vec<InstallRecipe>&       recipes,
                                        const TargetInfo&               target)
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
                                     package_target_id_text(artifact.target)));
                }
            }
            auto found = false;
            for (const auto& candidate : owner->manifest.targets) {
                if (package_target_kind(candidate) == artifact.target.kind &&
                    package_target_name(candidate) == artifact.target.name.as_str()) {
                    found = true;
                    break;
                }
            }
            if (! found) {
                return selection_failure<InstallBuildRequirements>(
                    rstd::format("unknown install artifact target '{}'",
                                 package_target_id_text(artifact.target)));
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
    }
    return Ok(rstd::move(requirements));
}

} // namespace lito
