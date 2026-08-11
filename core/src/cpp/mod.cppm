module;
#include <rstd/enum.hpp>
#include <rstd/macro.hpp>

export module lito.cpp;

export import :token;
export import :scanner;

import rstd;
import lito.compiler.arguments;

using namespace rstd::prelude;
using namespace rstd::literals;

using Clone   = rstd::clone::Clone;
using PathBuf = rstd::path::PathBuf;

export namespace lito
{

enum class StandardLibrary
{
    Libstdcxx,
    Libcxx,
};

enum class CppOptimization
{
    Default,
    None,
    Level1,
    Level2,
    Level3,
    Level4,
    Debug,
    Size,
    SizeMin,
    Fast,
};

enum class CppDebugInfo
{
    None,
    LineDirectivesOnly,
    LineTablesOnly,
    Limited,
    Full,
};

enum class CppLto
{
    Off,
    Thin,
    Fat,
};

enum class CppMacroAction
{
    Define,
    Undefine,
};

enum class CppIncludeDirectoryKind
{
    User,
    System,
};

enum class CppVendorOptionEffect
{
    Preprocessor,
    Language,
    Abi,
    Codegen,
    Diagnostic,
    Unknown,
};

enum class CppOwnedSetting
{
    LanguageStandard,
    StandardLibrary,
    BmiRepresentation,
    Rtti,
    Exceptions,
    Optimization,
    DebugInfo,
    Lto,
};

enum class CppOptionFamilyDomain
{
    Language,
    Abi,
    Target,
    Codegen,
};

enum class CppCompilerArgumentKind
{
    MacroDefine,
    MacroUndefine,
    IncludeDirectory,
    SystemIncludeDirectory,
    Target,
    Sysroot,
    OwnedLanguageStandard,
    OwnedStandardLibrary,
    OwnedBmiRepresentation,
    OwnedRtti,
    OwnedExceptions,
    OwnedOptimization,
    OwnedDebugInfo,
    OwnedLto,
    LanguageMode,
    AbiMode,
    TargetMode,
    CodegenMode,
    Instrumentation,
    Diagnostic,
    VendorLanguage,
    VendorCodegen,
    VendorPreprocessorUnsupported,
};

enum class CppWarning
{
    All,
    Pedantic,
    GnuStatementExpression,
    DeprecatedDeclarations,
    UnknownAttributes,
};

struct CppWarningOption {
    CppWarning warning { CppWarning::All };
    bool       enabled { true };
};

struct CppMacroDirective : DefaultInClass<CppMacroDirective, Clone> {
    CppMacroAction action { CppMacroAction::Define };
    String         value;

    auto clone() const -> CppMacroDirective;
};

struct CppIncludeDirectory : DefaultInClass<CppIncludeDirectory, Clone> {
    PathBuf                 path;
    CppIncludeDirectoryKind kind { CppIncludeDirectoryKind::User };

    auto clone() const -> CppIncludeDirectory;
};

struct CppFamilyOption : DefaultInClass<CppFamilyOption, Clone> {
    String family;
    String value;

    auto clone() const -> CppFamilyOption;
};

struct CppVendorOption : DefaultInClass<CppVendorOption, Clone> {
    String                value;
    Vec<String>           raw_tokens;
    CppVendorOptionEffect effect { CppVendorOptionEffect::Unknown };
    bool                  native_preprocessor_unsupported { false };
    bool                  preserve_raw_tokens { false };

    auto clone() const -> CppVendorOption;
};

struct CppLanguageOptions {
    String               standard;
    bool                 exceptions { false };
    bool                 rtti { false };
    Vec<CppFamilyOption> modes;
};

struct CppAbiOptions {
    StandardLibrary      standard_library { StandardLibrary::Libstdcxx };
    Vec<CppFamilyOption> modes;
};

struct CppTargetOptions {
    Option<String>       target;
    Option<String>       sysroot;
    Vec<CppFamilyOption> features;
};

struct CppPreprocessorOptions {
    Vec<CppIncludeDirectory> include_directories;
    Vec<CppMacroDirective>   macros;
};

struct CppCodegenOptions {
    CppOptimization      optimization { CppOptimization::Default };
    CppDebugInfo         debug_info { CppDebugInfo::None };
    CppLto               lto { CppLto::Off };
    Vec<CppFamilyOption> modes;
    Vec<String>          instrumentation;
};

struct CppDiagnosticOptions {
    Vec<CppWarningOption> warnings;
    Vec<String>           options;
};

struct CppCompileOptions : DefaultInClass<CppCompileOptions, Clone> {
    CppLanguageOptions     language;
    CppAbiOptions          abi;
    CppTargetOptions       target;
    CppPreprocessorOptions preprocessor;
    CppCodegenOptions      codegen;
    CppDiagnosticOptions   diagnostics;
    Vec<CppVendorOption>   vendor;

