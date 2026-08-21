export module lito.core:manifest.build_script;

import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::manifest
{

enum class ScriptHostKind
{
    Build,
    Install,
};

struct ScriptPackageManifest {
    Vec<ScriptHostKind> supports;
};

auto script_host_kind_name(ScriptHostKind kind) noexcept -> ref<str>;
auto script_require_name(ref<str> package) -> String;

} // namespace lito::manifest

auto lito::manifest::script_host_kind_name(ScriptHostKind kind) noexcept -> ref<str> {
    return kind == ScriptHostKind::Build ? "build"_str : "install"_str;
}

auto lito::manifest::script_require_name(ref<str> package) -> String {
    auto result = String::make("@"_str);
    for (auto byte : package) result.push_ascii(byte == u8('-') ? '.' : char(byte.to_primitive()));
    return result;
}
