module;
#include <rstd/enum.hpp>
#include <rstd/macro.hpp>

export module lito.cpp:compiler.common;

import rstd;
import lito.core;

using namespace rstd::prelude;

export namespace lito::compiler
{

enum class ThreadingModel
{
    None,
    Posix,
};

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
    manifest::Optimization optimization { manifest::Optimization::Default };
    manifest::DebugInfo    debug_info { manifest::DebugInfo::None };
    manifest::Lto          lto { manifest::Lto::Off };
    bool                   position_independent_code { true };
};

struct CommonCompileOptions {
    TargetOptions  target;
    CodegenOptions codegen;
    ThreadingModel threading { ThreadingModel::None };

    auto clone() const -> CommonCompileOptions {
        return CommonCompileOptions {
            .target    = target.clone(),
            .codegen   = codegen,
            .threading = threading,
        };
    }
};

class CommonCompilerArgument : public DefaultInClass<CommonCompilerArgument, Clone> {
    RSTD_ENUM(CommonCompilerArgument,
              (Target, (String value;)),
              (Sysroot, (String value;)),
              (Threading, (ThreadingModel model;)),
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
