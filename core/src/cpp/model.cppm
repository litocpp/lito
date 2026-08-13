module;
#include <rstd/enum.hpp>
#include <rstd/macro.hpp>

export module lito.cpp:model;

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

enum class CppSizedDeallocation
{
    Auto,
    Enabled,
    Disabled,
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
    CppSizedDeallocation sized_deallocation { CppSizedDeallocation::Auto };
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
    bool                 position_independent_code { true };
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
              (PositionIndependentCode, (bool enabled;)),
              (SizedDeallocation, (CppSizedDeallocation value;)),
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

class CppOptionError {
    RSTD_ENUM(CppOptionError,
              (Argument, (CompilerArgumentError source;)),
              (Message, (String message;)))
};

template<typename T>
using CppOptionResult = Result<T, CppOptionError>;

struct CppCompilerArgumentBinding {
    CppCompilerArgumentKind     kind { CppCompilerArgumentKind::VendorLanguage };
    String                      family;
    Option<CppCompilerArgument> typed;
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

    auto add_typed(CppCompilerArgument argument, CompilerArgumentDefinition definition) -> void;

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

auto cpp_sized_deallocation_name(CppSizedDeallocation value) noexcept -> ref<str> {
    switch (value) {
    case CppSizedDeallocation::Auto: return "auto"_str;
    case CppSizedDeallocation::Enabled: return "enabled"_str;
    case CppSizedDeallocation::Disabled: return "disabled"_str;
    }
    return "auto"_str;
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
struct Impl<convert::From<lito::CompilerArgumentError>, lito::CppOptionError> {
    static auto from(lito::CompilerArgumentError error) -> lito::CppOptionError {
        return lito::CppOptionError::Argument(rstd::move(error));
    }
};

template<>
struct Impl<fmt::Display, lito::CppOptionError> : ImplBase<lito::CppOptionError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        const auto& error = this->self();
        if (error.is_Argument()) {
            return formatter.write_raw("C++ compiler argument is invalid",
                                       sizeof("C++ compiler argument is invalid") - 1);
        }
        return formatter.write_str(error.as_Message().message.as_str());
    }
};

template<>
struct Impl<fmt::Debug, lito::CppOptionError> : ImplBase<lito::CppOptionError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, lito::CppOptionError> : ImplBase<lito::CppOptionError> {
    auto source() const noexcept -> Option<error::ErrorRef> {
        const auto& error = this->self();
        if (! error.is_Argument()) return None();
        return Some(dyn<error::Error>::from_ref(error.as_Argument().source));
    }
};

} // namespace rstd
