export module lito.tools.cargo:request;

import rstd;
import lito.core;

using namespace rstd::prelude;
using namespace rstd::literals;
using PathBuf = rstd::path::PathBuf;

export namespace lito::tools::cargo
{

struct Configuration {
    bool offline { false };
};

struct Provider {
    PathBuf executable;
    String  identity;
    String  host_target;

    auto clone() const -> Provider {
        return Provider {
            .executable  = executable.clone(),
            .identity    = identity.clone(),
            .host_target = host_target.clone(),
        };
    }
};

struct MetadataRequest {
    PathBuf source_root;
    PathBuf manifest;
    String  package;
    bool    offline { false };
};

enum class ProfileOptimization
{
    None,
    Level1,
    Level2,
    Level3,
    Size,
    SizeMin,
};

enum class ProfileDebugInfo
{
    None,
    Limited,
    Full,
    LineDirectivesOnly,
    LineTablesOnly,
};

enum class ProfileLto
{
    Off,
    Thin,
    Fat,
};

enum class ProfileStrip
{
    None,
    DebugInfo,
    Symbols,
};

struct ProfileConfiguration {
    lito::dependency::CargoProfileName selected;
    lito::dependency::CargoProfileName inherits;
    Option<ProfileOptimization>        optimization;
    Option<ProfileDebugInfo>           debug_info;
    Option<ProfileLto>                 lto;
    Option<bool>                       debug_assertions;
    Option<ProfileStrip>               strip;
};

constexpr auto profile_optimization_name(ProfileOptimization value) noexcept -> ref<str> {
    switch (value) {
    case ProfileOptimization::None: return "0"_str;
    case ProfileOptimization::Level1: return "1"_str;
    case ProfileOptimization::Level2: return "2"_str;
    case ProfileOptimization::Level3: return "3"_str;
    case ProfileOptimization::Size: return "s"_str;
    case ProfileOptimization::SizeMin: return "z"_str;
    }
    __builtin_unreachable();
}

constexpr auto profile_debug_info_name(ProfileDebugInfo value) noexcept -> ref<str> {
    switch (value) {
    case ProfileDebugInfo::None: return "none"_str;
    case ProfileDebugInfo::Limited: return "limited"_str;
    case ProfileDebugInfo::Full: return "full"_str;
    case ProfileDebugInfo::LineDirectivesOnly: return "line-directives-only"_str;
    case ProfileDebugInfo::LineTablesOnly: return "line-tables-only"_str;
    }
    __builtin_unreachable();
}

constexpr auto profile_lto_name(ProfileLto value) noexcept -> ref<str> {
    switch (value) {
    case ProfileLto::Off: return "off"_str;
    case ProfileLto::Thin: return "thin"_str;
    case ProfileLto::Fat: return "fat"_str;
    }
    __builtin_unreachable();
}

constexpr auto profile_strip_name(ProfileStrip value) noexcept -> ref<str> {
    switch (value) {
    case ProfileStrip::None: return "none"_str;
    case ProfileStrip::DebugInfo: return "debuginfo"_str;
    case ProfileStrip::Symbols: return "symbols"_str;
    }
    __builtin_unreachable();
}

auto profile_configuration_identity(const ProfileConfiguration& profile) -> String {
    auto result = String::make("lito-cargo-profile-v1\n"_str);
    result.push_str("inherits="_str);
    result.push_str(profile.inherits.as_str());
    result.push_ascii('\n');
    const auto append = [&result](ref<str> name, Option<ref<str>> value) {
        result.push_str(name);
        result.push_ascii('=');
        result.push_str(value.is_some() ? *value : "unspecified"_str);
        result.push_ascii('\n');
    };
    append("optimization"_str,
           profile.optimization.is_some() ? Some(profile_optimization_name(*profile.optimization))
                                          : None());
    append("debug-info"_str,
           profile.debug_info.is_some() ? Some(profile_debug_info_name(*profile.debug_info))
                                        : None());
    append("lto"_str, profile.lto.is_some() ? Some(profile_lto_name(*profile.lto)) : None());
    append("debug-assertions"_str,
           profile.debug_assertions.is_some()
               ? Some(*profile.debug_assertions ? "true"_str : "false"_str)
               : None());
    append("strip"_str,
           profile.strip.is_some() ? Some(profile_strip_name(*profile.strip)) : None());
    return result;
}

auto profile_configuration_summary(const ProfileConfiguration& profile) -> String {
    auto result = rstd::format(
        "optimization={}, debug={}, LTO={}, debug-assertions={}, strip={}",
        profile.optimization.is_some() ? profile_optimization_name(*profile.optimization)
                                       : "inherited"_str,
        profile.debug_info.is_some() ? profile_debug_info_name(*profile.debug_info)
                                     : "inherited"_str,
        profile.lto.is_some() ? profile_lto_name(*profile.lto) : "inherited"_str,
        profile.debug_assertions.is_some() ? (*profile.debug_assertions ? "true"_str : "false"_str)
                                           : "inherited"_str,
        profile.strip.is_some() ? profile_strip_name(*profile.strip) : "inherited"_str);
    return result;
}

struct BuildRequest {
    String               alias;
    PathBuf              source_root;
    PathBuf              manifest;
    String               package;
    Vec<String>          features;
    bool                 default_features { true };
    ProfileConfiguration profile;
    String               target;
    String               request_identity;
    PathBuf              work_root;
    PathBuf              target_directory;
    usize                jobs { usize(1) };
    bool                 offline { false };
};

enum class EventKind
{
    Metadata,
    Build,
    Reuse,
};

struct Event {
    EventKind             kind { EventKind::Metadata };
    ref<str>              alias;
    ref<rstd::path::Path> path;
    rstd::time::Duration  elapsed;
    bool                  completed { false };
};

struct EventSink {
    void* context {};
    void (*notify)(void*, const Event&) noexcept {};
};

} // namespace lito::tools::cargo
