export module tenon.workspace_resolver;

import rstd;
import tenon.model;
import tenon.manifest;
import tenon.package;

using namespace rstd::prelude;
using namespace rstd::literals;
using IndexMap  = rstd::collections::BTreeMap<String, usize>;
using StringSet = rstd::collections::BTreeMap<String, empty>;

namespace tenon
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

auto selected_by_purpose(ArtifactKind kind, PackageSelectionPurpose purpose) -> bool {
    if (purpose == PackageSelectionPurpose::All) return true;
    if (purpose == PackageSelectionPurpose::Test) {
        return kind == ArtifactKind::TestExecutable || kind == ArtifactKind::CompileTest;
    }
    return kind != ArtifactKind::TestExecutable && kind != ArtifactKind::CompileTest;
}

auto selected_closure(const ResolvedPackageGraph& graph,
                      const Vec<String>&          selected_roots,
                      const TargetInfo*           target) -> Result<Vec<String>> {
    auto indices = IndexMap::make();
    for (usize index {}; index < graph.packages.len(); ++index) {
        indices.insert(graph.packages[index].manifest.name.clone(), index);
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
    }

    auto result = Vec<String>::make();
    for (const auto& package : graph.packages) {
        if (selected.contains_key(package.manifest.name.as_str())) {
            result.push(package.manifest.name.clone());
        }
    }
    return Ok(rstd::move(result));
}

} // namespace tenon

export namespace tenon
{

auto resolve_package_selection(const PackageSelection&  selection,
                               PackageSelectionPurpose  purpose = PackageSelectionPurpose::All,
                               PackageResolutionOptions options = {},
                               const TargetInfo*        target  = nullptr)
    -> Result<ResolvedPackageSelection> {
    auto resolved = resolve_package_graph(selection.root.as_path(), rstd::move(options));
    if (resolved.is_err()) return Err(rstd::move(resolved).unwrap_err());
    auto graph = rstd::move(resolved).unwrap();
    if (! graph.root_is_workspace && ! selection.packages.is_empty()) {
        return failure<ResolvedPackageSelection>("--package requires a workspace directory"_str);
    }

    auto roots = StringSet::make();
    for (const auto& name : graph.root_names) roots.insert(name.clone(), empty {});
    auto root_kinds = rstd::collections::BTreeMap<String, ArtifactKind>::make();
    for (const auto& package : graph.packages) {
        if (roots.contains_key(package.manifest.name.as_str())) {
            root_kinds.insert(package.manifest.name.clone(), package.manifest.artifact_kind);
        }
    }

    auto selected_roots = Vec<String>::make();
    if (selection.packages.is_empty()) {
        for (const auto& name : graph.root_names) {
            auto kind      = root_kinds.get(name.as_str());
            auto supported = true;
            if (target != nullptr) {
                for (const auto& package : graph.packages) {
                    if (package.manifest.name.as_str() == name.as_str()) {
                        supported = package.manifest.target.matches(*target);
                        break;
                    }
                }
            }
            if (kind.is_some() && selected_by_purpose(**kind, purpose) && supported) {
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
                    "workspace package '{}' was selected more than once", name.as_str()));
            }
            if (! roots.contains_key(name.as_str())) {
                return failure<ResolvedPackageSelection>(
                    rstd::format("workspace has no member package named '{}'", name.as_str()));
            }
            auto kind = root_kinds.get(name.as_str());
            if (purpose == PackageSelectionPurpose::Test &&
                (kind.is_none() || ! selected_by_purpose(**kind, purpose))) {
                return failure<ResolvedPackageSelection>(
                    rstd::format("workspace package '{}' is not a test package", name.as_str()));
            }
            if (target != nullptr) {
                for (const auto& package : graph.packages) {
                    if (package.manifest.name.as_str() == name.as_str() &&
                        ! package.manifest.target.matches(*target)) {
                        return failure<ResolvedPackageSelection>(
                            rstd::format("package '{}' does not support target '{}'",
                                         name.as_str(),
                                         target->triple.as_str()));
                    }
                }
            }
            selected_names.insert(name.clone(), empty {});
            selected_roots.push(name.clone());
        }
    }
    if (selected_roots.is_empty()) {
        return failure<ResolvedPackageSelection>(purpose == PackageSelectionPurpose::Test
                                                     ? "workspace has no selected test package"_str
                                                     : "workspace has no selected package"_str);
    }
    rstd::slice_::sort_unstable(selected_roots.as_mut_slice().as_mut_ref());

    auto selected_packages = selected_closure(graph, selected_roots, target);
    if (selected_packages.is_err()) {
        return Err(rstd::move(selected_packages).unwrap_err());
    }
    return Ok(ResolvedPackageSelection {
        .graph                  = rstd::move(graph),
        .selected_root_names    = rstd::move(selected_roots),
        .selected_package_names = rstd::move(selected_packages).unwrap(),
    });
}

} // namespace tenon
