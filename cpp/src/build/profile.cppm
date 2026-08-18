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
import :link;
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
    lito::link::Requirements           c_link_requirements;
    lito::link::Requirements           cpp_link_requirements;
    lito::artifact::StripMode          strip { lito::artifact::StripMode::None };
    Vec<String>                        linker_options;
};

struct ParsedGlobalBuildOptions {
    CppArgumentLayer                    cpp;
    lito::c::CArgumentLayer             c;
    Vec<lito::config::BuildOptionInput> linker;
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
    -> lito::manifest::BuildProfileResult<ParsedGlobalBuildOptions> {
    auto result = ParsedGlobalBuildOptions {};
    for (const auto& input : configuration.global_options.cpp) {
        auto parsed = parser.parse(input.arguments, input.source.as_str());
        if (parsed.is_err()) {
            return Err(lito::manifest::BuildProfileError::Options(
                erase_error(rstd::move(parsed).unwrap_err())));
        }
        for (auto& occurrence : parsed->occurrences) {
            result.cpp.occurrences.push(rstd::move(occurrence));
        }
    }
    for (const auto& input : configuration.global_options.c) {
        auto parsed = parser.parse_c(input.arguments, input.source.as_str());
        if (parsed.is_err()) {
            return Err(lito::manifest::BuildProfileError::Options(
                erase_error(rstd::move(parsed).unwrap_err())));
        }
        for (auto& occurrence : parsed->occurrences) {
            result.c.occurrences.push(rstd::move(occurrence));
        }
    }
    for (const auto& input : configuration.global_options.linker) {
        result.linker.push(input.clone());
    }
    return Ok(rstd::move(result));
}

auto make_profile_spec(const BuildConfiguration&               configuration,
                       const lito::manifest::ProjectProfile&   project_profile,
                       const lito::manifest::BuildProfileName& selected_profile,
                       ParsedGlobalBuildOptions                arguments)
    -> lito::manifest::BuildProfileResult<ProfileSpec> {
    auto selected =
        rstd_try(lito::manifest::resolve_build_profile(project_profile, selected_profile));
    auto cpp_link_requirements = lito::link::Requirements {};
    for (const auto& occurrence : arguments.cpp.occurrences) {
        auto option = occurrence.raw_tokens[usize {}].as_str();
        RSTD_MATCH(occurrence.argument) {
            RSTD_CASE(Macro, directive) {
                if (is_profile_owned_definition(directive.value.as_str()))
                    return profile_failure(
                        rstd::format("compiler option '{}' from {} overrides the selected profile",
                                     option,
                                     occurrence.source.as_str()));
            }
            RSTD_CASE(OwnedSetting, setting) {
                if (setting == CppOwnedSetting::Optimization ||
                    setting == CppOwnedSetting::DebugInfo || setting == CppOwnedSetting::Lto)
                    return profile_failure(
                        rstd::format("compiler option '{}' from {} overrides the selected profile",
                                     option,
                                     occurrence.source.as_str()));
            }
            RSTD_CASE(IncludeDirectory, value) {
                static_cast<void>(value);
            }
            RSTD_CASE(Common, value) {
                if (value.is_Threading() &&
                    value.as_Threading().model == lito::compiler::ThreadingModel::Posix) {
                    cpp_link_requirements.posix_threads = true;
                    cpp_link_requirements.thread_sources.push(occurrence.source.clone());
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
    auto c_link_requirements = lito::link::Requirements {};
    for (const auto& occurrence : arguments.c.occurrences) {
        if (! occurrence.argument.is_Common()) continue;
        const auto& common = occurrence.argument.as_Common().argument;
        if (common.is_Threading() &&
            common.as_Threading().model == lito::compiler::ThreadingModel::Posix) {
            c_link_requirements.posix_threads = true;
            c_link_requirements.thread_sources.push(occurrence.source.clone());
        }
    }
    auto linker_options = Vec<String>::make();
    for (const auto& input : arguments.linker) {
        auto normalized = lito::link::normalize_arguments(lito::link::ArgumentSequence {
            .tokens   = input.arguments.clone(),
            .source   = input.source.clone(),
            .identity = input.source.clone(),
        });
        if (normalized.is_err()) {
            return Err(lito::manifest::BuildProfileError::Options(
                erase_error(rstd::move(normalized).unwrap_err())));
        }
        lito::link::append_requirements(c_link_requirements, normalized->requirements);
        lito::link::append_requirements(cpp_link_requirements, normalized->requirements);
        for (auto& option : normalized->arguments.tokens) {
            if (option.as_str() == "-nostdlib++"_str ||
                option.as_str().starts_with("-stdlib="_str)) {
                return profile_failure(
                    rstd::format("linker option '{}' from {} overrides a Lito-owned setting",
                                 option.as_str(),
                                 input.source.as_str()));
            }
            if (is_profile_owned_linker_option(option.as_str())) {
                return profile_failure(
                    rstd::format("linker option '{}' from {} overrides the selected profile",
                                 option.as_str(),
                                 input.source.as_str()));
            }
            linker_options.push(rstd::move(option));
        }
    }
    auto c_layer = lito::c::CArgumentLayer {};
    if (selected.ndebug) c_layer.definitions.push(String::make("NDEBUG"_str));
    for (auto& occurrence : arguments.c.occurrences) {
        c_layer.occurrences.push(rstd::move(occurrence));
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
    cpp_layer.arguments = rstd::move(arguments.cpp);
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
        .name   = selected.name.value.clone(),
        .family = selected.family,
        .bmi    = BmiRequest { .representation   = configuration.bmi_mode,
                               .source_embedding = configuration.bmi_source_embedding },
        .c      = rstd::move(c),
        .cpp    = rstd::move(cpp),
        .c_link_requirements   = rstd::move(c_link_requirements),
        .cpp_link_requirements = rstd::move(cpp_link_requirements),
        .strip                 = selected.strip,
        .linker_options        = rstd::move(linker_options),
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
