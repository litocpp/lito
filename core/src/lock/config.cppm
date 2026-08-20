export module lito.core:lock.config;

import rstd;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;
using namespace rstd::literals;

export namespace lito::lock
{

struct LockConfig {
    PathBuf path;
};

auto resolve_lock_path(ref<rstd::path::Path> root, const LockConfig& config) -> PathBuf {
    if (! config.path.is_empty()) {
        if (config.path.as_path().is_absolute()) return config.path.clone();
        return PathBuf::from(root).join(config.path.as_path());
    }
    return PathBuf::from(root).join(PathBuf::from("lito.lock"_str).as_path());
}

} // namespace lito::lock
