module;
#include <rstd/enum.hpp>
#include <rstd/macro.hpp>

export module lito.cpp:compiler.common;

import rstd;
import lito.core;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::compiler
{

enum class ThreadingModel
{
    None,
    Posix,
};

enum class MicrosoftRuntimeLibrary
{
    Static,
    StaticDebug,
    Dynamic,
    DynamicDebug,
};

constexpr auto microsoft_runtime_library_name(MicrosoftRuntimeLibrary value) noexcept -> ref<str> {
    switch (value) {
    case MicrosoftRuntimeLibrary::Static: return "static"_str;
    case MicrosoftRuntimeLibrary::StaticDebug: return "static_dbg"_str;
    case MicrosoftRuntimeLibrary::Dynamic: return "dll"_str;
    case MicrosoftRuntimeLibrary::DynamicDebug: return "dll_dbg"_str;
    }
    return ""_str;
}

auto parse_microsoft_runtime_library(ref<str> value) noexcept -> Option<MicrosoftRuntimeLibrary> {
    if (value == microsoft_runtime_library_name(MicrosoftRuntimeLibrary::Static)) {
        return Some(MicrosoftRuntimeLibrary::Static);
    }
    if (value == microsoft_runtime_library_name(MicrosoftRuntimeLibrary::StaticDebug)) {
        return Some(MicrosoftRuntimeLibrary::StaticDebug);
    }
    if (value == microsoft_runtime_library_name(MicrosoftRuntimeLibrary::Dynamic)) {
        return Some(MicrosoftRuntimeLibrary::Dynamic);
    }
    if (value == microsoft_runtime_library_name(MicrosoftRuntimeLibrary::DynamicDebug)) {
        return Some(MicrosoftRuntimeLibrary::DynamicDebug);
    }
    return None();
}

constexpr auto microsoft_runtime_library_is_dynamic(MicrosoftRuntimeLibrary value) noexcept
    -> bool {
    return value == MicrosoftRuntimeLibrary::Dynamic ||
           value == MicrosoftRuntimeLibrary::DynamicDebug;
}

enum class CompilerWarning
{
    All,
    Pedantic,
    GnuStatementExpression,
    DeprecatedDeclarations,
    UnknownAttributes,
};

struct CompilerWarningOption {
    CompilerWarning warning { CompilerWarning::All };
    bool            enabled { true };
};

struct DiagnosticOptions {
    Vec<CompilerWarningOption> warnings;
    Vec<String>                options;

    auto clone() const -> DiagnosticOptions {
        return DiagnosticOptions {
            .warnings = warnings.clone(),
            .options  = as<Clone>(options).clone(),
        };
    }
};

struct TargetOptions {
    Option<String> target;
    Option<String> sysroot;

    auto clone() const -> TargetOptions {
        return TargetOptions {
            .target  = as<Clone>(target).clone(),
            .sysroot = as<Clone>(sysroot).clone(),
        };
    }
};

struct CodegenOptions {
    Option<manifest::Optimization> optimization;
    Option<manifest::DebugInfo>    debug_info;
    Option<manifest::Lto>          lto;
    bool                           position_independent_code { true };
};

class CodegenCompilerSetting : public DefaultInClass<CodegenCompilerSetting, Clone> {
    RSTD_ENUM(CodegenCompilerSetting,
              (Optimization, (manifest::Optimization value;)),
              (DebugInfo, (manifest::DebugInfo value;)),
              (Lto, (manifest::Lto value;)))

public:
    auto clone() const -> CodegenCompilerSetting {
        RSTD_MATCH(*this) {
            RSTD_CASE(Optimization, value) {
                return Optimization(value);
            }
            RSTD_CASE(DebugInfo, value) {
                return DebugInfo(value);
            }
            RSTD_CASE(Lto, value) {
                return Lto(value);
            }
        }
        rstd::unreachable();
    }
};

struct CommonCompileOptions {
    TargetOptions                   target;
    CodegenOptions                  codegen;
    ThreadingModel                  threading { ThreadingModel::None };
    Option<MicrosoftRuntimeLibrary> microsoft_runtime_library;

    auto clone() const -> CommonCompileOptions {
        return CommonCompileOptions {
            .target                    = target.clone(),
            .codegen                   = codegen,
            .threading                 = threading,
            .microsoft_runtime_library = microsoft_runtime_library,
        };
    }
};

class CommonCompilerArgument : public DefaultInClass<CommonCompilerArgument, Clone> {
    RSTD_ENUM(CommonCompilerArgument,
              (Target, (String value;)),
              (Sysroot, (String value;)),
              (Threading, (ThreadingModel model;)),
              (MicrosoftRuntime, (MicrosoftRuntimeLibrary library;)),
              (PositionIndependentCode, (bool enabled;)),
              (Warning, (CompilerWarningOption option;)))

public:
    auto clone() const -> CommonCompilerArgument {
        RSTD_MATCH(*this) {
            RSTD_CASE(Target, value) {
                return Target(value.clone());
            }
            RSTD_CASE(Sysroot, value) {
                return Sysroot(value.clone());
            }
            RSTD_CASE(Threading, model) {
                return Threading(model);
            }
            RSTD_CASE(MicrosoftRuntime, library) {
                return MicrosoftRuntime(library);
            }
            RSTD_CASE(PositionIndependentCode, enabled) {
                return PositionIndependentCode(enabled);
            }
            RSTD_CASE(Warning, option) {
                return Warning(option);
            }
        }
        rstd::unreachable();
    }
};

auto set_warning(DiagnosticOptions& output, CompilerWarningOption value) -> void {
    for (auto& existing : output.warnings) {
        if (existing.warning != value.warning) continue;
        existing.enabled = value.enabled;
        return;
    }
    output.warnings.push(rstd::move(value));
}

auto apply_common_compiler_argument(CommonCompileOptions&  options,
                                    DiagnosticOptions&     diagnostics,
                                    CommonCompilerArgument argument) -> void {
    RSTD_MATCH(rstd::move(argument)) {
        RSTD_CASE(Target, value) {
            options.target.target = Some(rstd::move(value));
        }
        RSTD_CASE(Sysroot, value) {
            options.target.sysroot = Some(rstd::move(value));
        }
        RSTD_CASE(Threading, model) {
            options.threading = model;
        }
        RSTD_CASE(MicrosoftRuntime, library) {
            options.microsoft_runtime_library = Some(library);
        }
        RSTD_CASE(PositionIndependentCode, enabled) {
            options.codegen.position_independent_code = enabled;
        }
        RSTD_CASE(Warning, option) {
            set_warning(diagnostics, option);
        }
    }
}

constexpr auto uses_posix_threads(const CommonCompileOptions& options) noexcept -> bool {
    return options.threading == ThreadingModel::Posix;
}

} // namespace lito::compiler
