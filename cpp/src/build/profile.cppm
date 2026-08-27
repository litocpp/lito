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

struct CodegenSettingSources {
    Option<String> optimization;
    Option<String> debug_info;
    Option<String> lto;
    Option<String> ndebug;

    auto clone() const -> CodegenSettingSources {
        return CodegenSettingSources {
            .optimization = optimization.clone(),
            .debug_info   = debug_info.clone(),
            .lto          = lto.clone(),
            .ndebug       = ndebug.clone(),
        };
    }
};

struct CppLanguageSettingSources {
    String exceptions;
    String rtti;

    auto clone() const -> CppLanguageSettingSources {
        return CppLanguageSettingSources {
            .exceptions = exceptions.clone(),
            .rtti       = rtti.clone(),
        };
    }
};

struct ProfileSpec {
    String                             name;
    lito::manifest::BuildProfileFamily family { lito::manifest::BuildProfileFamily::Debug };
    BmiRequest                         bmi;
    lito::c::CCompileOptions           c;
    CppCompileOptions                  cpp;
    lito::link::Requirements           c_link_requirements;
    lito::link::Requirements           cpp_link_requirements;
    lito::artifact::StripMode          strip { lito::artifact::StripMode::None };
    Option<bool>                       c_ndebug;
    Option<bool>                       cpp_ndebug;
    Option<lito::manifest::Lto>        link_lto;
    Option<lito::artifact::StripMode>  linker_strip;
    CodegenSettingSources              c_sources;
    CodegenSettingSources              cpp_sources;
    CppLanguageSettingSources          cpp_language_sources;
    Option<String>                     strip_source;
    Option<String>                     link_lto_source;
    Option<String>                     linker_strip_source;
    Vec<String>                        linker_options;
};

struct EffectiveNativeProfile {
    lito::manifest::PackageLanguage      language { lito::manifest::PackageLanguage::Cpp };
    Option<lito::manifest::Optimization> optimization;
    Option<lito::manifest::DebugInfo>    debug_info;
    Option<lito::manifest::Lto>          compile_lto;
    Option<lito::manifest::Lto>          link_lto;
    Option<bool>                         ndebug;
    Option<lito::artifact::StripMode>    strip;
    CodegenSettingSources                sources;
    Option<String>                       link_lto_source;
    Option<String>                       strip_source;
};

auto effective_native_profile(const ProfileSpec& profile, lito::manifest::PackageLanguage language)
    -> EffectiveNativeProfile {
    const auto& codegen = language == lito::manifest::PackageLanguage::C
                              ? profile.c.common.codegen
                              : profile.cpp.common.codegen;
    const auto& sources =
        language == lito::manifest::PackageLanguage::C ? profile.c_sources : profile.cpp_sources;
    auto result = EffectiveNativeProfile {
        .language     = language,
        .optimization = codegen.optimization,
        .debug_info   = codegen.debug_info,
        .compile_lto  = codegen.lto,
        .link_lto     = profile.link_lto,
        .ndebug =
            language == lito::manifest::PackageLanguage::C ? profile.c_ndebug : profile.cpp_ndebug,
        .sources         = sources.clone(),
        .link_lto_source = profile.link_lto_source.clone(),
    };
    if (result.ndebug.is_none() && result.sources.ndebug.is_some()) {
        result.ndebug = Some(false);
    }
    if (profile.linker_strip.is_some()) {
        result.strip        = profile.linker_strip;
        result.strip_source = profile.linker_strip_source.clone();
    } else if (profile.strip_source.is_some()) {
        result.strip        = Some<lito::artifact::StripMode>(profile.strip);
        result.strip_source = profile.strip_source.clone();
    }
    return result;
}

struct ParsedGlobalBuildOptions {
    CppArgumentLayer                    cpp;
    lito::c::CArgumentLayer             c;
    Vec<lito::config::BuildOptionInput> linker;
};