    auto clone() const -> CppCompileOptions;
};

struct CppPublicRequirements : DefaultInClass<CppPublicRequirements, Clone> {
    Vec<CppIncludeDirectory> include_directories;
    Vec<CppMacroDirective>   macros;

    auto clone() const -> CppPublicRequirements;
};

class CppCompilerArgument : public DefaultInClass<CppCompilerArgument, Clone> {
    RSTD_ENUM(CppCompilerArgument,
              (Macro, (CppMacroDirective directive;)),
              (IncludeDirectory, (CppIncludeDirectory directory;)),
              (Target, (String value;)),
              (Sysroot, (String value;)),
              (OwnedSetting, (CppOwnedSetting setting;)),
              (Family, (CppOptionFamilyDomain domain; String family; String value;)),
              (Instrumentation, (String value;)),
              (Warning, (CppWarningOption option;)),
              (Diagnostic, (String value;)),
              (Vendor, (CppVendorOption option;)))

public:
    auto clone() const -> CppCompilerArgument;
};

struct CppCompilerArgumentOccurrence : DefaultInClass<CppCompilerArgumentOccurrence, Clone> {
    CppCompilerArgument         argument;
    Vec<String>                 raw_tokens;
    CompilerArgumentSourceRange range;
    String                      source;

    auto clone() const -> CppCompilerArgumentOccurrence;
};

struct CppArgumentLayer : DefaultInClass<CppArgumentLayer, Clone> {
    Vec<CppCompilerArgumentOccurrence> occurrences;

    auto clone() const -> CppArgumentLayer;
};

struct CppOptionDelta {
    CppCompilerArgument         argument;
    Vec<String>                 raw_tokens;
    CompilerArgumentSourceRange range;
    String                      source;
};

template<typename T>
using CppOptionResult = Result<T, String>;

struct CppCompilerArgumentBinding {
    CppCompilerArgumentKind  kind { CppCompilerArgumentKind::VendorLanguage };
    String                   family;
    Option<CppWarningOption> warning;
};

class CppArgumentParser {
public:
    auto parse(const Vec<String>& arguments, ref<str> source) const
        -> CppOptionResult<CppArgumentLayer>;

private:
    friend class CppArgumentSchema;

    CppArgumentParser(CompilerArgumentParser parser, Vec<CppCompilerArgumentBinding> bindings)
        : parser_(rstd::move(parser)), bindings_(rstd::move(bindings)) {}

    CompilerArgumentParser          parser_;
    Vec<CppCompilerArgumentBinding> bindings_;
};

class CppArgumentSchema {
public:
    static auto make() -> CppArgumentSchema;

    auto add(CppCompilerArgumentKind    kind,
             CompilerArgumentDefinition definition,
             ref<str>                   family = {}) -> void;

    auto add_warning(CppWarningOption option, CompilerArgumentDefinition definition) -> void;

