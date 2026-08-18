module;
#include <rstd/enum.hpp>
#include <rstd/macro.hpp>

export module lito.core:manifest.profile;

import rstd;

using namespace rstd::prelude;
using ErrorBox = Box<dyn<rstd::error::Error>>;
using namespace rstd::literals;

export namespace lito::manifest
{

enum class Optimization
{
    Default,
    None,
    Level1,
    Level2,
    Level3,
    Level4,
    Debug,
    Size,
    SizeMin,
    Fast,
};

enum class DebugInfo
{
    None,
    LineDirectivesOnly,
    LineTablesOnly,
    Limited,
    Full,
};

enum class Lto
{
    Off,
    Thin,
    Fat,
};

struct BuildProfileName {
    String value { String::make("debug"_str) };

    auto as_str() const noexcept -> ref<str> { return value.as_str(); }

    auto clone() const -> BuildProfileName { return BuildProfileName { .value = value.clone() }; }

    auto operator==(const BuildProfileName& other) const noexcept -> bool {
        return value == other.value;
    }
};

class BuildProfileError {
    RSTD_ENUM(BuildProfileError, (Options, (ErrorBox source;)), (Message, (String message;)))
};

template<typename T>
using BuildProfileResult = Result<T, BuildProfileError>;

enum class BuildProfileFamily
{
    Debug,
    Release,
};

enum class StripMode
{
    None,
    DebugInfo,
    Symbols,
};

struct BuildProfileDefinition {
    BuildProfileName         name;
    Option<BuildProfileName> inherits;
    Option<Optimization>     optimization;
    Option<DebugInfo>        debug_info;
    Option<StripMode>        strip;
    Option<Lto>              lto;

    auto clone() const -> BuildProfileDefinition {
        auto result = BuildProfileDefinition {
            .name         = name.clone(),
            .optimization = optimization,
            .debug_info   = debug_info,
            .strip        = strip,
            .lto          = lto,
        };
        if (inherits.is_some()) result.inherits = Some(inherits->clone());
        return result;
    }
};

struct ResolvedBuildProfile {
    BuildProfileName   name;
    BuildProfileFamily family { BuildProfileFamily::Debug };
    Optimization       optimization { Optimization::None };
    DebugInfo          debug_info { DebugInfo::Full };
    StripMode          strip { StripMode::None };
    Lto                lto { Lto::Off };
    bool               ndebug { false };
};

struct ProjectProfile {
    bool                        exceptions { true };
    bool                        rtti { true };
    Vec<BuildProfileDefinition> build_profiles;

    auto clone() const -> ProjectProfile {
        auto profiles = Vec<BuildProfileDefinition>::with_capacity(build_profiles.len());
        for (const auto& profile : build_profiles) profiles.push(profile.clone());
        return ProjectProfile {
            .exceptions     = exceptions,
            .rtti           = rtti,
            .build_profiles = rstd::move(profiles),
        };
    }
};

} // namespace lito::manifest

export namespace rstd
{

template<>
struct Impl<fmt::Display, lito::manifest::BuildProfileError>
    : ImplBase<lito::manifest::BuildProfileError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        const auto& error = this->self();
        if (error.is_Options()) {
            return formatter.write_raw("build profile options are invalid",
                                       sizeof("build profile options are invalid") - 1);
        }
        return formatter.write_str(error.as_Message().message.as_str());
    }
};

template<>
struct Impl<fmt::Debug, lito::manifest::BuildProfileError>
    : ImplBase<lito::manifest::BuildProfileError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, lito::manifest::BuildProfileError>
    : ImplBase<lito::manifest::BuildProfileError> {
    auto source() const noexcept -> Option<error::ErrorRef> {
        const auto& error = this->self();
        if (! error.is_Options()) return None();
        return Some(error.as_Options().source.as_ref());
    }
};

} // namespace rstd

export namespace lito::manifest
{

auto valid_build_profile_name(ref<str> value) noexcept -> bool {
    if (value.is_empty() || value == "exceptions"_str || value == "rtti"_str) return false;
    for (const auto character : value) {
        const auto ascii = character.to_primitive();
        if (! ((ascii >= 'a' && ascii <= 'z') || (ascii >= 'A' && ascii <= 'Z') ||
               (ascii >= '0' && ascii <= '9') || ascii == '-' || ascii == '_')) {
            return false;
        }
    }
    return true;
}

} // namespace lito::manifest

using namespace lito::manifest;

template<typename T>
auto build_profile_failure(String message) -> BuildProfileResult<T> {
    return Err(BuildProfileError::Message(rstd::move(message)));
}

auto definition(const ProjectProfile& project, ref<str> name)
    -> Option<ref<BuildProfileDefinition>> {
    for (const auto& candidate : project.build_profiles) {
        if (candidate.name.as_str() == name) {
            return Some(ref<BuildProfileDefinition>::from_raw_parts(rstd::addressof(candidate)));
        }
    }
    return None();
}

auto inherited_cycle(const Vec<String>& path, ref<str> name) -> Option<String> {
    auto cycle = false;
    auto first = true;
    auto text  = String::make("build profile inheritance cycle: "_str);
    for (const auto& item : path) {
        if (item.as_str() == name) cycle = true;
        if (! cycle) continue;
        if (! first) text.push_str(" -> "_str);
        text.push_str(item.as_str());
        first = false;
    }
    if (! cycle) return None();
    text.push_str(" -> "_str);
    text.push_str(name);
    return Some(rstd::move(text));
}