auto apply_build_platform(ProfileSpec& profile, const lito::system::BuildPlatform& platform)
    -> lito::manifest::BuildProfileResult<empty> {
    auto target   = lito::compiler::TargetOptions {};
    target.target = Some(platform.effective_target.triple.clone());
    if (platform.sysroot.is_some()) {
        auto text = platform.sysroot->as_path().to_str();
        if (text.is_none()) {
            return Err(lito::manifest::BuildProfileError::Message(rstd::format(
                "target sysroot '{}' is not valid UTF-8", platform.sysroot->as_path())));
        }
        target.sysroot = Some(String::make(*text));
    }
    profile.c.common.target   = target.clone();
    profile.cpp.common.target = rstd::move(target);
    return Ok(empty {});
}

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

template<typename T>
auto profile_failure(String message) -> lito::manifest::BuildProfileResult<T> {
    return Err(lito::manifest::BuildProfileError::Message(rstd::move(message)));
}

auto occurrence_option(const Vec<String>& tokens) -> ref<str> {
    return tokens.is_empty() ? "<structured compiler option>"_str : tokens[usize {}].as_str();
}

auto language_setting_text(bool enabled) noexcept -> ref<str> {
    return enabled ? "enabled"_str : "disabled"_str;
}

auto merge_cpp_language_setting(const lito::manifest::BooleanProfileSetting& policy,
                                bool&                                        effective,
                                String&                                      effective_source,
                                bool                                         requested_value,
                                ref<str>                                     field,
                                ref<str>                                     profile_name,
                                ref<str>                                     option,
                                ref<str> source) -> lito::manifest::BuildProfileResult<empty> {
    if (! policy.is_delegated() && policy.default_value() != requested_value) {
        return profile_failure<empty>(
            rstd::format("compiler option '{}' from {} sets {} to '{}', but the selected profile "
                         "'{}' fixes it to '{}'",
                         option,
                         source,
                         field,
                         language_setting_text(requested_value),
                         profile_name,
                         language_setting_text(policy.default_value())));
    }
    effective = requested_value;
    if (policy.is_delegated()) effective_source = String::make(source);
    return Ok(empty {});
}

auto profile_setting_text(lito::manifest::Optimization value) -> ref<str> {
    auto text = cpp_optimization_option(value);
    return text.is_empty() ? "compiler default"_str : text;
}

auto profile_setting_text(lito::manifest::DebugInfo value) -> ref<str> {
    return cpp_debug_option(value);
}

auto profile_setting_text(lito::manifest::Lto value) -> ref<str> {
    return cpp_lto_option(value);
}

auto profile_setting_text(lito::artifact::StripMode value) -> ref<str> {
    switch (value) {
    case lito::artifact::StripMode::None: return "none"_str;
    case lito::artifact::StripMode::DebugInfo: return "debuginfo"_str;
    case lito::artifact::StripMode::Symbols: return "symbols"_str;
    }
    return "none"_str;
}

template<typename T>
auto merge_codegen_setting(const lito::manifest::ProfileSetting<T>& policy,
                           Option<T>&                               effective,
                           Option<String>&                          effective_source,
                           T                                        value,
                           ref<str>                                 field,
                           ref<str>                                 profile_name,
                           ref<str>                                 option,
                           ref<str> source) -> lito::manifest::BuildProfileResult<empty> {
    if (policy.fixed.is_some() && *policy.fixed != value) {
        return profile_failure<empty>(
            rstd::format("compiler option '{}' from {} sets {} to '{}', but the selected profile "
                         "'{}' fixes it to '{}'",
                         option,
                         source,
                         field,
                         profile_setting_text(value),
                         profile_name,
                         profile_setting_text(*policy.fixed)));
    }
    effective        = Some<T>(policy.fixed.is_some() ? *policy.fixed : value);
    effective_source = policy.is_delegated() ? Some(String::make(source))
                                             : Some(rstd::format("profile '{}'", profile_name));
    return Ok(empty {});
}

