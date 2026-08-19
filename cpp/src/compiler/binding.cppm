module;
#include <rstd/enum.hpp>
#include <rstd/macro.hpp>

export module lito.cpp:compiler.binding;

import rstd;
import :c.compiler;
import :compiler.argument;
import :compiler.option;

using namespace rstd::prelude;
using namespace rstd::literals;
using PathBuf = rstd::path::PathBuf;

export namespace lito::cpp
{

struct CppCompilerArgumentBinding {
    CppCompilerArgumentKind     kind { CppCompilerArgumentKind::VendorLanguage };
    String                      family;
    Option<CppCompilerArgument> typed;
};

} // namespace lito::cpp

export namespace rstd
{

template<>
struct Impl<convert::TryFrom<lito::cpp::CppCompilerArgumentOccurrence>, lito::cpp::CppOptionDelta> {
    using Error = lito::cpp::CppOptionError;

    static auto try_from(lito::cpp::CppCompilerArgumentOccurrence occurrence)
        -> Result<lito::cpp::CppOptionDelta, Error> {
        if (occurrence.argument.is_CodegenSetting()) {
            auto        field   = "codegen"_str;
            const auto& setting = occurrence.argument.as_CodegenSetting().setting;
            if (setting.is_Optimization()) field = "optimization"_str;
            if (setting.is_DebugInfo()) field = "debug information"_str;
            if (setting.is_Lto()) field = "link-time optimization"_str;
            auto spelling = occurrence.raw_tokens.is_empty()
                                ? String::make("<structured compiler option>"_str)
                                : occurrence.raw_tokens[usize {}].clone();
            return Err(lito::cpp::CppOptionError::Message(
                rstd::format("{} arguments {}..{}: compiler option '{}' overrides a Lito-owned "
                             "{} setting",
                             occurrence.source.as_str(),
                             occurrence.range.begin,
                             occurrence.range.end,
                             spelling.as_str(),
                             field)));
        }
        if (occurrence.argument.is_OwnedSetting()) {
            auto field = "language standard"_str;
            switch (occurrence.argument.as_OwnedSetting().setting) {
            case lito::cpp::CppOwnedSetting::LanguageStandard: break;
            case lito::cpp::CppOwnedSetting::StandardLibrary: field = "standard library"_str; break;
            case lito::cpp::CppOwnedSetting::BmiRepresentation:
                field = "BMI representation"_str;
                break;
            case lito::cpp::CppOwnedSetting::Rtti: field = "RTTI"_str; break;
            case lito::cpp::CppOwnedSetting::Exceptions: field = "exceptions"_str; break;
            }
            auto spelling = occurrence.raw_tokens.is_empty()
                                ? String::make("<structured compiler option>"_str)
                                : occurrence.raw_tokens[usize {}].clone();
            return Err(lito::cpp::CppOptionError::Message(
                rstd::format("{} arguments {}..{}: compiler option '{}' overrides Lito-owned {}",
                             occurrence.source.as_str(),
                             occurrence.range.begin,
                             occurrence.range.end,
                             spelling.as_str(),
                             field)));
        }
        return Ok(lito::cpp::CppOptionDelta {
            .argument   = rstd::move(occurrence.argument),
            .raw_tokens = rstd::move(occurrence.raw_tokens),
            .range      = occurrence.range,
            .source     = rstd::move(occurrence.source),
        });
    }
};

} // namespace rstd

