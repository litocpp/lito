export module lito.workspace.resolver;

import rstd;
import lito.error;
import lito.lock.contract;
import lito.package.graph_contract;
import lito.workspace.contract;
import lito.platform.contract;
import lito.manifest;
import lito.package;
import lito.system.environment;

using namespace rstd::prelude;
using namespace rstd::literals;
using IndexMap  = rstd::collections::BTreeMap<String, usize>;
using StringSet = rstd::collections::BTreeMap<String, empty>;

namespace lito
{

template<typename T>
auto failure(String message) -> Result<T> {
    return Err(Error::make(ErrorKind::Manifest, rstd::move(message)));
}

template<typename T>
auto failure(ref<str> message) -> Result<T> {
    return Err(Error::make(ErrorKind::Manifest, message));
}

auto copy_strings(const Vec<String>& values) -> Vec<String> {
    auto result = Vec<String>::with_capacity(values.len());
    for (const auto& value : values) result.push(value.clone());
    return result;
}

auto selected_by_purpose(ProjectRootRole         role,
                         PackageTargetKind       kind,
                         PackageSelectionPurpose purpose) -> bool {
    if (purpose == PackageSelectionPurpose::All) return true;
    if (role == ProjectRootRole::AssociatedTest) {
        return purpose == PackageSelectionPurpose::Test &&
               (kind == PackageTargetKind::Test || kind == PackageTargetKind::CompileTest);
    }
    if (purpose == PackageSelectionPurpose::Test) {
        return kind == PackageTargetKind::Test || kind == PackageTargetKind::CompileTest;
    }
    if (purpose == PackageSelectionPurpose::Benchmark) {
        return kind == PackageTargetKind::Benchmark;
    }
    return kind == PackageTargetKind::Library || kind == PackageTargetKind::Binary;
}

auto purpose_name(PackageSelectionPurpose purpose) noexcept -> ref<str> {
    switch (purpose) {
    case PackageSelectionPurpose::All: return "all"_str;
    case PackageSelectionPurpose::Production: return "production"_str;
    case PackageSelectionPurpose::Test: return "test"_str;
    case PackageSelectionPurpose::Benchmark: return "benchmark"_str;
    }
    return "unknown"_str;
}

auto append_selected_targets(Vec<PackageTargetId>&   output,
                             const PackageManifest&  package,
                             ProjectRootRole         role,
                             PackageSelectionPurpose purpose) -> bool {
    auto selected = false;
    for (const auto& target : package.targets) {
        const auto kind = package_target_kind(target);
        if (! selected_by_purpose(role, kind, purpose)) continue;
        output.push(PackageTargetId {
            .package = package.name.clone(),
            .kind    = kind,
            .name    = String::make(package_target_name(target)),
        });
        selected = true;
    }
    if (! package.compile_tests.is_empty() &&
        selected_by_purpose(role, PackageTargetKind::CompileTest, purpose)) {
        output.push(PackageTargetId {
            .package = package.name.clone(),
            .kind    = PackageTargetKind::CompileTest,
            .name    = package.name.clone(),
        });
        selected = true;
    }
    return selected;
}

auto selected_closure(const ResolvedPackageGraph& graph,
                      const Vec<String>&          selected_roots,
                      const Vec<PackageTargetId>& selected_targets,
                      const TargetInfo*           target) -> Result<Vec<String>> {
    auto indices = IndexMap::make();
    for (usize index {}; index < graph.packages.len(); ++index) {
        indices.insert(graph.packages[index].manifest.name.clone(), index);
    }

    auto development = StringSet::make();
    for (const auto& selected_target : selected_targets) {
        if (selected_target.kind == PackageTargetKind::Test ||
            selected_target.kind == PackageTargetKind::Benchmark ||
            selected_target.kind == PackageTargetKind::CompileTest) {
            development.insert(selected_target.package.clone(), empty {});
        }
    }

    auto pending  = copy_strings(selected_roots);
    auto selected = StringSet::make();
    while (! pending.is_empty()) {
        auto current = rstd::move(pending.pop()).unwrap();
        if (selected.contains_key(current.as_str())) continue;
        auto index = indices.get(current.as_str());
        if (index.is_none()) {
            return failure<Vec<String>>(rstd::format(
                "selected package '{}' is missing from resolved graph", current.as_str()));
        }
        if (target != nullptr && ! graph.packages[**index].manifest.target.matches(*target)) {
            return failure<Vec<String>>(rstd::format("package '{}' does not support target '{}'",
                                                     current.as_str(),
                                                     target->triple.as_str()));
        }
        selected.insert(current.clone(), empty {});
        for (const auto& dependency : graph.packages[**index].dependencies) {
            pending.push(dependency.name.clone());
        }
        if (development.contains_key(current.as_str())) {
            for (const auto& dependency : graph.packages[**index].dev_dependencies) {
                pending.push(dependency.name.clone());
            }
        }
    }

    auto result = Vec<String>::make();
    for (const auto& package : graph.packages) {
        if (selected.contains_key(package.manifest.name.as_str())) {
            result.push(package.manifest.name.clone());
        }
    }
    return Ok(rstd::move(result));
}

} // namespace lito