auto merge_codegen_setting(const lito::manifest::ResolvedBuildProfile&   profile,
                           lito::compiler::CodegenOptions&               effective,
                           CodegenSettingSources&                        sources,
                           const lito::compiler::CodegenCompilerSetting& setting,
                           const CppCompilerArgumentOccurrence&          occurrence)
    -> lito::manifest::BuildProfileResult<empty> {
    auto option = occurrence_option(occurrence.raw_tokens);
    RSTD_MATCH(setting) {
        RSTD_CASE(Optimization, value) {
            return merge_codegen_setting(profile.optimization,
                                         effective.optimization,
                                         sources.optimization,
                                         value,
                                         "optimization"_str,
                                         profile.name.as_str(),
                                         option,
                                         occurrence.source.as_str());
        }
        RSTD_CASE(DebugInfo, value) {
            return merge_codegen_setting(profile.debug_info,
                                         effective.debug_info,
                                         sources.debug_info,
                                         value,
                                         "debug information"_str,
                                         profile.name.as_str(),
                                         option,
                                         occurrence.source.as_str());
        }
        RSTD_CASE(Lto, value) {
            return merge_codegen_setting(profile.lto,
                                         effective.lto,
                                         sources.lto,
                                         value,
                                         "LTO"_str,
                                         profile.name.as_str(),
                                         option,
                                         occurrence.source.as_str());
        }
    }
    rstd::unreachable();
}

auto merge_codegen_setting(const lito::manifest::ResolvedBuildProfile&   profile,
                           lito::compiler::CodegenOptions&               effective,
                           CodegenSettingSources&                        sources,
                           const lito::compiler::CodegenCompilerSetting& setting,
                           const lito::c::CCompilerArgumentOccurrence&   occurrence)
    -> lito::manifest::BuildProfileResult<empty> {
    auto option = occurrence_option(occurrence.raw_tokens);
    RSTD_MATCH(setting) {
        RSTD_CASE(Optimization, value) {
            return merge_codegen_setting(profile.optimization,
                                         effective.optimization,
                                         sources.optimization,
                                         value,
                                         "optimization"_str,
                                         profile.name.as_str(),
                                         option,
                                         occurrence.source.as_str());
        }
        RSTD_CASE(DebugInfo, value) {
            return merge_codegen_setting(profile.debug_info,
                                         effective.debug_info,
                                         sources.debug_info,
                                         value,
                                         "debug information"_str,
                                         profile.name.as_str(),
                                         option,
                                         occurrence.source.as_str());
        }
        RSTD_CASE(Lto, value) {
            return merge_codegen_setting(profile.lto,
                                         effective.lto,
                                         sources.lto,
                                         value,
                                         "LTO"_str,
                                         profile.name.as_str(),
                                         option,
                                         occurrence.source.as_str());
        }
    }
    rstd::unreachable();
}

auto fixed_codegen(const lito::manifest::ResolvedBuildProfile& profile,
                   CodegenSettingSources& sources) -> lito::compiler::CodegenOptions {
    auto result = lito::compiler::CodegenOptions {};
    auto source = rstd::format("profile '{}'", profile.name.as_str());
    if (profile.optimization.fixed.is_some()) {
        result.optimization  = Some<lito::manifest::Optimization>(*profile.optimization.fixed);
        sources.optimization = Some(source.clone());
    }
    if (profile.debug_info.fixed.is_some()) {
        result.debug_info  = Some<lito::manifest::DebugInfo>(*profile.debug_info.fixed);
        sources.debug_info = Some(source.clone());
    }
    if (profile.lto.fixed.is_some()) {
        result.lto  = Some<lito::manifest::Lto>(*profile.lto.fixed);
        sources.lto = Some(source.clone());
    }
    if (profile.ndebug.fixed.is_some()) sources.ndebug = Some(rstd::move(source));
    return result;
}

auto definition_state(CppMacroAction action) noexcept -> bool {
    return action == CppMacroAction::Define;
}

auto definition_state(lito::c::CMacroAction action) noexcept -> bool {
    return action == lito::c::CMacroAction::Define;
}

