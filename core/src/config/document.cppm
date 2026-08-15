export module lito.core:config.document;

import rstd;
import :config.error;
import :config.project;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;

export namespace lito
{

struct ConfigQuery {
    PathBuf path;
    String  output;
};

struct ConfigMutation {
    PathBuf path;
    String  key;
};

auto project_config_path(ref<rstd::path::Path> root) -> ConfigResult<PathBuf>;

auto load_project_config(ref<rstd::path::Path> root, ProjectConfigRequest request)
    -> ConfigResult<ProjectConfig>;

auto load_project_config(ref<rstd::path::Path> root, ConfigLoadMode mode = ConfigLoadMode::Enabled)
    -> ConfigResult<ProjectConfig>;

auto get_persisted_config(ref<rstd::path::Path> root, Option<String> key)
    -> ConfigResult<ConfigQuery>;

auto set_persisted_config(ref<rstd::path::Path> root, ref<str> key, ref<str> value)
    -> ConfigResult<ConfigMutation>;

auto unset_persisted_config(ref<rstd::path::Path> root, ref<str> key)
    -> ConfigResult<ConfigMutation>;

} // namespace lito
