module;
#include <rstd/enum.hpp>
#include <rstd/macro.hpp>

export module lito.build_profile;

import rstd;
import lito.model;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace lito
{

template<typename T>
auto failure(String message) -> Result<T> {
    return Err(Error::make(ErrorKind::InvalidRequest, rstd::move(message)));
}

} // namespace lito

export namespace rstd
{

template<>
struct Impl<convert::TryFrom<ref<str>>, lito::BuildProfile> {
    using Error = lito::Error;

    static auto try_from(ref<str> name) -> Result<lito::BuildProfile, Error> {
        if (name == "debug"_str) return Ok(lito::BuildProfile::Debug);
        if (name == "release"_str) return Ok(lito::BuildProfile::Release);
        return Err(lito::Error::make(
            lito::ErrorKind::InvalidRequest,
            rstd::format("unknown profile '{}'; expected debug or release", name)));
    }
};

} // namespace rstd

export namespace lito
{

auto build_profile_name(BuildProfile profile) -> ref<str> {
    switch (profile) {
    case BuildProfile::Debug: return "debug"_str;
    case BuildProfile::Release: return "release"_str;
    }
    return "debug"_str;
}

auto parse_build_profile(ref<str> name) -> Result<BuildProfile> {
    return rstd::try_from<BuildProfile>(name);
}

auto is_profile_owned_definition(ref<str> definition) -> bool {
    return definition == "NDEBUG"_str || definition.starts_with("NDEBUG="_str);
}

auto make_profile_spec(const BuildConfiguration& configuration, const CppArgumentParser& parser)
    -> Result<ProfileSpec> {
    auto arguments =
        rstd_try(parser.parse(configuration.options, "build.options"_str), [](String error) {
            return Error::make(ErrorKind::InvalidRequest, rstd::move(error));
        });
    for (const auto& occurrence : arguments.occurrences) {
        auto option = occurrence.raw_tokens[usize {}].as_str();
        RSTD_MATCH(occurrence.argument) {
            RSTD_CASE(Macro, directive) {
                if (is_profile_owned_definition(directive.value.as_str())) {
                    return failure<ProfileSpec>(
                        rstd::format("build option '{}' overrides the selected profile", option));
                }
            }
            RSTD_CASE(OwnedSetting, setting) {
                if (setting == CppOwnedSetting::Optimization ||
                    setting == CppOwnedSetting::DebugInfo) {
                    return failure<ProfileSpec>(
                        rstd::format("build option '{}' overrides the selected profile", option));
                }
            }
            RSTD_CASE(IncludeDirectory, path) {
                static_cast<void>(path);
            }
            RSTD_CASE(Target, value) {
                static_cast<void>(value);
            }
            RSTD_CASE(Sysroot, value) {
                static_cast<void>(value);
            }
            RSTD_CASE(Family, domain, family, value) {
                static_cast<void>(domain);
                static_cast<void>(family);
                static_cast<void>(value);
            }
            RSTD_CASE(Instrumentation, value) {
                static_cast<void>(value);
            }
            RSTD_CASE(Diagnostic, value) {
                static_cast<void>(value);
            }
            RSTD_CASE(Vendor, value) {
                static_cast<void>(value);
            }
        }
    }

    auto optimization = CppOptimization::Default;
    auto debug_info   = CppDebugInfo::None;
    auto layer        = CppOptionLayer {};
    switch (configuration.profile) {
    case BuildProfile::Debug:
        optimization = CppOptimization::None;
        debug_info   = CppDebugInfo::Full;
        break;
    case BuildProfile::Release:
        optimization = CppOptimization::Level3;
        layer.definitions.push(String::make("NDEBUG"_str));
        break;
    }
    layer.arguments = rstd::move(arguments);

    auto cpp = rstd_try(make_cpp_options(configuration.language_standard.as_str(),
                                         configuration.standard_library,
                                         configuration.exceptions,
                                         configuration.rtti,
                                         optimization,
                                         debug_info,
                                         rstd::move(layer)),
                        [](String error) {
                            return Error::make(ErrorKind::InvalidRequest, rstd::move(error));
                        });

    return Ok(ProfileSpec {
        .name = String::make(build_profile_name(configuration.profile)),
        .bmi =
            BmiRequest {
                .representation   = configuration.bmi_mode,
                .source_embedding = configuration.bmi_source_embedding,
            },
        .cpp            = rstd::move(cpp),
        .linker_options = configuration.linker_options.clone(),
    });
}

} // namespace lito
