export module lito.driver:install.path;

import rstd;
import lito.core;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito
{

auto install_relative_destination_is_valid(ref<rstd::path::Path> path) -> bool {
    if (path.is_empty() || path.is_absolute() || path.has_root()) return false;
    auto components = path.components();
    auto first      = true;
    for (auto component : components) {
        if (! component.is_normal()) return false;
        if (first && component.as_os_str().to_str() == Some(".lito"_str)) return false;
        first = false;
    }
    return ! first;
}

auto install_path_is_under_bin(ref<rstd::path::Path> path) -> bool {
    auto components = path.components();
    auto first      = components.next();
    return first.is_some() && first->is_normal() && first->as_os_str().to_str() == Some("bin"_str);
}

using InstallRuntimeSearchPathResult = Result<lito::artifact::OriginRelativeRuntimePath, String>;

auto install_runtime_search_path(ref<rstd::path::Path> artifact_destination,
                                 ref<rstd::path::Path> asset_destination)
    -> InstallRuntimeSearchPathResult {
    if (! install_relative_destination_is_valid(artifact_destination)) {
        return Err(
            rstd::format("install artifact destination '{}' is unsafe", artifact_destination));
    }
    if (! install_relative_destination_is_valid(asset_destination)) {
        return Err(rstd::format("install asset destination '{}' is unsafe", asset_destination));
    }
    auto artifact_parent = PathBuf::make();
    auto parent          = artifact_destination.parent();
    if (parent.is_some()) artifact_parent = PathBuf::from(*parent);
    auto relative = rstd::path::lexically_relative(artifact_parent.as_path(), asset_destination);
    if (relative.is_none()) {
        return Err(rstd::format("install artifact destination '{}' and asset destination '{}' "
                                "have no lexical relation",
                                artifact_destination,
                                asset_destination));
    }
    return lito::artifact::make_origin_relative_runtime_path(rstd::move(relative).unwrap());
}

} // namespace lito
