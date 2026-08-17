module;
#include <rstd/enum.hpp>
#include <rstd/macro.hpp>

export module lito.cpp:compiler.binding;

import rstd;
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
            case lito::cpp::CppOwnedSetting::Optimization: field = "optimization"_str; break;
            case lito::cpp::CppOwnedSetting::DebugInfo: field = "debug info"_str; break;
            case lito::cpp::CppOwnedSetting::Lto: field = "LTO"_str; break;
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
    case CppCompilerArgumentKind::OwnedOptimization: return CppOwnedSetting::Optimization;
    case CppCompilerArgumentKind::OwnedDebugInfo: return CppOwnedSetting::DebugInfo;
    case CppCompilerArgumentKind::OwnedLto: return CppOwnedSetting::Lto;
    default: return CppOwnedSetting::LanguageStandard;
    }
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
        return Ok(CppCompilerArgument::Target(compiler_argument_value(matched).clone()));
    case CppCompilerArgumentKind::Sysroot:
        return Ok(CppCompilerArgument::Sysroot(compiler_argument_value(matched).clone()));
    case CppCompilerArgumentKind::OwnedLanguageStandard:
    case CppCompilerArgumentKind::OwnedStandardLibrary:
    case CppCompilerArgumentKind::OwnedBmiRepresentation:
    case CppCompilerArgumentKind::OwnedRtti:
    case CppCompilerArgumentKind::OwnedExceptions:
    case CppCompilerArgumentKind::OwnedOptimization:
    case CppCompilerArgumentKind::OwnedDebugInfo:
    case CppCompilerArgumentKind::OwnedLto:
        return Ok(CppCompilerArgument::OwnedSetting(owned_setting(kind)));
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
        return Ok(CppCompilerArgument::Threading(CppThreadingModel::Posix));
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

} // namespace lito::cpp
