module;
#include <rstd/enum.hpp>
#include <rstd/macro.hpp>

export module lito.cpp:compiler.option;

import rstd;
import :compiler.argument;
export import :compiler.error;
import lito.core;

using namespace rstd::prelude;
using namespace rstd::literals;

using PathBuf = rstd::path::PathBuf;

export namespace lito::cpp
{

enum class CppSizedDeallocation
{
    Auto,
    Enabled,
    Disabled,
};

enum class CppThreadingModel
{
    Posix,
};

enum class CppCompatibilityDomain
{
    Target,
    ImportClosure,
    ArtifactClosure,
    LinkClosure,
};

enum class CppRequirementMergePolicy
{
    Exact,
    Enable,
    SetUnion,
    Ordered,
};

struct CppRequirementApplications {
    bool preprocess { false };
    bool scan { false };
    bool bmi { false };
    bool compile { false };
    bool link { false };
};

struct CppRequirementSemantics {
    CppRequirementApplications applications;
    CppCompatibilityDomain      domain { CppCompatibilityDomain::Target };
    CppRequirementMergePolicy  merge { CppRequirementMergePolicy::Exact };
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
    Threading,
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

struct CppThreadingOptions {
    bool posix { false };
};

struct ResolvedStandardLibrary : DefaultInClass<ResolvedStandardLibrary, Clone> {
    StandardLibrary family { StandardLibrary::Libstdcxx };
    String          headers_identity;
    String          binary_identity;
    String          thread_backend;
    String          identity;

    auto clone() const -> ResolvedStandardLibrary {
        return ResolvedStandardLibrary {
            .family           = family,
            .headers_identity = headers_identity.clone(),
            .binary_identity  = binary_identity.clone(),
            .thread_backend   = thread_backend.clone(),
            .identity         = identity.clone(),
        };
    }
};

struct CppAbiOptions {
    StandardLibrary                 standard_library { StandardLibrary::Libstdcxx };
    Option<ResolvedStandardLibrary> resolved_standard_library;
    Vec<CppFamilyOption>            modes;
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
    CppThreadingOptions    threading;
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

    auto clone() const -> CppPublicRequirements {
        return CppPublicRequirements {
            .include_directories = as<Clone>(include_directories).clone(),
            .macros              = as<Clone>(macros).clone(),
        };
    }
};

class CppCompilerArgument : public DefaultInClass<CppCompilerArgument, Clone> {
    RSTD_ENUM(CppCompilerArgument,
              (Macro, (CppMacroDirective directive;)),
              (IncludeDirectory, (CppIncludeDirectory directory;)),
              (Target, (String value;)),
              (Sysroot, (String value;)),
              (OwnedSetting, (CppOwnedSetting setting;)),
              (Family, (CppOptionFamilyDomain domain; String family; String value;)),
              (Threading, (CppThreadingModel model;)),
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

struct CppOptionLayer {
    Vec<PathBuf>     include_directories;
    Vec<String>      definitions;
    CppArgumentLayer arguments;
};

inline auto CppMacroDirective::clone() const -> CppMacroDirective {
    return CppMacroDirective {
        .action = action,
        .value  = value.clone(),
    };
}

inline auto CppIncludeDirectory::clone() const -> CppIncludeDirectory {
    return CppIncludeDirectory {
        .path = path.clone(),
        .kind = kind,
    };
}

inline auto CppFamilyOption::clone() const -> CppFamilyOption {
    return CppFamilyOption {
        .family = family.clone(),
        .value  = value.clone(),
    };
}

inline auto CppVendorOption::clone() const -> CppVendorOption {
    return CppVendorOption {
        .value                           = value.clone(),
        .raw_tokens                      = as<Clone>(raw_tokens).clone(),
        .effect                          = effect,
        .native_preprocessor_unsupported = native_preprocessor_unsupported,
        .preserve_raw_tokens             = preserve_raw_tokens,
    };
}

inline auto CppCompilerArgument::clone() const -> CppCompilerArgument {
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
        RSTD_CASE(Threading, model) {
            return CppCompilerArgument::Threading(model);
        }
        RSTD_CASE(Instrumentation, value) {
            return CppCompilerArgument::Instrumentation(value.clone());
        }
        RSTD_CASE(PositionIndependentCode, enabled) {
            return CppCompilerArgument::PositionIndependentCode(enabled);
        }
        RSTD_CASE(SizedDeallocation, value) {
            return CppCompilerArgument::SizedDeallocation(value);
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

inline auto CppCompilerArgumentOccurrence::clone() const -> CppCompilerArgumentOccurrence {
    return CppCompilerArgumentOccurrence {
        .argument   = as<Clone>(argument).clone(),
        .raw_tokens = as<Clone>(raw_tokens).clone(),
        .range      = range,
        .source     = source.clone(),
    };
}

inline auto CppArgumentLayer::clone() const -> CppArgumentLayer {
    return CppArgumentLayer {
        .occurrences = as<Clone>(occurrences).clone(),
    };
}

inline auto CppCompileOptions::clone() const -> CppCompileOptions {
    const auto& input  = *this;
    auto        result = CppCompileOptions {
        .language =
            CppLanguageOptions {
                .standard           = input.language.standard.clone(),
                .exceptions         = input.language.exceptions,
                .rtti               = input.language.rtti,
                .sized_deallocation = input.language.sized_deallocation,
                .modes              = as<Clone>(input.language.modes).clone(),
            },
        .threading = input.threading,
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
                .optimization              = input.codegen.optimization,
                .debug_info                = input.codegen.debug_info,
                .lto                       = input.codegen.lto,
                .position_independent_code = input.codegen.position_independent_code,
                .modes                     = as<Clone>(input.codegen.modes).clone(),
                .instrumentation           = as<Clone>(input.codegen.instrumentation).clone(),
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
    if (input.abi.resolved_standard_library.is_some()) {
        result.abi.resolved_standard_library =
            Some(input.abi.resolved_standard_library->clone());
    }
    result.target.features = as<Clone>(input.target.features).clone();
    return result;
}

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

} // namespace lito::cpp