auto merge_ndebug(const lito::manifest::ProfileSetting<bool>& policy,
                  Option<bool>&                               effective,
                  Option<String>&                             effective_source,
                  bool                                        value,
                  ref<str>                                    profile_name,
                  ref<str>                                    option,
                  ref<str> source) -> lito::manifest::BuildProfileResult<bool> {
    if (policy.fixed.is_some()) {
        if (*policy.fixed != value) {
            return profile_failure<bool>(rstd::format(
                "compiler option '{}' from {} sets NDEBUG to {}, but the selected profile '{}' "
                "fixes it to {}",
                option,
                source,
                value,
                profile_name,
                *policy.fixed));
        }
        effective = *policy.fixed ? Some<bool>(true) : None();
        return Ok(true);
    }
    effective        = Some(value);
    effective_source = Some(String::make(source));
    return Ok(false);
}

auto linker_option_is_delegated(const lito::manifest::ResolvedBuildProfile& profile,
                                ref<str>                                    option) -> bool {
    if (option == "-O"_str || option.starts_with("-O"_str)) {
        return profile.optimization.is_delegated();
    }
    if (option == "-g"_str || option.starts_with("-g"_str)) {
        return profile.debug_info.is_delegated();
    }
    if (option == "-flto"_str || option.starts_with("-flto="_str) || option == "-fno-lto"_str) {
        return profile.lto.is_delegated();
    }
    if (option == "-s"_str || option == "--strip-all"_str || option == "--strip-debug"_str ||
        (option.starts_with("-Wl,"_str) &&
         (option.contains(",--strip-all"_str) || option.contains(",--strip-debug"_str) ||
          option.ends_with(",-s"_str) || option.contains(",-s,"_str)))) {
        return profile.strip.is_delegated();
    }
    return false;
}

auto merge_link_strip(const lito::manifest::ResolvedBuildProfile& profile,
                      Option<lito::artifact::StripMode>&          effective,
                      Option<String>&                             effective_source,
                      lito::artifact::StripMode                   value,
                      ref<str>                                    profile_name,
                      ref<str>                                    option,
                      ref<str> source) -> lito::manifest::BuildProfileResult<bool> {
    if (profile.strip.fixed.is_some()) {
        if (*profile.strip.fixed != value) {
            return profile_failure<bool>(rstd::format(
                "linker option '{}' from {} sets strip to '{}', but the selected profile '{}' "
                "fixes it to '{}'",
                option,
                source,
                profile_setting_text(value),
                profile_name,
                profile_setting_text(*profile.strip.fixed)));
        }
        effective        = Some(value);
        effective_source = Some(String::make(source));
        return Ok(true);
    }
    effective        = Some(value);
    effective_source = Some(String::make(source));
    return Ok(true);
}

auto validate_link_lto(const Option<lito::manifest::Lto>& compile,
                       const Option<lito::manifest::Lto>& link,
                       ref<str>                           language,
                       ref<str> source) -> lito::manifest::BuildProfileResult<empty> {
    if (compile.is_none() || link.is_none() || *compile == *link) return Ok(empty {});
    return profile_failure<empty>(
        rstd::format("linker LTO from {} conflicts with the effective {} compiler LTO setting",
                     source,
                     language));
}

auto link_option_sequence_matches(const Vec<String>& tokens,
                                  usize              begin,
                                  const Vec<String>& expected) -> bool {
    if (expected.is_empty() || begin + expected.len() > tokens.len()) return false;
    for (auto offset = usize {}; offset < expected.len(); ++offset) {
        if (tokens[begin + offset] != expected[offset].as_str()) return false;
    }
    return true;
}

auto belongs_to_profile_option(const Vec<String>&      tokens,
                               usize                   index,
                               const Vec<Vec<String>>& occurrences) -> bool {
    for (const auto& occurrence : occurrences) {
        for (auto offset = usize {}; offset < occurrence.len(); ++offset) {
            if (index < offset) continue;
            if (link_option_sequence_matches(tokens, index - offset, occurrence)) return true;
        }
    }
    return false;
}

} // namespace lito::cpp