auto apply_definition(ResolvedBuildProfile profile, const BuildProfileDefinition& value)
    -> ResolvedBuildProfile {
    profile.name = value.name.clone();
    if (value.optimization.is_some()) profile.optimization = *value.optimization;
    if (value.debug_info.is_some()) profile.debug_info = *value.debug_info;
    if (value.strip.is_some()) profile.strip = *value.strip;
    if (value.lto.is_some()) profile.lto = *value.lto;
    return profile;
}

auto resolve_profile(const ProjectProfile& project, ref<str> name, Vec<String> path)
    -> BuildProfileResult<ResolvedBuildProfile> {
    auto cycle = inherited_cycle(path, name);
    if (cycle.is_some())
        return build_profile_failure<ResolvedBuildProfile>(rstd::move(cycle).unwrap());
    path.push(String::make(name));

    auto declared = definition(project, name);
    if (name == "debug"_str || name == "release"_str) {
        auto profile = name == "debug"_str
                           ? ResolvedBuildProfile {
                                 .name         = BuildProfileName {
                                     .value = String::make("debug"_str),
                                 },
                                 .family       = BuildProfileFamily::Debug,
                                 .optimization = Optimization::None,
                                 .debug_info   = DebugInfo::Full,
                                 .strip        = StripMode::None,
                                 .lto          = Lto::Off,
                                 .ndebug       = false,
                             }
                           : ResolvedBuildProfile {
                                 .name         = BuildProfileName {
                                     .value = String::make("release"_str),
                                 },
                                 .family       = BuildProfileFamily::Release,
                                 .optimization = Optimization::Level3,
                                 .debug_info   = DebugInfo::None,
                                 .strip        = StripMode::None,
                                 .lto          = Lto::Off,
                                 .ndebug       = true,
                             };
        if (declared.is_some()) profile = apply_definition(rstd::move(profile), **declared);
        return Ok(rstd::move(profile));
    }

    if (declared.is_none()) {
        auto message =
            rstd::format("unknown profile '{}'; available profiles: debug, release", name);
        for (const auto& candidate : project.build_profiles) {
            if (candidate.name.as_str() == "debug"_str ||
                candidate.name.as_str() == "release"_str) {
                continue;
            }
            message.push_str(", "_str);
            message.push_str(candidate.name.as_str());
        }
        return build_profile_failure<ResolvedBuildProfile>(rstd::move(message));
    }
    if ((**declared).inherits.is_none()) {
        return build_profile_failure<ResolvedBuildProfile>(
            rstd::format("custom profile '{}' must declare inherits", name));
    }
    auto inherited =
        rstd_try(resolve_profile(project, (**declared).inherits->as_str(), rstd::move(path)));
    return Ok(apply_definition(rstd::move(inherited), **declared));
}

export namespace rstd
{

template<>
struct Impl<convert::TryFrom<ref<str>>, lito::manifest::BuildProfileName> {
    using Error = lito::manifest::BuildProfileError;

    static auto try_from(ref<str> name) -> Result<lito::manifest::BuildProfileName, Error> {
        if (! lito::manifest::valid_build_profile_name(name)) {
            return Err(lito::manifest::BuildProfileError::Message(rstd::format(
                "invalid profile '{}'; expected ASCII letters, digits, '-' or '_'", name)));
        }
        return Ok(lito::manifest::BuildProfileName {
            .value = String::make(name),
        });
    }
};

} // namespace rstd

export namespace lito::manifest
{

auto parse_build_profile(ref<str> name) -> BuildProfileResult<BuildProfileName> {
    return rstd::try_from<BuildProfileName>(name);
}

auto build_profile_name(const BuildProfileName& profile) noexcept -> ref<str> {
    return profile.as_str();
}

auto validate_build_profiles(const ProjectProfile& project) -> BuildProfileResult<empty> {
    for (usize index {}; index < project.build_profiles.len(); ++index) {
        const auto& profile = project.build_profiles[index];
        if (! valid_build_profile_name(profile.name.as_str())) {
            return build_profile_failure<empty>(
                rstd::format("invalid build profile name '{}'", profile.name.as_str()));
        }
        for (usize prior {}; prior < index; ++prior) {
            if (project.build_profiles[prior].name == profile.name) {
                return build_profile_failure<empty>(rstd::format(
                    "build profile '{}' is declared more than once", profile.name.as_str()));
            }
        }
        const auto builtin =
            profile.name.as_str() == "debug"_str || profile.name.as_str() == "release"_str;
        if (builtin && profile.inherits.is_some()) {
            return build_profile_failure<empty>(rstd::format(
                "built-in profile '{}' cannot declare inherits", profile.name.as_str()));
        }
        if (! builtin && profile.inherits.is_none()) {
            return build_profile_failure<empty>(
                rstd::format("custom profile '{}' must declare inherits", profile.name.as_str()));
        }
        rstd_try(resolve_profile(project, profile.name.as_str(), Vec<String>::make()));
    }
    return Ok(empty {});
}

auto resolve_build_profile(const ProjectProfile& project, const BuildProfileName& name)
    -> BuildProfileResult<ResolvedBuildProfile> {
    rstd_try(validate_build_profiles(project));
    return resolve_profile(project, name.as_str(), Vec<String>::make());
}

} // namespace lito::manifest
