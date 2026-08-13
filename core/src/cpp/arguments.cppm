module;
#include <rstd/enum.hpp>
#include <rstd/macro.hpp>

export module lito.cpp:arguments;

import rstd;
import lito.compiler.arguments;
import :model;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace rstd
{

template<>
struct Impl<convert::TryFrom<lito::CppCompilerArgumentOccurrence>, lito::CppOptionDelta> {
    using Error = lito::CppOptionError;

    static auto try_from(lito::CppCompilerArgumentOccurrence occurrence)
        -> Result<lito::CppOptionDelta, Error> {
        if (occurrence.argument.is_OwnedSetting()) {
            auto field = "language standard"_str;
            switch (occurrence.argument.as_OwnedSetting().setting) {
            case lito::CppOwnedSetting::LanguageStandard: break;
            case lito::CppOwnedSetting::StandardLibrary: field = "standard library"_str; break;
            case lito::CppOwnedSetting::BmiRepresentation: field = "BMI representation"_str; break;
            case lito::CppOwnedSetting::Rtti: field = "RTTI"_str; break;
            case lito::CppOwnedSetting::Exceptions: field = "exceptions"_str; break;
            case lito::CppOwnedSetting::Optimization: field = "optimization"_str; break;
            case lito::CppOwnedSetting::DebugInfo: field = "debug info"_str; break;
            case lito::CppOwnedSetting::Lto: field = "LTO"_str; break;
            }
            auto spelling = occurrence.raw_tokens.is_empty()
                                ? String::make("<structured compiler option>"_str)
                                : occurrence.raw_tokens[usize {}].clone();
            return Err(lito::CppOptionError::Message(
                rstd::format("{} arguments {}..{}: compiler option '{}' overrides Lito-owned {}",
                             occurrence.source.as_str(),
                             occurrence.range.begin,
                             occurrence.range.end,
                             spelling.as_str(),
                             field)));
        }
        return Ok(lito::CppOptionDelta {
            .argument   = rstd::move(occurrence.argument),
            .raw_tokens = rstd::move(occurrence.raw_tokens),
            .range      = occurrence.range,
            .source     = rstd::move(occurrence.source),
        });
    }
};

} // namespace rstd

