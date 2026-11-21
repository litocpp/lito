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
    Pack,
};

struct ScriptPackageManifest {
    Vec<ScriptHostKind> supports;
};

auto script_host_kind_name(ScriptHostKind kind) noexcept -> ref<str>;
auto script_require_name(ref<str> package) -> String;

} // namespace lito::manifest

auto lito::manifest::script_host_kind_name(ScriptHostKind kind) noexcept -> ref<str> {
    switch (kind) {
    case ScriptHostKind::Build: return "build"_str;
    case ScriptHostKind::Install: return "install"_str;
    case ScriptHostKind::Pack: return "pack"_str;
    }
    __builtin_unreachable();
}

auto lito::manifest::script_require_name(ref<str> package) -> String {
    auto result = "@"_Str;
    for (auto byte : package) result.push_ascii(byte == u8('-') ? '.' : char(byte.to_primitive()));
    return result;
}
