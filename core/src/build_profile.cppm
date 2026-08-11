module;
#include <rstd/enum.hpp>
#include <rstd/macro.hpp>

export module lito.build_profile;

import rstd;
import lito.model;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito
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

} // namespace lito

namespace lito
{

template<typename T>
auto failure(String message) -> Result<T> {
    return Err(Error::make(ErrorKind::InvalidRequest, rstd::move(message)));
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
    -> Result<ResolvedBuildProfile> {
    auto cycle = inherited_cycle(path, name);
    if (cycle.is_some()) return failure<ResolvedBuildProfile>(rstd::move(cycle).unwrap());
    path.push(String::make(name));

    auto declared = definition(project, name);
    if (name == "debug"_str || name == "release"_str) {
        auto profile = name == "debug"_str
                           ? ResolvedBuildProfile {
                                 .name         = BuildProfileName {
                                     .value = String::make("debug"_str),
                                 },
                                 .family       = BuildProfileFamily::Debug,
                                 .optimization = CppOptimization::None,
                                 .debug_info   = CppDebugInfo::Full,
                                 .strip        = StripMode::None,
                                 .lto          = CppLto::Off,
                                 .ndebug       = false,
                             }
                           : ResolvedBuildProfile {
                                 .name         = BuildProfileName {
                                     .value = String::make("release"_str),
                                 },
                                 .family       = BuildProfileFamily::Release,
                                 .optimization = CppOptimization::Level3,
                                 .debug_info   = CppDebugInfo::None,
                                 .strip        = StripMode::None,
                                 .lto          = CppLto::Off,
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
        return failure<ResolvedBuildProfile>(rstd::move(message));
    }
    if ((**declared).inherits.is_none()) {
        return failure<ResolvedBuildProfile>(
            rstd::format("custom profile '{}' must declare inherits", name));
    }
    auto inherited =
        rstd_try(resolve_profile(project, (**declared).inherits->as_str(), rstd::move(path)));
    return Ok(apply_definition(rstd::move(inherited), **declared));
}

} // namespace lito

export namespace rstd
{

template<>
struct Impl<convert::TryFrom<ref<str>>, lito::BuildProfileName> {
    using Error = lito::Error;

    static auto try_from(ref<str> name) -> Result<lito::BuildProfileName, Error> {
        if (! lito::valid_build_profile_name(name)) {
            return Err(lito::Error::make(
                lito::ErrorKind::InvalidRequest,
                rstd::format("invalid profile '{}'; expected ASCII letters, digits, '-' or '_'",
                             name)));
        }
        return Ok(lito::BuildProfileName {
            .value = String::make(name),
        });
    }
};

} // namespace rstd

export namespace lito
{

auto parse_build_profile(ref<str> name) -> Result<BuildProfileName> {
    return rstd::try_from<BuildProfileName>(name);
}

auto build_profile_name(const BuildProfileName& profile) noexcept -> ref<str> {
    return profile.as_str();
}

auto validate_build_profiles(const ProjectProfile& project) -> Result<empty> {
    for (usize index {}; index < project.build_profiles.len(); ++index) {
        const auto& profile = project.build_profiles[index];
        if (! valid_build_profile_name(profile.name.as_str())) {
            return failure<empty>(
                rstd::format("invalid build profile name '{}'", profile.name.as_str()));
        }
        for (usize prior {}; prior < index; ++prior) {
            if (project.build_profiles[prior].name == profile.name) {
                return failure<empty>(rstd::format("build profile '{}' is declared more than once",
                                                   profile.name.as_str()));
            }
        }
        const auto builtin =
            profile.name.as_str() == "debug"_str || profile.name.as_str() == "release"_str;
        if (builtin && profile.inherits.is_some()) {
            return failure<empty>(rstd::format("built-in profile '{}' cannot declare inherits",
                                               profile.name.as_str()));
        }
        if (! builtin && profile.inherits.is_none()) {
            return failure<empty>(
                rstd::format("custom profile '{}' must declare inherits", profile.name.as_str()));
        }
        rstd_try(resolve_profile(project, profile.name.as_str(), Vec<String>::make()));
    }
    return Ok(empty {});
}

auto resolve_build_profile(const ProjectProfile& project, const BuildProfileName& name)
    -> Result<ResolvedBuildProfile> {
    rstd_try(validate_build_profiles(project));
    return resolve_profile(project, name.as_str(), Vec<String>::make());
}

auto is_profile_owned_definition(ref<str> definition) -> bool {
    return definition == "NDEBUG"_str || definition.starts_with("NDEBUG="_str);
}

auto is_profile_owned_linker_option(ref<str> option) -> bool {
    if (option == "-O"_str || option.starts_with("-O"_str) || option == "-g"_str ||
        option.starts_with("-g"_str) || option == "-flto"_str || option.starts_with("-flto="_str) ||
        option == "-fno-lto"_str || option == "-s"_str || option == "--strip-all"_str ||
        option == "--strip-debug"_str) {
        return true;
    }
    if (! option.starts_with("-Wl,"_str)) return false;
    return option.contains(",--strip-all"_str) || option.contains(",--strip-debug"_str) ||
           option.ends_with(",-s"_str) || option.contains(",-s,"_str);
}

auto make_profile_spec(const BuildConfiguration& configuration,
                       const ProjectProfile&     project_profile,
                       const BuildProfileName&   selected_profile,
                       const CppArgumentParser&  parser) -> Result<ProfileSpec> {
    auto selected = rstd_try(resolve_build_profile(project_profile, selected_profile));
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
                    setting == CppOwnedSetting::DebugInfo || setting == CppOwnedSetting::Lto) {
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
            RSTD_CASE(PositionIndependentCode, enabled) {
                static_cast<void>(enabled);
            }
            RSTD_CASE(Warning, value) {
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
    for (const auto& option : configuration.linker_options) {
        if (is_profile_owned_linker_option(option.as_str())) {
            return failure<ProfileSpec>(rstd::format(
                "build linker option '{}' overrides the selected profile", option.as_str()));
        }
    }

    auto layer = CppOptionLayer {};
    if (selected.ndebug) layer.definitions.push(String::make("NDEBUG"_str));
    layer.arguments = rstd::move(arguments);

    auto cpp        = rstd_try(make_cpp_options(configuration.language_standard.as_str(),
                                                configuration.standard_library,
                                                project_profile.exceptions,
                                                project_profile.rtti,
                                                selected.optimization,
                                                selected.debug_info,
                                                rstd::move(layer)),
                               [](String error) {
                            return Error::make(ErrorKind::InvalidRequest, rstd::move(error));
                               });
    cpp.codegen.lto = selected.lto;

    return Ok(ProfileSpec {
        .name   = selected.name.value.clone(),
        .family = selected.family,
        .bmi =
            BmiRequest {
                .representation   = configuration.bmi_mode,
                .source_embedding = configuration.bmi_source_embedding,
            },
        .cpp            = rstd::move(cpp),
        .strip          = selected.strip,
        .linker_options = configuration.linker_options.clone(),
    });
}

} // namespace lito