namespace lito
{

template<typename Key, typename Value>
using BTreeMap = rstd::collections::BTreeMap<Key, Value>;

auto option_error(ref<str> message) -> CppOptionResult<CppCompileOptions> {
    return Err(CppOptionError::Message(String::make(message)));
}

auto option_error(String message) -> CppOptionResult<CppCompileOptions> {
    return Err(CppOptionError::Message(rstd::move(message)));
}

auto append_unique(Vec<String>& output, String value) -> void {
    for (const auto& existing : output) {
        if (existing.as_str() == value.as_str()) return;
    }
    output.push(rstd::move(value));
}

auto append_unique(Vec<CppIncludeDirectory>& output, CppIncludeDirectory value) -> void {
    for (const auto& existing : output) {
        if (existing.path.as_path() == value.path.as_path() && existing.kind == value.kind) return;
    }
    output.push(rstd::move(value));
}

auto set_warning(Vec<CppWarningOption>& output, CppWarningOption value) -> void {
    for (auto& existing : output) {
        if (existing.warning != value.warning) continue;
        existing.enabled = value.enabled;
        return;
    }
    output.push(rstd::move(value));
}

auto default_cpp_warnings() -> Vec<CppWarningOption> {
    auto result = Vec<CppWarningOption>::make();
    result.push(CppWarningOption { .warning = CppWarning::All });
    result.push(CppWarningOption { .warning = CppWarning::Pedantic });
    result.push(CppWarningOption {
        .warning = CppWarning::GnuStatementExpression,
        .enabled = false,
    });
    result.push(CppWarningOption {
        .warning = CppWarning::DeprecatedDeclarations,
        .enabled = false,
    });
    result.push(CppWarningOption { .warning = CppWarning::UnknownAttributes });
    return result;
}

auto family_map(const Vec<CppFamilyOption>& values) -> BTreeMap<String, String> {
    auto result = BTreeMap<String, String>::make();
    for (const auto& value : values) result.insert(value.family.clone(), value.value.clone());
    return result;
}

auto family_values(BTreeMap<String, String> values) -> Vec<CppFamilyOption> {
    auto result   = Vec<CppFamilyOption>::with_capacity(values.len());
    auto iterator = values.into_iter();
    while (auto value = iterator.next()) {
        auto entry = rstd::move(value).unwrap();
        result.push(CppFamilyOption {
            .family = rstd::move(entry.template get<0>()),
            .value  = rstd::move(entry.template get<1>()),
        });
    }
    return result;
}

auto macro_name(ref<str> value) -> ref<str> {
    auto separator = value.find("="_str);
    return separator.is_some() ? value.split_at(*separator).get<0>() : value;
}

auto macro_states(const Vec<CppMacroDirective>& values) -> BTreeMap<String, String> {
    auto result = BTreeMap<String, String>::make();
    for (const auto& value : values) {
        auto state =
            String::make(value.action == CppMacroAction::Define ? "define:"_str : "undefine:"_str);
        state.push_str(value.value.as_str());
        result.insert(String::make(macro_name(value.value.as_str())), rstd::move(state));
    }
    return result;
}

auto push_identity(String& output, ref<str> key, ref<str> value) -> void {
    output.push_str(key);
    output.push_ascii('=');
    output.push_str(rstd::format("{}:{}\n", value.len(), value).as_str());
}

auto push_identity(String& output, ref<str> key, bool value) -> void {
    push_identity(output, key, value ? "true"_str : "false"_str);
}

auto option_effect_name(CppVendorOptionEffect effect) -> ref<str> {
    switch (effect) {
    case CppVendorOptionEffect::Preprocessor: return "preprocessor"_str;
    case CppVendorOptionEffect::Language: return "language"_str;
    case CppVendorOptionEffect::Abi: return "abi"_str;
    case CppVendorOptionEffect::Codegen: return "codegen"_str;
    case CppVendorOptionEffect::Diagnostic: return "diagnostic"_str;
    case CppVendorOptionEffect::Unknown: return "unknown"_str;
    }
    return "unknown"_str;
}

auto append_semantic_identity(String& output, const CppCompileOptions& options) -> void {
    push_identity(output, "standard"_str, options.language.standard.as_str());
    push_identity(output, "exceptions"_str, options.language.exceptions);
    push_identity(output, "rtti"_str, options.language.rtti);
    push_identity(output,
                  "sized-deallocation"_str,
                  cpp_sized_deallocation_name(options.language.sized_deallocation));
    push_identity(output,
                  "stdlib"_str,
                  options.abi.standard_library == StandardLibrary::Libstdcxx ? "libstdc++"_str
                                                                             : "libc++"_str);
    for (const auto& value : options.language.modes) {
        push_identity(output,
                      rstd::format("language:{}", value.family.as_str()).as_str(),
                      value.value.as_str());
    }
    for (const auto& value : options.abi.modes) {
        push_identity(
            output, rstd::format("abi:{}", value.family.as_str()).as_str(), value.value.as_str());
    }
    if (options.target.target.is_some()) {
        push_identity(output, "target"_str, options.target.target->as_str());
    }
    if (options.target.sysroot.is_some()) {
        push_identity(output, "sysroot"_str, options.target.sysroot->as_str());
    }
    for (const auto& value : options.target.features) {
        push_identity(output,
                      rstd::format("target:{}", value.family.as_str()).as_str(),
                      value.value.as_str());
    }
}

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
                                const CppCompilerArgumentBinding* binding) -> CppCompilerArgument {
    if (binding == nullptr) {
        return CppCompilerArgument::Vendor(CppVendorOption {
            .value               = matched.raw_tokens[usize {}].clone(),
            .raw_tokens          = compiler_argument_raw_tokens(matched),
            .effect              = CppVendorOptionEffect::Unknown,
            .preserve_raw_tokens = true,
        });
    }
    if (binding->typed.is_some()) return as<Clone>(*binding->typed).clone();
    auto kind = binding->kind;
    switch (kind) {
    case CppCompilerArgumentKind::MacroDefine:
    case CppCompilerArgumentKind::MacroUndefine:
        return CppCompilerArgument::Macro(CppMacroDirective {
            .action = kind == CppCompilerArgumentKind::MacroDefine ? CppMacroAction::Define
                                                                   : CppMacroAction::Undefine,
            .value  = compiler_argument_value(matched).clone(),
        });
    case CppCompilerArgumentKind::IncludeDirectory:
    case CppCompilerArgumentKind::SystemIncludeDirectory:
        return CppCompilerArgument::IncludeDirectory(CppIncludeDirectory {
            .path = PathBuf::from(compiler_argument_value(matched).clone()),
            .kind = kind == CppCompilerArgumentKind::SystemIncludeDirectory
                        ? CppIncludeDirectoryKind::System
                        : CppIncludeDirectoryKind::User,
        });
    case CppCompilerArgumentKind::Target:
        return CppCompilerArgument::Target(compiler_argument_value(matched).clone());
    case CppCompilerArgumentKind::Sysroot:
        return CppCompilerArgument::Sysroot(compiler_argument_value(matched).clone());
    case CppCompilerArgumentKind::OwnedLanguageStandard:
    case CppCompilerArgumentKind::OwnedStandardLibrary:
    case CppCompilerArgumentKind::OwnedBmiRepresentation:
    case CppCompilerArgumentKind::OwnedRtti:
    case CppCompilerArgumentKind::OwnedExceptions:
    case CppCompilerArgumentKind::OwnedOptimization:
    case CppCompilerArgumentKind::OwnedDebugInfo:
    case CppCompilerArgumentKind::OwnedLto:
        return CppCompilerArgument::OwnedSetting(owned_setting(kind));
    case CppCompilerArgumentKind::LanguageMode:
    case CppCompilerArgumentKind::AbiMode:
    case CppCompilerArgumentKind::TargetMode:
    case CppCompilerArgumentKind::CodegenMode: {
        auto family = binding->family.clone();
        if (family.is_empty()) {
            family = dynamic_target_family(compiler_argument_value(matched).as_str());
        }
        return CppCompilerArgument::Family(
            family_domain(kind), rstd::move(family), canonical_argument(matched));
    }
    case CppCompilerArgumentKind::Instrumentation:
        return CppCompilerArgument::Instrumentation(canonical_argument(matched));
    case CppCompilerArgumentKind::Diagnostic:
        return CppCompilerArgument::Diagnostic(canonical_argument(matched));
    case CppCompilerArgumentKind::VendorLanguage:
        return CppCompilerArgument::Vendor(CppVendorOption {
            .value      = canonical_argument(matched),
            .raw_tokens = compiler_argument_raw_tokens(matched),
            .effect     = CppVendorOptionEffect::Language,
        });
    case CppCompilerArgumentKind::VendorCodegen:
        return CppCompilerArgument::Vendor(CppVendorOption {
            .value      = canonical_argument(matched),
            .raw_tokens = compiler_argument_raw_tokens(matched),
            .effect     = CppVendorOptionEffect::Codegen,
        });
    case CppCompilerArgumentKind::VendorPreprocessorUnsupported:
        return CppCompilerArgument::Vendor(CppVendorOption {
            .value                           = canonical_argument(matched),
            .raw_tokens                      = compiler_argument_raw_tokens(matched),
            .effect                          = CppVendorOptionEffect::Preprocessor,
            .native_preprocessor_unsupported = true,
        });
    }
    return CppCompilerArgument::Vendor(CppVendorOption {
        .value               = matched.raw_tokens[usize {}].clone(),
        .raw_tokens          = compiler_argument_raw_tokens(matched),
        .effect              = CppVendorOptionEffect::Unknown,
        .preserve_raw_tokens = true,
    });
}

} // namespace lito
