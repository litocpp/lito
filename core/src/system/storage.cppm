export module lito.system.storage;

import rstd;
import lito.error;
export import lito.system.error_contract;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito
{

auto lito_cache_directory(ref<rstd::path::Path> relative, ref<str> purpose)
    -> SystemResult<PathBuf> {
    auto configured = rstd::env::var("XDG_CACHE_HOME"_str);
    auto root       = PathBuf::make();
    if (configured.is_some() && ! configured->is_empty()) {
        root = PathBuf::from(rstd::move(configured).unwrap());
        if (! root.as_path().is_absolute()) {
            return Err(SystemError::Storage(
                String::make("XDG_CACHE_HOME must be an absolute path"_str)));
        }
    } else {
        auto home = rstd::env::var("HOME"_str);
        if (home.is_none() || home->is_empty()) {
            return Err(SystemError::Storage(
                rstd::format("{} require XDG_CACHE_HOME or HOME", purpose)));
        }
        root = PathBuf::from(rstd::move(home).unwrap());
        if (! root.as_path().is_absolute()) {
            return Err(
                SystemError::Storage(String::make("HOME must be an absolute path"_str)));
        }
        root.push(PathBuf::from(".cache"_str).as_path());
    }
    root.push(PathBuf::from("lito"_str).as_path());
    return Ok(root.join(relative));
}

} // namespace lito