export namespace lito::cpp
{

auto parse_build_arguments(const lito::config::ProjectBuildOptions& options,
                           const CppArgumentParser&                 parser)
    -> lito::manifest::BuildProfileResult<ParsedGlobalBuildOptions> {
    auto result = ParsedGlobalBuildOptions {};
    for (const auto& input : options.cpp) {
        auto parsed = parser.parse(input.arguments, input.source.as_str());
        if (parsed.is_err()) {
            return Err(lito::manifest::BuildProfileError::Options(
                erase_error(rstd::move(parsed).unwrap_err())));
        }
        for (auto& occurrence : parsed->occurrences) {
            result.cpp.occurrences.push(rstd::move(occurrence));
        }
    }
    for (const auto& input : options.c) {
        auto parsed = parser.parse_c(input.arguments, input.source.as_str());
        if (parsed.is_err()) {
            return Err(lito::manifest::BuildProfileError::Options(
                erase_error(rstd::move(parsed).unwrap_err())));
        }
        for (auto& occurrence : parsed->occurrences) {
            result.c.occurrences.push(rstd::move(occurrence));
        }
    }
    for (const auto& input : options.linker) {
        result.linker.push(input.clone());
    }
    return Ok(rstd::move(result));
}

auto parse_build_arguments(const BuildConfiguration& configuration, const CppArgumentParser& parser)
    -> lito::manifest::BuildProfileResult<ParsedGlobalBuildOptions> {
    return parse_build_arguments(configuration.global_options, parser);
}

auto make_profile_spec(const BuildConfiguration&               configuration,
                       const lito::manifest::ProjectProfile&   project_profile,
                       const lito::manifest::BuildProfileName& selected_profile,
                       ParsedGlobalBuildOptions                arguments)
    -> lito::manifest::BuildProfileResult<ProfileSpec> {
    auto selected =
        rstd_try(lito::manifest::resolve_build_profile(project_profile, selected_profile));
    auto profile_source       = rstd::format("profile '{}'", selected.name.as_str());
    auto cpp_exceptions       = selected.exceptions.default_value();
    auto cpp_rtti             = selected.rtti.default_value();
    auto cpp_language_sources = CppLanguageSettingSources {
        .exceptions = profile_source.clone(),
        .rtti       = rstd::move(profile_source),
    };
    auto cpp_sources = CodegenSettingSources {};
    auto c_sources   = CodegenSettingSources {};
    auto cpp_codegen = fixed_codegen(selected, cpp_sources);
    auto c_codegen   = fixed_codegen(selected, c_sources);
    auto cpp_ndebug  = Option<bool> {};
    auto c_ndebug    = Option<bool> {};
    if (selected.ndebug.fixed.is_some() && *selected.ndebug.fixed) {
        cpp_ndebug = Some<bool>(true);
        c_ndebug   = Some<bool>(true);
    }

    auto cpp_link_requirements = lito::link::Requirements {};
    auto cpp_arguments         = CppArgumentLayer {};
    for (auto& occurrence : arguments.cpp.occurrences) {
        auto consume = false;
        RSTD_MATCH(occurrence.argument) {
            RSTD_CASE(Macro, directive) {
                if (is_profile_owned_definition(directive.value.as_str())) {
                    consume = rstd_try(merge_ndebug(selected.ndebug,
                                                    cpp_ndebug,
                                                    cpp_sources.ndebug,
                                                    definition_state(directive.action),
                                                    selected.name.as_str(),
                                                    occurrence_option(occurrence.raw_tokens),
                                                    occurrence.source.as_str()));
                }
            }
            RSTD_CASE(CodegenSetting, setting) {
                rstd_try(
                    merge_codegen_setting(selected, cpp_codegen, cpp_sources, setting, occurrence));
                consume = true;
            }
            RSTD_CASE(OwnedSetting, setting, enabled) {
                if (enabled.is_none()) {
                    return profile_failure<ProfileSpec>(
                        rstd::format("compiler option '{}' from {} overrides a Lito-owned setting",
                                     occurrence_option(occurrence.raw_tokens),
                                     occurrence.source.as_str()));
                }
                auto        field            = "RTTI"_str;
                const auto* policy           = rstd::addressof(selected.rtti);
                auto*       effective        = rstd::addressof(cpp_rtti);
                auto*       effective_source = rstd::addressof(cpp_language_sources.rtti);
                if (setting == CppOwnedSetting::Exceptions) {
                    field            = "exceptions"_str;
                    policy           = rstd::addressof(selected.exceptions);
                    effective        = rstd::addressof(cpp_exceptions);
                    effective_source = rstd::addressof(cpp_language_sources.exceptions);
                }
                rstd_try(merge_cpp_language_setting(*policy,
                                                    *effective,
                                                    *effective_source,
                                                    *enabled,
                                                    field,
                                                    selected.name.as_str(),
                                                    occurrence_option(occurrence.raw_tokens),
                                                    occurrence.source.as_str()));
                consume = true;
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
        if (! consume) cpp_arguments.occurrences.push(rstd::move(occurrence));
    }

    auto c_link_requirements = lito::link::Requirements {};
    auto c_arguments         = lito::c::CArgumentLayer {};
    for (auto& occurrence : arguments.c.occurrences) {
        auto consume = false;
        if (occurrence.argument.is_Common()) {
            const auto& common = occurrence.argument.as_Common().argument;
            if (common.is_Threading() &&
                common.as_Threading().model == lito::compiler::ThreadingModel::Posix) {
                c_link_requirements.posix_threads = true;
                c_link_requirements.thread_sources.push(occurrence.source.clone());
            }
        } else if (occurrence.argument.is_CodegenSetting()) {
            rstd_try(merge_codegen_setting(selected,
                                           c_codegen,
                                           c_sources,
                                           occurrence.argument.as_CodegenSetting().setting,
                                           occurrence));
            consume = true;
        } else if (occurrence.argument.is_Macro()) {
            const auto& directive = occurrence.argument.as_Macro().directive;
            if (is_profile_owned_definition(directive.value.as_str())) {
                consume = rstd_try(merge_ndebug(selected.ndebug,
                                                c_ndebug,
                                                c_sources.ndebug,
                                                definition_state(directive.action),
                                                selected.name.as_str(),
                                                occurrence_option(occurrence.raw_tokens),
                                                occurrence.source.as_str()));
            }
        }
        if (! consume) c_arguments.occurrences.push(rstd::move(occurrence));
    }

    auto linker_options      = Vec<String>::make();
    auto link_lto            = Option<lito::manifest::Lto> {};
    auto linker_strip        = Option<lito::artifact::StripMode> {};
    auto link_lto_source     = Option<String> {};
    auto linker_strip_source = Option<String> {};
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
        auto typed_profile_options = Vec<Vec<String>>::make();
        for (const auto& occurrence : normalized->profile_arguments) {
            auto option = occurrence_option(occurrence.raw_tokens);
            if (occurrence.argument.is_Lto()) {
                rstd_try(merge_codegen_setting(selected.lto,
                                               link_lto,
                                               link_lto_source,
                                               occurrence.argument.as_Lto().value,
                                               "LTO"_str,
                                               selected.name.as_str(),
                                               option,
                                               occurrence.source.as_str()));
                typed_profile_options.push(occurrence.raw_tokens.clone());
                continue;
            }
            rstd_try(merge_link_strip(selected,
                                      linker_strip,
                                      linker_strip_source,
                                      occurrence.argument.as_Strip().value,
                                      selected.name.as_str(),
                                      option,
                                      occurrence.source.as_str()));
            typed_profile_options.push(occurrence.raw_tokens.clone());
        }
        for (auto index = usize {}; index < normalized->arguments.tokens.len(); ++index) {
            auto& option = normalized->arguments.tokens[index];
            if (option.as_str() == "-nostdlib++"_str ||
                option.as_str().starts_with("-stdlib="_str)) {
                return profile_failure<ProfileSpec>(
                    rstd::format("linker option '{}' from {} overrides a Lito-owned setting",
                                 option.as_str(),
                                 input.source.as_str()));
            }
            if (is_profile_owned_linker_option(option.as_str())) {
                const auto typed = belongs_to_profile_option(
                    normalized->arguments.tokens, index, typed_profile_options);
                if (! typed && ! linker_option_is_delegated(selected, option.as_str())) {
                    return profile_failure<ProfileSpec>(
                        rstd::format("linker option '{}' from {} overrides the selected profile",
                                     option.as_str(),
                                     input.source.as_str()));
                }
            }
            linker_options.push(rstd::move(option));
        }
    }
    if (link_lto_source.is_some()) {
        rstd_try(validate_link_lto(c_codegen.lto, link_lto, "C"_str, link_lto_source->as_str()));
        rstd_try(
            validate_link_lto(cpp_codegen.lto, link_lto, "C++"_str, link_lto_source->as_str()));
    }

    auto c_layer = lito::c::CArgumentLayer {};
    if (selected.ndebug.fixed.is_some() && *selected.ndebug.fixed) {
        c_layer.definitions.push(String::make("NDEBUG"_str));
    }
    c_layer.occurrences = rstd::move(c_arguments.occurrences);
    auto c_common       = lito::compiler::CommonCompileOptions {
        .codegen = rstd::move(c_codegen),
    };
    auto c =
        lito::c::apply_c_option_layer(lito::c::make_c_options(rstd::move(c_common),
                                                              configuration.c_standard.is_some()
                                                                  ? *configuration.c_standard
                                                                  : lito::manifest::CStandard::C99),
                                      rstd::move(c_layer));
    if (c.is_err()) {
        return Err(
            lito::manifest::BuildProfileError::Options(erase_error(rstd::move(c).unwrap_err())));
    }

    auto cpp_layer = CppOptionLayer {};
    if (selected.ndebug.fixed.is_some() && *selected.ndebug.fixed) {
        cpp_layer.definitions.push(String::make("NDEBUG"_str));
    }
    cpp_layer.arguments = rstd::move(cpp_arguments);
    auto cpp_result     = make_cpp_options(configuration.language_standard.as_str(),
                                           configuration.standard_library,
                                           cpp_exceptions,
                                           cpp_rtti,
                                           rstd::move(cpp_codegen),
                                           rstd::move(cpp_layer));
    if (cpp_result.is_err()) {
        return Err(lito::manifest::BuildProfileError::Options(
            erase_error(rstd::move(cpp_result).unwrap_err())));
    }
    auto cpp = rstd::move(cpp_result).unwrap();
    return Ok(ProfileSpec {
        .name   = selected.name.value.clone(),
        .family = selected.family,
        .bmi    = BmiRequest { .representation   = configuration.bmi_mode,
                               .source_embedding = configuration.bmi_source_embedding },
        .c      = rstd::move(c).unwrap(),
        .cpp    = rstd::move(cpp),
        .c_link_requirements   = rstd::move(c_link_requirements),
        .cpp_link_requirements = rstd::move(cpp_link_requirements),
        .strip                 = selected.strip.fixed.is_some() && linker_strip.is_none()
                                     ? *selected.strip.fixed
                                     : lito::artifact::StripMode::None,
        .c_ndebug              = c_ndebug,
        .cpp_ndebug            = cpp_ndebug,
        .link_lto              = link_lto,
        .linker_strip          = linker_strip,
        .c_sources             = rstd::move(c_sources),
        .cpp_sources           = rstd::move(cpp_sources),
        .cpp_language_sources  = rstd::move(cpp_language_sources),
        .strip_source          = selected.strip.fixed.is_some() && linker_strip.is_none()
                                     ? Some(rstd::format("profile '{}'", selected.name.as_str()))
                                     : None(),
        .link_lto_source       = rstd::move(link_lto_source),
        .linker_strip_source   = rstd::move(linker_strip_source),
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
