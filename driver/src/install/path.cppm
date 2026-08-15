export module lito.driver:install.path;

import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito
{

auto install_relative_destination_is_valid(ref<rstd::path::Path> path) -> bool {
    if (path.is_empty() || path.is_absolute() || path.has_root()) return false;
    auto components = path.components();
    auto first      = true;
    for (auto component = components.next(); component.is_some(); component = components.next()) {
        if (! component->is_normal()) return false;
        if (first && component->as_os_str().to_str() == Some(".lito"_str)) return false;
        first = false;
    }
    return ! first;
}

auto install_path_is_under_bin(ref<rstd::path::Path> path) -> bool {
    auto components = path.components();
    auto first      = components.next();
    return first.is_some() && first->is_normal() && first->as_os_str().to_str() == Some("bin"_str);
}

} // namespace lito