    auto build() && -> CppOptionResult<CppArgumentParser>;

private:
    CompilerArgumentSchema          schema_;
    Vec<CppCompilerArgumentBinding> bindings_;
};

struct CppOptionLayer {
    Vec<PathBuf>     include_directories;
    Vec<String>      definitions;
    CppArgumentLayer arguments;
};

auto is_supported_cpp_standard(ref<str> standard) noexcept -> bool {
    return standard == "c++20"_str || standard == "c++23"_str || standard == "c++26"_str ||
           standard == "c++2b"_str || standard == "c++2c"_str;
}

auto canonical_cpp_standard(ref<str> standard) noexcept -> ref<str> {
    if (standard == "c++2b"_str) return "c++23"_str;
    if (standard == "c++2c"_str) return "c++26"_str;
    return standard;
}

auto cpp_optimization_option(CppOptimization value) noexcept -> ref<str> {
    switch (value) {
    case CppOptimization::Default: return ""_str;
    case CppOptimization::None: return "-O0"_str;
    case CppOptimization::Level1: return "-O1"_str;
    case CppOptimization::Level2: return "-O2"_str;
    case CppOptimization::Level3: return "-O3"_str;
    case CppOptimization::Level4: return "-O4"_str;
    case CppOptimization::Debug: return "-Og"_str;
    case CppOptimization::Size: return "-Os"_str;
    case CppOptimization::SizeMin: return "-Oz"_str;
    case CppOptimization::Fast: return "-Ofast"_str;
    }
    return ""_str;
}

auto cpp_debug_option(CppDebugInfo value) noexcept -> ref<str> {
    switch (value) {
    case CppDebugInfo::None: return "-g0"_str;
    case CppDebugInfo::LineDirectivesOnly: return "-gline-directives-only"_str;
    case CppDebugInfo::LineTablesOnly: return "-gline-tables-only"_str;
    case CppDebugInfo::Limited: return "-g1"_str;
    case CppDebugInfo::Full: return "-g2"_str;
    }
    return "-g0"_str;
}

auto cpp_lto_option(CppLto value) noexcept -> ref<str> {
    switch (value) {
    case CppLto::Off: return "-fno-lto"_str;
    case CppLto::Thin: return "-flto=thin"_str;
    case CppLto::Fat: return "-flto=full"_str;
    }
    return "-fno-lto"_str;
}

auto cpp_warning_name(CppWarning value) noexcept -> ref<str> {
    switch (value) {
    case CppWarning::All: return "all"_str;
    case CppWarning::Pedantic: return "pedantic"_str;
    case CppWarning::GnuStatementExpression: return "gnu-statement-expression"_str;
    case CppWarning::DeprecatedDeclarations: return "deprecated-declarations"_str;
    case CppWarning::UnknownAttributes: return "unknown-attributes"_str;
    }
    return "unknown"_str;
}

auto cpp_public_requirements(const CppCompileOptions& input) -> CppPublicRequirements;

auto merge_cpp_public_requirements(CppPublicRequirements input, const CppPublicRequirements& extra)
    -> CppPublicRequirements;

auto make_cpp_options(ref<str>        language_standard,
                      StandardLibrary standard_library,
                      bool            exceptions,
                      bool            rtti,
                      CppOptimization optimization,
                      CppDebugInfo    debug_info,
                      CppOptionLayer  layer = {}) -> CppOptionResult<CppCompileOptions>;

auto apply_cpp_option_layer(CppCompileOptions input, CppOptionLayer layer)
    -> CppOptionResult<CppCompileOptions>;

auto merge_cpp_options(CppCompileOptions input, const CppCompileOptions& extra)
    -> CppOptionResult<CppCompileOptions>;

auto cpp_compile_identity(const CppCompileOptions& options) -> String;

auto cpp_scan_identity(const CppCompileOptions& options) -> String;

auto cpp_bmi_compatibility_identity(const CppCompileOptions& options) -> String;

auto cpp_public_requirements_identity(const CppPublicRequirements& requirements) -> String;

auto cpp_public_requirements_satisfied(const CppPublicRequirements& requirements,
                                       const CppCompileOptions&     consumer) -> bool;

} // namespace lito

