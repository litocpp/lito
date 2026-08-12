export module lito.build.profile_contract;

import rstd;
import lito.error;
import lito.cpp;
import lito.cpp.bmi;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito
{

struct BuildProfileName {
    String value { String::make("debug"_str) };

    auto as_str() const noexcept -> ref<str> { return value.as_str(); }

    auto clone() const -> BuildProfileName { return BuildProfileName { .value = value.clone() }; }

    auto operator==(const BuildProfileName& other) const noexcept -> bool {
        return value == other.value;
    }
};

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
    Option<CppOptimization>  optimization;
    Option<CppDebugInfo>     debug_info;
    Option<StripMode>        strip;
    Option<CppLto>           lto;

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
    CppOptimization    optimization { CppOptimization::None };
    CppDebugInfo       debug_info { CppDebugInfo::Full };
    StripMode          strip { StripMode::None };
    CppLto             lto { CppLto::Off };
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

struct ProfileSpec {
    String             name;
    BuildProfileFamily family { BuildProfileFamily::Debug };
    BmiRequest         bmi;
    CppCompileOptions  cpp;
    StripMode          strip { StripMode::None };
    Vec<String>        linker_options;
};

} // namespace lito
