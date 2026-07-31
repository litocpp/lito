export module tenon.workspace_resolver;

import rstd;
import tenon.model;
import tenon.manifest;
import tenon.package;

using namespace rstd::prelude;
using namespace rstd::literals;
using StringMap = rstd::collections::BTreeMap<String, String>;
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

auto selected_closure(const ResolvedPackageGraph& graph, const Vec<String>& selected_roots)
    -> Result<Vec<String>> {
    auto indices = IndexMap::make();
    for (usize index {}; index < graph.packages.len(); ++index) {
        indices.insert(graph.packages[index].id.clone(), index);
    }

    auto pending  = copy_strings(selected_roots);
    auto selected = StringSet::make();
    while (! pending.is_empty()) {
        auto current = rstd::move(pending.pop()).unwrap();
        if (selected.contains_key(current.as_str())) continue;
        auto index = indices.get(current.as_str());
        if (index.is_none()) {
            return failure<Vec<String>>(rstd::format(
                "selected package id '{}' is missing from resolved graph", current.as_str()));
        }
        selected.insert(current.clone(), empty {});
        for (const auto& dependency : graph.packages[**index].dependencies) {
            pending.push(dependency.package_id.clone());
        }
    }

    auto result = Vec<String>::make();
    for (const auto& package : graph.packages) {
        if (selected.contains_key(package.id.as_str())) result.push(package.id.clone());
    }
    return Ok(rstd::move(result));
}

} // namespace tenon

export namespace tenon
{

auto resolve_package_selection(const PackageSelection&  selection,
                               PackageResolutionOptions options = {})
    -> Result<ResolvedPackageSelection> {
    auto resolved = resolve_package_graph(selection.root.as_path(), rstd::move(options));
    if (resolved.is_err()) return Err(rstd::move(resolved).unwrap_err());
    auto graph = rstd::move(resolved).unwrap();
    if (! graph.root_is_workspace && ! selection.packages.is_empty()) {
        return failure<ResolvedPackageSelection>("--package requires a workspace directory"_str);
    }

    auto roots = StringSet::make();
    for (const auto& id : graph.root_ids) roots.insert(id.clone(), empty {});
    auto names = StringMap::make();
    for (const auto& package : graph.packages) {
        if (! roots.contains_key(package.id.as_str())) continue;
        if (names.contains_key(package.manifest.name.as_str())) {
            return failure<ResolvedPackageSelection>(
                rstd::format("workspace contains more than one package named '{}'",
                             package.manifest.name.as_str()));
        }
        names.insert(package.manifest.name.clone(), package.id.clone());
    }

    auto selected_roots = Vec<String>::make();
    if (selection.packages.is_empty()) {
        selected_roots = copy_strings(graph.root_ids);
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
            auto id = names.get(name.as_str());
            if (id.is_none()) {
                return failure<ResolvedPackageSelection>(
                    rstd::format("workspace has no member package named '{}'", name.as_str()));
            }
            selected_names.insert(name.clone(), empty {});
            selected_roots.push((**id).clone());
        }
    }
    rstd::slice_::sort_unstable(selected_roots.as_mut_slice().as_mut_ref());

    auto selected_packages = selected_closure(graph, selected_roots);
    if (selected_packages.is_err()) {
        return Err(rstd::move(selected_packages).unwrap_err());
    }
    return Ok(ResolvedPackageSelection {
        .graph                = rstd::move(graph),
        .selected_root_ids    = rstd::move(selected_roots),
        .selected_package_ids = rstd::move(selected_packages).unwrap(),
    });
}

} // namespace tenon
