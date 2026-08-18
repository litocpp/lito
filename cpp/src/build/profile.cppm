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
import :usage;
import :c.compiler;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::cpp
{

struct ProfileSpec {
    String                             name;
    lito::manifest::BuildProfileFamily family { lito::manifest::BuildProfileFamily::Debug };
    BmiRequest                         bmi;
    lito::c::CCompileOptions           c;
    CppCompileOptions                  cpp;
    CppLinkRequirements                link_requirements;
    lito::manifest::StripMode          strip { lito::manifest::StripMode::None };
    Vec<String>                        linker_options;
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

auto profile_failure(String message) -> lito::manifest::BuildProfileResult<ProfileSpec> {
    return Err(lito::manifest::BuildProfileError::Message(rstd::move(message)));
}

} // namespace lito::cpp

export namespace lito::cpp
{

auto parse_build_arguments(const BuildConfiguration& configuration, const CppArgumentParser& parser)
    -> lito::manifest::BuildProfileResult<CppArgumentLayer> {
    return parser.parse(configuration.options, "build.options"_str)
        .map_err([](CppOptionError error) {
            return lito::manifest::BuildProfileError::Options(erase_error(rstd::move(error)));
        });
}

auto make_profile_spec(const BuildConfiguration&               configuration,
                       const lito::manifest::ProjectProfile&   project_profile,
                       const lito::manifest::BuildProfileName& selected_profile,
                       CppArgumentLayer                        arguments)
    -> lito::manifest::BuildProfileResult<ProfileSpec> {
    auto selected =
        rstd_try(lito::manifest::resolve_build_profile(project_profile, selected_profile));
    auto link_requirements = CppLinkRequirements {};
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
            RSTD_CASE(Common, value) {
                if (value.is_Threading() &&
                    value.as_Threading().model == lito::compiler::ThreadingModel::Posix) {
                    link_requirements.posix_threads = true;
                    link_requirements.thread_sources.push(occurrence.source.clone());
                }
            }
            RSTD_CASE(Family, domain, family, value) {
                static_cast<void>(domain);
                static_cast<void>(family);
                static_cast<void>(value);
            }
            RSTD_CASE(Instrumentation, value) {
                static_cast<void>(value);
            }
            RSTD_CASE(SymbolVisibility, value) {
                static_cast<void>(value);
            }
            RSTD_CASE(TypeVisibility, value) {
                static_cast<void>(value);
            }
            RSTD_CASE(InlineVisibilityHidden, value) {
                static_cast<void>(value);
            }
            RSTD_CASE(SizedDeallocation, value) {
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
        if (option.as_str() == "-pthread"_str) {
            return profile_failure(String::make(
                "build linker option '-pthread' must be declared in build.options"_str));
        }
        if (option.as_str() == "-ldl"_str) {
            return profile_failure(String::make(
                "build linker option '-ldl' must be declared as usage.system-libraries"_str));
        }
        if (option.as_str() == "-nostdlib++"_str || option.as_str().starts_with("-stdlib="_str))
            return profile_failure(rstd::format(
                "build linker option '{}' overrides a Lito-owned setting", option.as_str()));
        if (is_profile_owned_linker_option(option.as_str()))
            return profile_failure(rstd::format(
                "build linker option '{}' overrides the selected profile", option.as_str()));
    }
    auto c_layer = lito::c::CArgumentLayer {};
    if (selected.ndebug) c_layer.definitions.push(String::make("NDEBUG"_str));
    for (const auto& occurrence : arguments.occurrences) {
        if (! occurrence.argument.is_Common()) continue;
        c_layer.occurrences.push(lito::c::CCompilerArgumentOccurrence {
            .argument = lito::c::CCompilerArgument::Common(
                as<Clone>(occurrence.argument.as_Common().argument).clone()),
            .raw_tokens = as<Clone>(occurrence.raw_tokens).clone(),
            .range      = occurrence.range,
            .source     = occurrence.source.clone(),
        });
    }
    auto c_common = lito::compiler::CommonCompileOptions {
        .codegen =
            lito::compiler::CodegenOptions {
                .optimization = selected.optimization,
                .debug_info   = selected.debug_info,
                .lto          = selected.lto,
            },
    };
    auto c =
        lito::c::apply_c_option_layer(lito::c::make_c_options(rstd::move(c_common),
                                                              configuration.c_standard.is_some()
                                                                  ? *configuration.c_standard
                                                                  : lito::manifest::CStandard::C99),
                                      rstd::move(c_layer));

    auto cpp_layer = CppOptionLayer {};
    if (selected.ndebug) cpp_layer.definitions.push(String::make("NDEBUG"_str));
    cpp_layer.arguments = rstd::move(arguments);
    auto cpp_result     = make_cpp_options(configuration.language_standard.as_str(),
                                           configuration.standard_library,
                                           project_profile.exceptions,
                                           project_profile.rtti,
                                           selected.optimization,
                                           selected.debug_info,
                                           rstd::move(cpp_layer));
    if (cpp_result.is_err()) {
        return Err(lito::manifest::BuildProfileError::Options(
            erase_error(rstd::move(cpp_result).unwrap_err())));
    }
    auto cpp               = rstd::move(cpp_result).unwrap();
    cpp.common.codegen.lto = selected.lto;
    return Ok(ProfileSpec {
        .name              = selected.name.value.clone(),
        .family            = selected.family,
        .bmi               = BmiRequest { .representation   = configuration.bmi_mode,
                                          .source_embedding = configuration.bmi_source_embedding },
        .c                 = rstd::move(c),
        .cpp               = rstd::move(cpp),
        .link_requirements = rstd::move(link_requirements),
        .strip             = selected.strip,
        .linker_options    = configuration.linker_options.clone(),
    });
}

auto make_profile_spec(const BuildConfiguration&               configuration,
                       const lito::manifest::ProjectProfile&   project_profile,
                       const lito::manifest::BuildProfileName& selected_profile,
                       const CppArgumentParser&                parser)
    -> lito::manifest::BuildProfileResult<ProfileSpec> {
    auto arguments = rstd_try(parse_build_arguments(configuration, parser));
    return make_profile_spec(
        configuration, project_profile, selected_profile, rstd::move(arguments));
}

} // namespace lito::cpp