export namespace rstd
{

template<>
struct Impl<convert::TryFrom<lito::CppCompilerArgumentOccurrence>, lito::CppOptionDelta> {
    using Error = String;

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
            return Err(
                rstd::format("{} arguments {}..{}: compiler option '{}' overrides Lito-owned {}",
                             occurrence.source.as_str(),
                             occurrence.range.begin,
                             occurrence.range.end,
                             spelling.as_str(),
                             field));
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
    return Err(String::make(message));
}

auto option_error(String message) -> CppOptionResult<CppCompileOptions> {
    return Err(rstd::move(message));
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
    if (binding->warning.is_some()) return CppCompilerArgument::Warning(*binding->warning);
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

export namespace lito
{

auto CppArgumentSchema::make() -> CppArgumentSchema {
    auto result      = CppArgumentSchema {};
    result.schema_   = CompilerArgumentSchema::make();
    result.bindings_ = Vec<CppCompilerArgumentBinding>::make();
    return result;
}

auto CppArgumentSchema::add(CppCompilerArgumentKind    kind,
                            CompilerArgumentDefinition definition,
                            ref<str>                   family) -> void {
    schema_.add(rstd::move(definition));
    bindings_.push(CppCompilerArgumentBinding {
        .kind   = kind,
        .family = String::make(family),
    });
}

auto CppArgumentSchema::add_warning(CppWarningOption option, CompilerArgumentDefinition definition)
    -> void {
    schema_.add(rstd::move(definition));
    bindings_.push(CppCompilerArgumentBinding {
        .kind    = CppCompilerArgumentKind::Diagnostic,
        .warning = Some(option),
    });
}

auto CppArgumentSchema::build() && -> CppOptionResult<CppArgumentParser> {
    auto parser = rstd_try(rstd::move(schema_).build(), [](CompilerArgumentError error) {
        return compiler_argument_error_message(error);
    });
    return Ok(CppArgumentParser(rstd::move(parser), rstd::move(bindings_)));
}

auto CppArgumentParser::parse(const Vec<String>& arguments, ref<str> source) const
    -> CppOptionResult<CppArgumentLayer> {
    auto parsed = rstd_try(parser_.parse(arguments), [](CompilerArgumentError error) {
        return compiler_argument_error_message(error);
    });
    auto result = CppArgumentLayer {};
    for (auto& matched : parsed) {
        auto binding = matched.definition.is_some()
                           ? rstd::addressof(bindings_[*matched.definition])
                           : static_cast<const CppCompilerArgumentBinding*>(nullptr);
        result.occurrences.push(CppCompilerArgumentOccurrence {
            .argument   = make_cpp_compiler_argument(matched, binding),
            .raw_tokens = rstd::move(matched.raw_tokens),
            .range      = matched.range,
            .source     = String::make(source),
        });
    }
    return Ok(rstd::move(result));
}

auto CppMacroDirective::clone() const -> CppMacroDirective {
    return CppMacroDirective {
        .action = action,
        .value  = value.clone(),
    };
}

auto CppIncludeDirectory::clone() const -> CppIncludeDirectory {
    return CppIncludeDirectory {
        .path = path.clone(),
        .kind = kind,
    };
}

auto CppFamilyOption::clone() const -> CppFamilyOption {
    return CppFamilyOption {
        .family = family.clone(),
        .value  = value.clone(),
    };
}

auto CppVendorOption::clone() const -> CppVendorOption {
    return CppVendorOption {
        .value                           = value.clone(),
        .raw_tokens                      = as<Clone>(raw_tokens).clone(),
        .effect                          = effect,
        .native_preprocessor_unsupported = native_preprocessor_unsupported,
        .preserve_raw_tokens             = preserve_raw_tokens,
    };
}

auto CppCompilerArgument::clone() const -> CppCompilerArgument {
    RSTD_MATCH(*this) {
        RSTD_CASE(Macro, directive) {
            return CppCompilerArgument::Macro(CppMacroDirective {
                .action = directive.action,
                .value  = directive.value.clone(),
            });
        }
        RSTD_CASE(IncludeDirectory, directory) {
            return CppCompilerArgument::IncludeDirectory(as<Clone>(directory).clone());
        }
        RSTD_CASE(Target, value) {
            return CppCompilerArgument::Target(value.clone());
        }
        RSTD_CASE(Sysroot, value) {
            return CppCompilerArgument::Sysroot(value.clone());
        }
        RSTD_CASE(OwnedSetting, setting) {
            return CppCompilerArgument::OwnedSetting(setting);
        }
        RSTD_CASE(Family, domain, family, value) {
            return CppCompilerArgument::Family(domain, family.clone(), value.clone());
        }
        RSTD_CASE(Instrumentation, value) {
            return CppCompilerArgument::Instrumentation(value.clone());
        }
        RSTD_CASE(Warning, option) {
            return CppCompilerArgument::Warning(option);
        }
        RSTD_CASE(Diagnostic, value) {
            return CppCompilerArgument::Diagnostic(value.clone());
        }
        RSTD_CASE(Vendor, option) {
            return CppCompilerArgument::Vendor(as<Clone>(option).clone());
        }
    }
    rstd::unreachable();
}

auto CppCompilerArgumentOccurrence::clone() const -> CppCompilerArgumentOccurrence {
    return CppCompilerArgumentOccurrence {
        .argument   = as<Clone>(argument).clone(),
        .raw_tokens = as<Clone>(raw_tokens).clone(),
        .range      = range,
        .source     = source.clone(),
    };
}

auto CppArgumentLayer::clone() const -> CppArgumentLayer {
    return CppArgumentLayer {
        .occurrences = as<Clone>(occurrences).clone(),
    };
}

auto CppCompileOptions::clone() const -> CppCompileOptions {
    const auto& input  = *this;
    auto        result = CppCompileOptions {
        .language =
            CppLanguageOptions {
                .standard   = input.language.standard.clone(),
                .exceptions = input.language.exceptions,
                .rtti       = input.language.rtti,
                .modes      = as<Clone>(input.language.modes).clone(),
            },
        .abi =
            CppAbiOptions {
                .standard_library = input.abi.standard_library,
                .modes            = as<Clone>(input.abi.modes).clone(),
            },
        .preprocessor =
            CppPreprocessorOptions {
                .include_directories = as<Clone>(input.preprocessor.include_directories).clone(),
                .macros              = as<Clone>(input.preprocessor.macros).clone(),
            },
        .codegen =
            CppCodegenOptions {
                .optimization    = input.codegen.optimization,
                .debug_info      = input.codegen.debug_info,
                .lto             = input.codegen.lto,
                .modes           = as<Clone>(input.codegen.modes).clone(),
                .instrumentation = as<Clone>(input.codegen.instrumentation).clone(),
            },
        .diagnostics =
            CppDiagnosticOptions {
                .warnings = input.diagnostics.warnings.clone(),
                .options  = as<Clone>(input.diagnostics.options).clone(),
            },
        .vendor = as<Clone>(input.vendor).clone(),
    };
    if (input.target.target.is_some()) result.target.target = Some(input.target.target->clone());
    if (input.target.sysroot.is_some()) {
        result.target.sysroot = Some(input.target.sysroot->clone());
    }
    result.target.features = as<Clone>(input.target.features).clone();
    return result;
}

auto cpp_public_requirements(const CppCompileOptions& input) -> CppPublicRequirements {
    return CppPublicRequirements {
        .include_directories = as<Clone>(input.preprocessor.include_directories).clone(),
        .macros              = as<Clone>(input.preprocessor.macros).clone(),
    };
}

auto CppPublicRequirements::clone() const -> CppPublicRequirements {
    const auto& input = *this;
    return CppPublicRequirements {
        .include_directories = as<Clone>(input.include_directories).clone(),
        .macros              = as<Clone>(input.macros).clone(),
    };
}

auto merge_cpp_public_requirements(CppPublicRequirements input, const CppPublicRequirements& extra)
    -> CppPublicRequirements {
    for (const auto& include : extra.include_directories) {
        append_unique(input.include_directories, include.clone());
    }
    for (const auto& macro : extra.macros) {
        input.macros.push(CppMacroDirective {
            .action = macro.action,
            .value  = macro.value.clone(),
        });
    }
    return input;
}

auto make_cpp_options(ref<str>        language_standard,
                      StandardLibrary standard_library,
                      bool            exceptions,
                      bool            rtti,
                      CppOptimization optimization_value,
                      CppDebugInfo    debug_info,
                      CppOptionLayer  layer) -> CppOptionResult<CppCompileOptions> {
    auto result = CppCompileOptions {
        .language =
            CppLanguageOptions {
                .standard   = String::make(canonical_cpp_standard(language_standard)),
                .exceptions = exceptions,
                .rtti       = rtti,
            },
        .abi =
            CppAbiOptions {
                .standard_library = standard_library,
            },
        .codegen =
            CppCodegenOptions {
                .optimization = optimization_value,
                .debug_info   = debug_info,
            },
        .diagnostics =
            CppDiagnosticOptions {
                .warnings = default_cpp_warnings(),
            },
    };
    return apply_cpp_option_layer(rstd::move(result), rstd::move(layer));
}

auto apply_cpp_option_layer(CppCompileOptions input, CppOptionLayer layer)
    -> CppOptionResult<CppCompileOptions> {
    for (auto& include : layer.include_directories) {
        append_unique(input.preprocessor.include_directories,
                      CppIncludeDirectory {
                          .path = rstd::move(include),
                          .kind = CppIncludeDirectoryKind::User,
                      });
    }
    for (auto& definition : layer.definitions) {
        input.preprocessor.macros.push(CppMacroDirective {
            .action = CppMacroAction::Define,
            .value  = rstd::move(definition),
        });
    }

    auto language_modes  = family_map(input.language.modes);
    auto abi_modes       = family_map(input.abi.modes);
    auto target_modes    = family_map(input.target.features);
    auto codegen_modes   = family_map(input.codegen.modes);
    auto instrumentation = BTreeMap<String, empty>::make();
    for (const auto& value : input.codegen.instrumentation) {
        instrumentation.insert(value.clone(), empty {});
    }

    for (auto& occurrence : layer.arguments.occurrences) {
        auto delta = rstd_try(rstd::try_from<CppOptionDelta>(rstd::move(occurrence)));
        RSTD_MATCH(rstd::move(delta.argument)) {
            RSTD_CASE(Macro, directive) {
                input.preprocessor.macros.push(rstd::move(directive));
            }
            RSTD_CASE(IncludeDirectory, directory) {
                append_unique(input.preprocessor.include_directories, rstd::move(directory));
            }
            RSTD_CASE(Target, value) {
                input.target.target = Some(rstd::move(value));
            }
            RSTD_CASE(Sysroot, value) {
                input.target.sysroot = Some(rstd::move(value));
            }
            RSTD_CASE(OwnedSetting, setting) {
                static_cast<void>(setting);
                return option_error("invalid prevalidated Lito-owned compiler option"_str);
            }
            RSTD_CASE(Family, domain, family, value) {
                auto* modes = rstd::addressof(language_modes);
                switch (domain) {
                case CppOptionFamilyDomain::Language: break;
                case CppOptionFamilyDomain::Abi: modes = rstd::addressof(abi_modes); break;
                case CppOptionFamilyDomain::Target: modes = rstd::addressof(target_modes); break;
                case CppOptionFamilyDomain::Codegen: modes = rstd::addressof(codegen_modes); break;
                }
                modes->insert(rstd::move(family), rstd::move(value));
            }
            RSTD_CASE(Instrumentation, value) {
                instrumentation.insert(rstd::move(value), empty {});
            }
            RSTD_CASE(Warning, option) {
                set_warning(input.diagnostics.warnings, option);
            }
            RSTD_CASE(Diagnostic, value) {
                append_unique(input.diagnostics.options, rstd::move(value));
            }
            RSTD_CASE(Vendor, option) {
                input.vendor.push(rstd::move(option));
            }
        }
    }

    input.language.modes  = family_values(rstd::move(language_modes));
    input.abi.modes       = family_values(rstd::move(abi_modes));
    input.target.features = family_values(rstd::move(target_modes));
    input.codegen.modes   = family_values(rstd::move(codegen_modes));
    input.codegen.instrumentation.clear();
    auto instrumentation_values = instrumentation.into_iter();
    while (auto value = instrumentation_values.next()) {
        auto entry = rstd::move(value).unwrap();
        input.codegen.instrumentation.push(rstd::move(entry.template get<0>()));
    }
    return Ok(rstd::move(input));
}

auto merge_cpp_options(CppCompileOptions input, const CppCompileOptions& extra)
    -> CppOptionResult<CppCompileOptions> {
    if (cpp_bmi_compatibility_identity(input).as_str() !=
        cpp_bmi_compatibility_identity(extra).as_str()) {
        return option_error("cannot merge C++ contexts with different semantic or ABI options"_str);
    }
    auto layer = CppOptionLayer {};
    for (const auto& include : extra.preprocessor.include_directories) {
        layer.arguments.occurrences.push(CppCompilerArgumentOccurrence {
            .argument   = CppCompilerArgument::IncludeDirectory(as<Clone>(include).clone()),
            .raw_tokens = Vec<String>::make(),
            .source     = String::make("merged C++ context"_str),
        });
    }
    for (const auto& macro : extra.preprocessor.macros) {
        if (macro.action == CppMacroAction::Define) {
            layer.definitions.push(macro.value.clone());
        } else {
            layer.arguments.occurrences.push(CppCompilerArgumentOccurrence {
                .argument = CppCompilerArgument::Macro(CppMacroDirective {
                    .action = CppMacroAction::Undefine,
                    .value  = macro.value.clone(),
                }),
            });
        }
    }
    for (const auto& value : extra.language.modes) {
        layer.arguments.occurrences.push(CppCompilerArgumentOccurrence {
            .argument = CppCompilerArgument::Family(
                CppOptionFamilyDomain::Language, value.family.clone(), value.value.clone()),
        });
    }
    for (const auto& value : extra.abi.modes) {
        layer.arguments.occurrences.push(CppCompilerArgumentOccurrence {
            .argument = CppCompilerArgument::Family(
                CppOptionFamilyDomain::Abi, value.family.clone(), value.value.clone()),
        });
    }
    for (const auto& value : extra.target.features) {
        layer.arguments.occurrences.push(CppCompilerArgumentOccurrence {
            .argument = CppCompilerArgument::Family(
                CppOptionFamilyDomain::Target, value.family.clone(), value.value.clone()),
        });
    }
    for (const auto& value : extra.codegen.modes) {
        layer.arguments.occurrences.push(CppCompilerArgumentOccurrence {
            .argument = CppCompilerArgument::Family(
                CppOptionFamilyDomain::Codegen, value.family.clone(), value.value.clone()),
        });
    }
    for (const auto& value : extra.codegen.instrumentation) {
        layer.arguments.occurrences.push(CppCompilerArgumentOccurrence {
            .argument = CppCompilerArgument::Instrumentation(value.clone()),
        });
    }
    for (const auto& value : extra.diagnostics.warnings) {
        layer.arguments.occurrences.push(CppCompilerArgumentOccurrence {
            .argument = CppCompilerArgument::Warning(value),
        });
    }
    for (const auto& value : extra.diagnostics.options) {
        layer.arguments.occurrences.push(CppCompilerArgumentOccurrence {
            .argument = CppCompilerArgument::Diagnostic(value.clone()),
        });
    }
    for (const auto& value : extra.vendor) {
        layer.arguments.occurrences.push(CppCompilerArgumentOccurrence {
            .argument = CppCompilerArgument::Vendor(as<Clone>(value).clone()),
        });
    }
    return apply_cpp_option_layer(rstd::move(input), rstd::move(layer));
}

auto cpp_compile_identity(const CppCompileOptions& options) -> String {
    auto result = String::make("lito-cpp-compile-context-v1\n"_str);
    append_semantic_identity(result, options);
    push_identity(
        result, "optimization"_str, cpp_optimization_option(options.codegen.optimization));
    push_identity(result, "debug-info"_str, cpp_debug_option(options.codegen.debug_info));
    push_identity(result, "lto"_str, cpp_lto_option(options.codegen.lto));
    for (const auto& value : options.codegen.modes) {
        push_identity(result,
                      rstd::format("codegen:{}", value.family.as_str()).as_str(),
                      value.value.as_str());
    }
    for (const auto& value : options.codegen.instrumentation) {
        push_identity(result, "instrumentation"_str, value.as_str());
    }
    for (const auto& value : options.diagnostics.warnings) {
        push_identity(result,
                      rstd::format("warning:{}", cpp_warning_name(value.warning)).as_str(),
                      value.enabled ? "enabled"_str : "disabled"_str);
    }
    for (const auto& include : options.preprocessor.include_directories) {
        auto text = include.path.as_path().to_str();
        push_identity(result,
                      include.kind == CppIncludeDirectoryKind::System ? "system-include"_str
                                                                      : "include"_str,
                      text.is_some() ? *text : "<non-utf8>"_str);
    }
    for (const auto& macro : options.preprocessor.macros) {
        push_identity(result,
                      macro.action == CppMacroAction::Define ? "define"_str : "undefine"_str,
                      macro.value.as_str());
    }
    for (const auto& value : options.diagnostics.options) {
        push_identity(result, "diagnostic"_str, value.as_str());
    }
    for (const auto& value : options.vendor) {
        push_identity(result, option_effect_name(value.effect), value.value.as_str());
    }
    return result;
}

auto cpp_scan_identity(const CppCompileOptions& options) -> String {
    auto result = String::make("lito-cpp-scan-context-v1\n"_str);
    append_semantic_identity(result, options);
    push_identity(
        result, "optimization"_str, cpp_optimization_option(options.codegen.optimization));
    for (const auto& value : options.codegen.modes) {
        push_identity(result,
                      rstd::format("codegen:{}", value.family.as_str()).as_str(),
                      value.value.as_str());
    }
    for (const auto& value : options.codegen.instrumentation) {
        push_identity(result, "instrumentation"_str, value.as_str());
    }
    for (const auto& include : options.preprocessor.include_directories) {
        auto text = include.path.as_path().to_str();
        push_identity(result,
                      include.kind == CppIncludeDirectoryKind::System ? "system-include"_str
                                                                      : "include"_str,
                      text.is_some() ? *text : "<non-utf8>"_str);
    }
    for (const auto& macro : options.preprocessor.macros) {
        push_identity(result,
                      macro.action == CppMacroAction::Define ? "define"_str : "undefine"_str,
                      macro.value.as_str());
    }
    for (const auto& value : options.vendor) {
        if (value.effect == CppVendorOptionEffect::Codegen ||
            value.effect == CppVendorOptionEffect::Diagnostic) {
            continue;
        }
        push_identity(result, option_effect_name(value.effect), value.value.as_str());
    }
    return result;
}

auto cpp_bmi_compatibility_identity(const CppCompileOptions& options) -> String {
    auto result = String::make("lito-cpp-bmi-compatibility-v1\n"_str);
    append_semantic_identity(result, options);
    for (const auto& value : options.vendor) {
        if (value.effect == CppVendorOptionEffect::Language ||
            value.effect == CppVendorOptionEffect::Abi ||
            value.effect == CppVendorOptionEffect::Preprocessor ||
            value.effect == CppVendorOptionEffect::Unknown) {
            push_identity(result, option_effect_name(value.effect), value.value.as_str());
        }
    }
    return result;
}

auto cpp_public_requirements_identity(const CppPublicRequirements& requirements) -> String {
    auto result = String::make("lito-cpp-public-requirements-v1\n"_str);
    for (const auto& include : requirements.include_directories) {
        auto text = include.path.as_path().to_str();
        push_identity(result,
                      include.kind == CppIncludeDirectoryKind::System ? "system-include"_str
                                                                      : "include"_str,
                      text.is_some() ? *text : "<non-utf8>"_str);
    }
    for (const auto& macro : requirements.macros) {
        push_identity(result,
                      macro.action == CppMacroAction::Define ? "define"_str : "undefine"_str,
                      macro.value.as_str());
    }
    return result;
}

auto cpp_public_requirements_satisfied(const CppPublicRequirements& requirements,
                                       const CppCompileOptions&     consumer) -> bool {
    for (const auto& required : requirements.include_directories) {
        auto available = false;
        for (const auto& include : consumer.preprocessor.include_directories) {
            if (include.path.as_path() == required.path.as_path() &&
                include.kind == required.kind) {
                available = true;
                break;
            }
        }
        if (! available) return false;
    }

    auto required_macros = macro_states(requirements.macros);
    auto consumer_macros = macro_states(consumer.preprocessor.macros);
    auto values          = required_macros.into_iter();
    while (auto value = values.next()) {
        auto entry    = rstd::move(value).unwrap();
        auto consumer = consumer_macros.get(entry.template get<0>().as_str());
        if (consumer.is_none() || (**consumer).as_str() != entry.template get<1>().as_str()) {
            return false;
        }
    }
    return true;
}

} // namespace lito