export namespace lito
{

auto resolve_package_selection_with_environment(const PackageSelection&           selection,
                                                PackageSelectionPurpose           purpose,
                                                PackageResolutionOptions          options,
                                                const TargetInfo*                 target,
                                                ToolResolver&                     tool_resolver,
                                                const ResolvedProcessEnvironment& environment,
                                                usize                             jobs = usize(1))
    -> Result<ResolvedPackageSelection> {
    auto resolved = resolve_package_graph_with_environment(
        selection.root.as_path(), rstd::move(options), tool_resolver, environment, jobs);
    if (resolved.is_err()) return Err(rstd::move(resolved).unwrap_err());
    auto graph      = rstd::move(resolved).unwrap();
    auto root_roles = rstd::collections::BTreeMap<String, ProjectRootRole>::make();
    for (const auto& root : graph.roots) root_roles.insert(root.name.clone(), root.role);
    auto selected_roots   = Vec<String>::make();
    auto selected_targets = Vec<PackageTargetId>::make();
    if (selection.packages.is_empty()) {
        for (const auto& root : graph.roots) {
            const auto&            name      = root.name;
            auto                   supported = true;
            const PackageManifest* manifest  = nullptr;
            if (target != nullptr) {
                for (const auto& package : graph.packages) {
                    if (package.manifest.name.as_str() == name.as_str()) {
                        supported = package.manifest.target.matches(*target);
                        manifest  = rstd::addressof(package.manifest);
                        break;
                    }
                }
            } else {
                for (const auto& package : graph.packages) {
                    if (package.manifest.name.as_str() == name.as_str()) {
                        manifest = rstd::addressof(package.manifest);
                        break;
                    }
                }
            }
            if (manifest != nullptr && supported &&
                append_selected_targets(selected_targets, *manifest, root.role, purpose)) {
                selected_roots.push(name.clone());
            }
        }
    } else {
        auto selected_names = StringSet::make();
        for (const auto& name : selection.packages) {
            if (! valid_package_name(name.as_str())) {
                return failure<ResolvedPackageSelection>(
                    rstd::format("package selection '{}' must contain only ASCII "
                                 "letters, digits, '-' or '_'",
                                 name.as_str()));
            }
            if (selected_names.contains_key(name.as_str())) {
                return failure<ResolvedPackageSelection>(rstd::format(
                    "project package '{}' was selected more than once", name.as_str()));
            }
            auto role = root_roles.get(name.as_str());
            if (role.is_none()) {
                return failure<ResolvedPackageSelection>(
                    rstd::format("project has no root package named '{}'", name.as_str()));
            }
            const PackageManifest* manifest = nullptr;
            if (target != nullptr) {
                for (const auto& package : graph.packages) {
                    if (package.manifest.name.as_str() != name.as_str()) continue;
                    manifest = rstd::addressof(package.manifest);
                    if (! package.manifest.target.matches(*target)) {
                        return failure<ResolvedPackageSelection>(
                            rstd::format("package '{}' does not support target '{}'",
                                         name.as_str(),
                                         target->triple.as_str()));
                    }
                    break;
                }
            } else {
                for (const auto& package : graph.packages) {
                    if (package.manifest.name.as_str() == name.as_str()) {
                        manifest = rstd::addressof(package.manifest);
                        break;
                    }
                }
            }
            if (manifest == nullptr ||
                ! append_selected_targets(selected_targets, *manifest, **role, purpose)) {
                return failure<ResolvedPackageSelection>(rstd::format(
                    "project package '{}' has no {} target", name.as_str(), purpose_name(purpose)));
            }
            selected_names.insert(name.clone(), empty {});
            selected_roots.push(name.clone());
        }
    }
    if (selected_roots.is_empty()) {
        return failure<ResolvedPackageSelection>(
            rstd::format("project has no selected {} package", purpose_name(purpose)));
    }
    rstd::slice_::sort_unstable(selected_roots.as_mut_slice().as_mut_ref());

    auto selected_packages = selected_closure(graph, selected_roots, selected_targets, target);
    if (selected_packages.is_err()) {
        return Err(rstd::move(selected_packages).unwrap_err());
    }
    return Ok(ResolvedPackageSelection {
        .graph                  = rstd::move(graph),
        .selected_root_names    = rstd::move(selected_roots),
        .selected_package_names = rstd::move(selected_packages).unwrap(),
        .selected_targets       = rstd::move(selected_targets),
    });
}

auto resolve_package_selection(const PackageSelection&  selection,
                               PackageSelectionPurpose  purpose = PackageSelectionPurpose::All,
                               PackageResolutionOptions options = {},
                               const TargetInfo*        target  = nullptr)
    -> Result<ResolvedPackageSelection> {
    auto environment = ResolvedProcessEnvironment::resolve(ProcessEnvironmentSpec {});
    if (environment.is_err()) return Err(rstd::move(environment).unwrap_err());
    auto resolver = ToolResolver(*environment);
    return resolve_package_selection_with_environment(
        selection, purpose, rstd::move(options), target, resolver, *environment);
}

} // namespace lito