namespace lito::cpp
{

auto compiler_argument_value(const CompilerArgumentMatch& matched) -> const String& {
    return *matched.value;
}

auto owned_setting(CppCompilerArgumentKind kind) noexcept -> CppOwnedSetting {
    switch (kind) {
    case CppCompilerArgumentKind::OwnedLanguageStandard: return CppOwnedSetting::LanguageStandard;
    case CppCompilerArgumentKind::OwnedStandardLibrary: return CppOwnedSetting::StandardLibrary;
    case CppCompilerArgumentKind::OwnedBmiRepresentation: return CppOwnedSetting::BmiRepresentation;
    case CppCompilerArgumentKind::OwnedRtti: return CppOwnedSetting::Rtti;
    case CppCompilerArgumentKind::OwnedExceptions: return CppOwnedSetting::Exceptions;
    default: return CppOwnedSetting::LanguageStandard;
    }
}

auto invalid_codegen_setting(const CompilerArgumentMatch& matched,
                             ref<str>                     source,
                             ref<str>                     expected)
    -> CompilerOptionResult<lito::compiler::CodegenCompilerSetting> {
    return Err(CompilerOptionError::Message(
        rstd::format("{} arguments {}..{}: unsupported codegen option '{}'; expected {}",
                     source,
                     matched.range.begin,
                     matched.range.end,
                     matched.raw_tokens[usize {}].as_str(),
                     expected)));
}

auto codegen_setting(CppCompilerArgumentKind      kind,
                     const CompilerArgumentMatch& matched,
                     ref<str>                     source)
    -> CompilerOptionResult<lito::compiler::CodegenCompilerSetting> {
    if (kind == CppCompilerArgumentKind::OwnedOptimization) {
        auto value = matched.value.is_some() ? matched.value->as_str() : "1"_str;
        if (value == "0"_str)
            return Ok(lito::compiler::CodegenCompilerSetting::Optimization(
                lito::manifest::Optimization::None));
        if (value == "1"_str)
            return Ok(lito::compiler::CodegenCompilerSetting::Optimization(
                lito::manifest::Optimization::Level1));
        if (value == "2"_str)
            return Ok(lito::compiler::CodegenCompilerSetting::Optimization(
                lito::manifest::Optimization::Level2));
        if (value == "3"_str)
            return Ok(lito::compiler::CodegenCompilerSetting::Optimization(
                lito::manifest::Optimization::Level3));
        if (value == "4"_str)
            return Ok(lito::compiler::CodegenCompilerSetting::Optimization(
                lito::manifest::Optimization::Level4));
        if (value == "g"_str)
            return Ok(lito::compiler::CodegenCompilerSetting::Optimization(
                lito::manifest::Optimization::Debug));
        if (value == "s"_str)
            return Ok(lito::compiler::CodegenCompilerSetting::Optimization(
                lito::manifest::Optimization::Size));
        if (value == "z"_str)
            return Ok(lito::compiler::CodegenCompilerSetting::Optimization(
                lito::manifest::Optimization::SizeMin));
        if (value == "fast"_str)
            return Ok(lito::compiler::CodegenCompilerSetting::Optimization(
                lito::manifest::Optimization::Fast));
        return invalid_codegen_setting(matched, source, "-O0..-O4, -Og, -Os, -Oz, or -Ofast"_str);
    }
    if (kind == CppCompilerArgumentKind::OwnedDebugInfo) {
        auto value = matched.value.is_some() ? matched.value->as_str() : "2"_str;
        if (value == "0"_str)
            return Ok(
                lito::compiler::CodegenCompilerSetting::DebugInfo(lito::manifest::DebugInfo::None));
        if (value == "1"_str || value == "limited"_str)
            return Ok(lito::compiler::CodegenCompilerSetting::DebugInfo(
                lito::manifest::DebugInfo::Limited));
        if (value == "2"_str || value == "3"_str || value == "full"_str)
            return Ok(
                lito::compiler::CodegenCompilerSetting::DebugInfo(lito::manifest::DebugInfo::Full));
        if (value == "line-directives-only"_str)
            return Ok(lito::compiler::CodegenCompilerSetting::DebugInfo(
                lito::manifest::DebugInfo::LineDirectivesOnly));
        if (value == "line-tables-only"_str)
            return Ok(lito::compiler::CodegenCompilerSetting::DebugInfo(
                lito::manifest::DebugInfo::LineTablesOnly));
        return invalid_codegen_setting(
            matched, source, "-g, -g0..-g3, -gline-directives-only, or -gline-tables-only"_str);
    }
    if (matched.spelling.as_str() == "-fno-lto"_str) {
        return Ok(lito::compiler::CodegenCompilerSetting::Lto(lito::manifest::Lto::Off));
    }
    auto value = matched.value.is_some() ? matched.value->as_str() : "full"_str;
    if (value == "thin"_str)
        return Ok(lito::compiler::CodegenCompilerSetting::Lto(lito::manifest::Lto::Thin));
    if (value == "full"_str || value == "fat"_str)
        return Ok(lito::compiler::CodegenCompilerSetting::Lto(lito::manifest::Lto::Fat));
    return invalid_codegen_setting(
        matched, source, "-fno-lto, -flto, -flto=thin, or -flto=full"_str);
}

auto family_domain(CppCompilerArgumentKind kind) noexcept -> CppOptionFamilyDomain {
    switch (kind) {
    case CppCompilerArgumentKind::LanguageMode: return CppOptionFamilyDomain::Language;
    case CppCompilerArgumentKind::AbiMode: return CppOptionFamilyDomain::Abi;
    case CppCompilerArgumentKind::TargetMode: return CppOptionFamilyDomain::Target;
    case CppCompilerArgumentKind::CodegenMode: return CppOptionFamilyDomain::Codegen;
    default: return CppOptionFamilyDomain::Language;
    }
}

auto dynamic_target_family(ref<str> value) -> String {
    auto begin = value.starts_with("no-"_str) ? usize(3) : usize {};
    auto end   = begin;
    while (end < value.len() && value.as_bytes()[end] != u8('=')) ++end;
    auto name = value.get(begin, end);
    return name.is_some() ? String::make(*name) : String::make(value);
}

auto canonical_argument(const CompilerArgumentMatch& matched) -> String {
    if (matched.value.is_none() || matched.raw_tokens.len() == usize(1)) {
        return matched.raw_tokens[usize {}].clone();
    }
    auto result = matched.spelling.clone();
    result.push_ascii('=');
    result.push_str(matched.value->as_str());
    return result;
}

auto compiler_argument_raw_tokens(const CompilerArgumentMatch& matched) -> Vec<String> {
    auto result = Vec<String>::with_capacity(matched.raw_tokens.len());
    for (const auto& token : matched.raw_tokens) result.push(token.clone());
    return result;
}

auto make_cpp_compiler_argument(const CompilerArgumentMatch&      matched,
                                const CppCompilerArgumentBinding* binding,
                                ref<str> source) -> CppOptionResult<CppCompilerArgument> {
    if (binding == nullptr) {
        return Ok(CppCompilerArgument::Vendor(CppVendorOption {
            .value               = matched.raw_tokens[usize {}].clone(),
            .raw_tokens          = compiler_argument_raw_tokens(matched),
            .effect              = CppVendorOptionEffect::Unknown,
            .preserve_raw_tokens = true,
        }));
    }
    if (binding->typed.is_some()) return Ok(as<Clone>(*binding->typed).clone());
    auto kind = binding->kind;
    switch (kind) {
    case CppCompilerArgumentKind::MacroDefine:
    case CppCompilerArgumentKind::MacroUndefine:
        return Ok(CppCompilerArgument::Macro(CppMacroDirective {
            .action = kind == CppCompilerArgumentKind::MacroDefine ? CppMacroAction::Define
                                                                   : CppMacroAction::Undefine,
            .value  = compiler_argument_value(matched).clone(),
        }));
    case CppCompilerArgumentKind::IncludeDirectory:
    case CppCompilerArgumentKind::SystemIncludeDirectory:
        return Ok(CppCompilerArgument::IncludeDirectory(CppIncludeDirectory {
            .path = PathBuf::from(compiler_argument_value(matched).clone()),
            .kind = kind == CppCompilerArgumentKind::SystemIncludeDirectory
                        ? CppIncludeDirectoryKind::System
                        : CppIncludeDirectoryKind::User,
        }));
    case CppCompilerArgumentKind::Target:
        return Ok(CppCompilerArgument::Common(lito::compiler::CommonCompilerArgument::Target(
            compiler_argument_value(matched).clone())));
    case CppCompilerArgumentKind::Sysroot:
        return Ok(CppCompilerArgument::Common(lito::compiler::CommonCompilerArgument::Sysroot(
            compiler_argument_value(matched).clone())));
    case CppCompilerArgumentKind::OwnedLanguageStandard:
    case CppCompilerArgumentKind::OwnedStandardLibrary:
    case CppCompilerArgumentKind::OwnedBmiRepresentation:
    case CppCompilerArgumentKind::OwnedRtti:
    case CppCompilerArgumentKind::OwnedExceptions:
        return Ok(CppCompilerArgument::OwnedSetting(owned_setting(kind)));
    case CppCompilerArgumentKind::OwnedOptimization:
    case CppCompilerArgumentKind::OwnedDebugInfo:
    case CppCompilerArgumentKind::OwnedLto:
        return Ok(
            CppCompilerArgument::CodegenSetting(rstd_try(codegen_setting(kind, matched, source))));
    case CppCompilerArgumentKind::LanguageMode:
    case CppCompilerArgumentKind::AbiMode:
    case CppCompilerArgumentKind::TargetMode:
    case CppCompilerArgumentKind::CodegenMode: {
        auto family = binding->family.clone();
        if (family.is_empty()) {
            family = dynamic_target_family(compiler_argument_value(matched).as_str());
        }
        return Ok(CppCompilerArgument::Family(
            family_domain(kind), rstd::move(family), canonical_argument(matched)));
    }
    case CppCompilerArgumentKind::Threading:
        return Ok(CppCompilerArgument::Common(lito::compiler::CommonCompilerArgument::Threading(
            lito::compiler::ThreadingModel::Posix)));
    case CppCompilerArgumentKind::MicrosoftRuntime: {
        auto parsed = lito::compiler::parse_microsoft_runtime_library(
            compiler_argument_value(matched).as_str());
        if (parsed.is_none()) {
            return Err(CppOptionError::Message(
                rstd::format("{} arguments {}..{}: invalid Microsoft runtime library '{}'",
                             source,
                             matched.range.begin,
                             matched.range.end,
                             compiler_argument_value(matched).as_str())));
        }
        return Ok(CppCompilerArgument::Common(
            lito::compiler::CommonCompilerArgument::MicrosoftRuntime(*parsed)));
    }
    case CppCompilerArgumentKind::Instrumentation:
        return Ok(CppCompilerArgument::Instrumentation(canonical_argument(matched)));
    case CppCompilerArgumentKind::SymbolVisibility:
    case CppCompilerArgumentKind::TypeVisibility: {
        const auto& value  = compiler_argument_value(matched);
        auto        parsed = parse_cpp_symbol_visibility(value.as_str());
        if (parsed.is_none()) {
            return Err(CppOptionError::Message(
                rstd::format("{} arguments {}..{}: invalid visibility value '{}' in '{}'; expected "
                             "default, hidden, internal, or protected",
                             source,
                             matched.range.begin,
                             matched.range.end,
                             value.as_str(),
                             matched.raw_tokens[usize {}].as_str())));
        }
        return Ok(kind == CppCompilerArgumentKind::SymbolVisibility
                      ? CppCompilerArgument::SymbolVisibility(*parsed)
                      : CppCompilerArgument::TypeVisibility(*parsed));
    }
    case CppCompilerArgumentKind::Diagnostic:
        return Ok(CppCompilerArgument::Diagnostic(canonical_argument(matched)));
    case CppCompilerArgumentKind::VendorLanguage:
        return Ok(CppCompilerArgument::Vendor(CppVendorOption {
            .value      = canonical_argument(matched),
            .raw_tokens = compiler_argument_raw_tokens(matched),
            .effect     = CppVendorOptionEffect::Language,
        }));
    case CppCompilerArgumentKind::VendorCodegen:
        return Ok(CppCompilerArgument::Vendor(CppVendorOption {
            .value      = canonical_argument(matched),
            .raw_tokens = compiler_argument_raw_tokens(matched),
            .effect     = CppVendorOptionEffect::Codegen,
        }));
    case CppCompilerArgumentKind::VendorPreprocessorUnsupported:
        return Ok(CppCompilerArgument::Vendor(CppVendorOption {
            .value                           = canonical_argument(matched),
            .raw_tokens                      = compiler_argument_raw_tokens(matched),
            .effect                          = CppVendorOptionEffect::Preprocessor,
            .native_preprocessor_unsupported = true,
        }));
    }
    return Ok(CppCompilerArgument::Vendor(CppVendorOption {
        .value               = matched.raw_tokens[usize {}].clone(),
        .raw_tokens          = compiler_argument_raw_tokens(matched),
        .effect              = CppVendorOptionEffect::Unknown,
        .preserve_raw_tokens = true,
    }));
}

auto c_vendor_effect(CppCompilerArgumentKind kind) noexcept -> lito::c::CVendorOptionEffect {
    switch (kind) {
    case CppCompilerArgumentKind::Diagnostic: return lito::c::CVendorOptionEffect::Diagnostic;
    case CppCompilerArgumentKind::VendorCodegen:
    case CppCompilerArgumentKind::CodegenMode:
    case CppCompilerArgumentKind::Instrumentation:
    case CppCompilerArgumentKind::SymbolVisibility: return lito::c::CVendorOptionEffect::Codegen;
    case CppCompilerArgumentKind::VendorPreprocessorUnsupported:
        return lito::c::CVendorOptionEffect::Preprocessor;
    case CppCompilerArgumentKind::LanguageMode:
    case CppCompilerArgumentKind::VendorLanguage: return lito::c::CVendorOptionEffect::Language;
    case CppCompilerArgumentKind::AbiMode: return lito::c::CVendorOptionEffect::Abi;
    case CppCompilerArgumentKind::TargetMode: return lito::c::CVendorOptionEffect::Target;
    default: return lito::c::CVendorOptionEffect::Unknown;
    }
}

auto c_vendor_argument(const CompilerArgumentMatch& matched, lito::c::CVendorOptionEffect effect)
    -> lito::c::CCompilerArgument {
    return lito::c::CCompilerArgument::Vendor(lito::c::CVendorOption {
        .value                           = canonical_argument(matched),
        .raw_tokens                      = compiler_argument_raw_tokens(matched),
        .effect                          = effect,
        .native_preprocessor_unsupported = effect == lito::c::CVendorOptionEffect::Preprocessor,
        .preserve_raw_tokens             = matched.definition.is_none(),
    });
}

auto make_c_compiler_argument(const CompilerArgumentMatch&      matched,
                              const CppCompilerArgumentBinding* binding,
                              ref<str> source) -> CompilerOptionResult<lito::c::CCompilerArgument> {
    if (binding == nullptr) {
        return Ok(c_vendor_argument(matched, lito::c::CVendorOptionEffect::Unknown));
    }
    if (binding->typed.is_some()) {
        const auto& typed = *binding->typed;
        if (typed.is_Common()) {
            return Ok(
                lito::c::CCompilerArgument::Common(as<Clone>(typed.as_Common().argument).clone()));
        }
        return Err(CompilerOptionError::Message(
            rstd::format("{} arguments {}..{}: compiler option '{}' is C++-specific",
                         source,
                         matched.range.begin,
                         matched.range.end,
                         matched.raw_tokens[usize {}].as_str())));
    }
    auto kind = binding->kind;
    switch (kind) {
    case CppCompilerArgumentKind::MacroDefine:
    case CppCompilerArgumentKind::MacroUndefine:
        return Ok(lito::c::CCompilerArgument::Macro(lito::c::CMacroDirective {
            .action = kind == CppCompilerArgumentKind::MacroDefine
                          ? lito::c::CMacroAction::Define
                          : lito::c::CMacroAction::Undefine,
            .value  = compiler_argument_value(matched).clone(),
        }));
    case CppCompilerArgumentKind::IncludeDirectory:
    case CppCompilerArgumentKind::SystemIncludeDirectory:
        return Ok(lito::c::CCompilerArgument::IncludeDirectory(lito::c::CIncludeDirectory {
            .path = PathBuf::from(compiler_argument_value(matched).clone()),
            .kind = kind == CppCompilerArgumentKind::SystemIncludeDirectory
                        ? lito::c::CIncludeDirectoryKind::System
                        : lito::c::CIncludeDirectoryKind::User,
        }));
    case CppCompilerArgumentKind::Target:
        return Ok(lito::c::CCompilerArgument::Common(lito::compiler::CommonCompilerArgument::Target(
            compiler_argument_value(matched).clone())));
    case CppCompilerArgumentKind::Sysroot:
        return Ok(
            lito::c::CCompilerArgument::Common(lito::compiler::CommonCompilerArgument::Sysroot(
                compiler_argument_value(matched).clone())));
    case CppCompilerArgumentKind::Threading:
        return Ok(
            lito::c::CCompilerArgument::Common(lito::compiler::CommonCompilerArgument::Threading(
                lito::compiler::ThreadingModel::Posix)));
    case CppCompilerArgumentKind::MicrosoftRuntime: {
        auto parsed = lito::compiler::parse_microsoft_runtime_library(
            compiler_argument_value(matched).as_str());
        if (parsed.is_none()) {
            return Err(CompilerOptionError::Message(
                rstd::format("{} arguments {}..{}: invalid Microsoft runtime library '{}'",
                             source,
                             matched.range.begin,
                             matched.range.end,
                             compiler_argument_value(matched).as_str())));
        }
        return Ok(lito::c::CCompilerArgument::Common(
            lito::compiler::CommonCompilerArgument::MicrosoftRuntime(*parsed)));
    }
    case CppCompilerArgumentKind::OwnedLanguageStandard:
    case CppCompilerArgumentKind::OwnedStandardLibrary:
    case CppCompilerArgumentKind::OwnedBmiRepresentation:
    case CppCompilerArgumentKind::OwnedRtti:
    case CppCompilerArgumentKind::OwnedExceptions:
        return Err(CompilerOptionError::Message(
            rstd::format("{} arguments {}..{}: compiler option '{}' overrides a Lito-owned setting",
                         source,
                         matched.range.begin,
                         matched.range.end,
                         matched.raw_tokens[usize {}].as_str())));
    case CppCompilerArgumentKind::OwnedOptimization:
    case CppCompilerArgumentKind::OwnedDebugInfo:
    case CppCompilerArgumentKind::OwnedLto:
        return Ok(lito::c::CCompilerArgument::CodegenSetting(
            rstd_try(codegen_setting(kind, matched, source))));
    case CppCompilerArgumentKind::TypeVisibility:
        return Err(CompilerOptionError::Message(
            rstd::format("{} arguments {}..{}: compiler option '{}' is C++-specific",
                         source,
                         matched.range.begin,
                         matched.range.end,
                         matched.raw_tokens[usize {}].as_str())));
    case CppCompilerArgumentKind::Diagnostic:
        return Ok(lito::c::CCompilerArgument::Diagnostic(canonical_argument(matched)));
    case CppCompilerArgumentKind::LanguageMode:
    case CppCompilerArgumentKind::AbiMode:
    case CppCompilerArgumentKind::TargetMode:
    case CppCompilerArgumentKind::CodegenMode:
    case CppCompilerArgumentKind::Instrumentation:
    case CppCompilerArgumentKind::SymbolVisibility:
    case CppCompilerArgumentKind::VendorLanguage:
    case CppCompilerArgumentKind::VendorCodegen:
    case CppCompilerArgumentKind::VendorPreprocessorUnsupported:
        return Ok(c_vendor_argument(matched, c_vendor_effect(kind)));
    }
    return Ok(c_vendor_argument(matched, lito::c::CVendorOptionEffect::Unknown));
}

} // namespace lito::cpp
