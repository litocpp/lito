module;
#include <rstd/enum.hpp>
#include <rstd/macro.hpp>

export module lito.cpp:build.profile;

import rstd;
import :build.configuration;
import :bmi.artifact;
import :compiler.option;
import :compiler.parser;
import :compiler.policy;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::cpp
{

struct ProfileSpec {
    String             name;
    BuildProfileFamily family { BuildProfileFamily::Debug };
    BmiRequest         bmi;
    CppCompileOptions  cpp;
    StripMode          strip { StripMode::None };
    Vec<String>        linker_options;
};

} // namespace lito::cpp

export namespace lito::cpp
{

auto is_profile_owned_definition(ref<str> definition) -> bool {
    return definition == "NDEBUG"_str || definition.starts_with("NDEBUG="_str);
}

auto is_profile_owned_linker_option(ref<str> option) -> bool {
    if (option == "-O"_str || option.starts_with("-O"_str) || option == "-g"_str ||
        option.starts_with("-g"_str) || option == "-flto"_str || option.starts_with("-flto="_str) ||
        option == "-fno-lto"_str || option == "-s"_str || option == "--strip-all"_str ||
        option == "--strip-debug"_str)
        return true;
    if (! option.starts_with("-Wl,"_str)) return false;
    return option.contains(",--strip-all"_str) || option.contains(",--strip-debug"_str) ||
           option.ends_with(",-s"_str) || option.contains(",-s,"_str);
}

} // namespace lito::cpp

namespace lito::cpp
{

auto profile_failure(String message) -> BuildProfileResult<ProfileSpec> {
    return Err(BuildProfileError::Message(rstd::move(message)));
}

} // namespace lito::cpp

export namespace lito::cpp
{

auto parse_build_arguments(const BuildConfiguration& configuration, const CppArgumentParser& parser)
    -> BuildProfileResult<CppArgumentLayer> {
    return parser.parse(configuration.options, "build.options"_str)
        .map_err([](CppOptionError error) {
            return BuildProfileError::Options(erase_error(rstd::move(error)));
        });
}

auto make_profile_spec(const BuildConfiguration& configuration,
                       const ProjectProfile&     project_profile,
                       const BuildProfileName&   selected_profile,
                       CppArgumentLayer          arguments) -> BuildProfileResult<ProfileSpec> {
    auto selected = rstd_try(resolve_build_profile(project_profile, selected_profile));
    for (const auto& occurrence : arguments.occurrences) {
        auto option = occurrence.raw_tokens[usize {}].as_str();
        RSTD_MATCH(occurrence.argument) {
            RSTD_CASE(Macro, directive) {
                if (is_profile_owned_definition(directive.value.as_str()))
                    return profile_failure(
                        rstd::format("build option '{}' overrides the selected profile", option));
            }
            RSTD_CASE(OwnedSetting, setting) {
                if (setting == CppOwnedSetting::Optimization ||
                    setting == CppOwnedSetting::DebugInfo || setting == CppOwnedSetting::Lto)
                    return profile_failure(
                        rstd::format("build option '{}' overrides the selected profile", option));
            }
            RSTD_CASE(IncludeDirectory, value) {
                static_cast<void>(value);
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
            RSTD_CASE(PositionIndependentCode, value) {
                static_cast<void>(value);
            }
            RSTD_CASE(SizedDeallocation, value) {
                static_cast<void>(value);
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
        if (option.as_str() == "-nostdlib++"_str || option.as_str().starts_with("-stdlib="_str))
            return profile_failure(rstd::format(
                "build linker option '{}' overrides a Lito-owned setting", option.as_str()));
        if (is_profile_owned_linker_option(option.as_str()))
            return profile_failure(rstd::format(
                "build linker option '{}' overrides the selected profile", option.as_str()));
    }
    auto layer = CppOptionLayer {};
    if (selected.ndebug) layer.definitions.push(String::make("NDEBUG"_str));
    layer.arguments = rstd::move(arguments);
    auto cpp_result = make_cpp_options(configuration.language_standard.as_str(),
                                       configuration.standard_library,
                                       project_profile.exceptions,
                                       project_profile.rtti,
                                       selected.optimization,
                                       selected.debug_info,
                                       rstd::move(layer));
    if (cpp_result.is_err()) {
        return Err(BuildProfileError::Options(erase_error(rstd::move(cpp_result).unwrap_err())));
    }
    auto cpp        = rstd::move(cpp_result).unwrap();
    cpp.codegen.lto = selected.lto;
    return Ok(ProfileSpec {
        .name           = selected.name.value.clone(),
        .family         = selected.family,
        .bmi            = BmiRequest { .representation   = configuration.bmi_mode,
                                       .source_embedding = configuration.bmi_source_embedding },
        .cpp            = rstd::move(cpp),
        .strip          = selected.strip,
        .linker_options = configuration.linker_options.clone(),
    });
}

auto make_profile_spec(const BuildConfiguration& configuration,
                       const ProjectProfile&     project_profile,
                       const BuildProfileName&   selected_profile,
                       const CppArgumentParser&  parser) -> BuildProfileResult<ProfileSpec> {
    auto arguments = rstd_try(parse_build_arguments(configuration, parser));
    return make_profile_spec(
        configuration, project_profile, selected_profile, rstd::move(arguments));
}

} // namespace lito::cpp
