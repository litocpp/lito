module;
#include <rstd/enum.hpp>

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

class BuildProfileError {
    RSTD_ENUM(BuildProfileError,
              (Cpp, (CppOptionError source;)),
              (Message, (String message;)))
};

template<typename T>
using BuildProfileResult = rstd::Result<T, BuildProfileError>;

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

export namespace rstd
{

template<>
struct Impl<convert::From<lito::CppOptionError>, lito::BuildProfileError> {
    static auto from(lito::CppOptionError error) -> lito::BuildProfileError {
        return lito::BuildProfileError::Cpp(rstd::move(error));
    }
};

template<>
struct Impl<fmt::Display, lito::BuildProfileError> : ImplBase<lito::BuildProfileError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        const auto& error = this->self();
        if (error.is_Cpp()) {
            return formatter.write_raw("build profile C++ options are invalid",
                                       sizeof("build profile C++ options are invalid") - 1);
        }
        return formatter.write_str(error.as_Message().message.as_str());
    }
};

template<>
struct Impl<fmt::Debug, lito::BuildProfileError> : ImplBase<lito::BuildProfileError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, lito::BuildProfileError> : ImplBase<lito::BuildProfileError> {
    auto source() const noexcept -> Option<error::ErrorRef> {
        const auto& error = this->self();
        if (! error.is_Cpp()) return None();
        return Some(dyn<error::Error>::from_ref(error.as_Cpp().source));
    }
};

}
