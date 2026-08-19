module;
#include <rstd/enum.hpp>
#include <rstd/macro.hpp>

export module lito.cpp:c.compiler;

import rstd;
import lito.core;
import :compiler.argument;
import :compiler.common;
import :compiler.error;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::c
{

enum class CMacroAction
{
    Define,
    Undefine,
};

enum class CIncludeDirectoryKind
{
    User,
    System,
};

struct CMacroDirective {
    CMacroAction action { CMacroAction::Define };
    String       value;

    auto clone() const -> CMacroDirective {
        return CMacroDirective { .action = action, .value = value.clone() };
    }
};

struct CIncludeDirectory {
    PathBuf               path;
    CIncludeDirectoryKind kind { CIncludeDirectoryKind::User };

    auto clone() const -> CIncludeDirectory {
        return CIncludeDirectory { .path = path.clone(), .kind = kind };
    }
};

enum class CVendorOptionEffect
{
    Preprocessor,
    Language,
    Abi,
    Target,
    Codegen,
    Diagnostic,
    Unknown,
};

struct CVendorOption {
    String              value;
    Vec<String>         raw_tokens;
    CVendorOptionEffect effect { CVendorOptionEffect::Unknown };
    bool                native_preprocessor_unsupported { false };
    bool                preserve_raw_tokens { false };

    auto clone() const -> CVendorOption {
        return CVendorOption {
            .value                           = value.clone(),
            .raw_tokens                      = as<Clone>(raw_tokens).clone(),
            .effect                          = effect,
            .native_preprocessor_unsupported = native_preprocessor_unsupported,
            .preserve_raw_tokens             = preserve_raw_tokens,
        };
    }
};

struct CCompileOptions {
    lito::compiler::CommonCompileOptions common;
    lito::manifest::CStandard            standard { lito::manifest::CStandard::C99 };
    Vec<CIncludeDirectory>               include_directories;
    Vec<CMacroDirective>                 macros;
    lito::compiler::DiagnosticOptions    diagnostics;
    Vec<CVendorOption>                   vendor;

    auto clone() const -> CCompileOptions {
        auto includes = Vec<CIncludeDirectory>::with_capacity(include_directories.len());
        for (const auto& include : include_directories) includes.push(include.clone());
        auto copied_macros = Vec<CMacroDirective>::with_capacity(macros.len());
        for (const auto& macro : macros) copied_macros.push(macro.clone());
        auto copied_vendor = Vec<CVendorOption>::with_capacity(vendor.len());
        for (const auto& option : vendor) copied_vendor.push(option.clone());
        return CCompileOptions {
            .common              = common.clone(),
            .standard            = standard,
            .include_directories = rstd::move(includes),
            .macros              = rstd::move(copied_macros),
            .diagnostics         = diagnostics.clone(),
            .vendor              = rstd::move(copied_vendor),
        };
    }
};

struct CPublicRequirements {
    Vec<CIncludeDirectory> include_directories;
    Vec<CMacroDirective>   macros;

    auto clone() const -> CPublicRequirements {
        auto includes = Vec<CIncludeDirectory>::with_capacity(include_directories.len());
        for (const auto& include : include_directories) includes.push(include.clone());
        auto copied_macros = Vec<CMacroDirective>::with_capacity(macros.len());
        for (const auto& macro : macros) copied_macros.push(macro.clone());
        return CPublicRequirements {
            .include_directories = rstd::move(includes),
            .macros              = rstd::move(copied_macros),
        };
    }
};

class CCompilerArgument : public DefaultInClass<CCompilerArgument, Clone> {
    RSTD_ENUM(CCompilerArgument,
              (Macro, (CMacroDirective directive;)),
              (IncludeDirectory, (CIncludeDirectory directory;)),
              (Common, (lito::compiler::CommonCompilerArgument argument;)),
              (CodegenSetting, (lito::compiler::CodegenCompilerSetting setting;)),
              (Diagnostic, (String value;)),
              (Vendor, (CVendorOption option;)))

public:
    auto clone() const -> CCompilerArgument {
        RSTD_MATCH(*this) {
            RSTD_CASE(Macro, directive) {
                return Macro(directive.clone());
            }
            RSTD_CASE(IncludeDirectory, directory) {
                return IncludeDirectory(directory.clone());
            }
            RSTD_CASE(Common, argument) {
                return Common(as<Clone>(argument).clone());
            }
            RSTD_CASE(CodegenSetting, setting) {
                return CodegenSetting(as<Clone>(setting).clone());
            }
            RSTD_CASE(Diagnostic, value) {
                return Diagnostic(value.clone());
            }
            RSTD_CASE(Vendor, option) {
                return Vendor(option.clone());
            }
        }
        rstd::unreachable();
    }
};

struct CCompilerArgumentOccurrence {
    CCompilerArgument                      argument;
    Vec<String>                            raw_tokens;
    lito::cpp::CompilerArgumentSourceRange range;
    String                                 source;

    auto clone() const -> CCompilerArgumentOccurrence {
        return CCompilerArgumentOccurrence {
            .argument   = as<Clone>(argument).clone(),
            .raw_tokens = as<Clone>(raw_tokens).clone(),
            .range      = range,
            .source     = source.clone(),
        };
    }
};

struct CArgumentLayer {
    Vec<PathBuf>                     include_directories;
    Vec<String>                      definitions;
    Vec<CCompilerArgumentOccurrence> occurrences;

    auto clone() const -> CArgumentLayer {
        auto copied_occurrences =
            Vec<CCompilerArgumentOccurrence>::with_capacity(occurrences.len());
        for (const auto& occurrence : occurrences) {
            copied_occurrences.push(occurrence.clone());
        }
        return CArgumentLayer {
            .include_directories = as<Clone>(include_directories).clone(),
            .definitions         = as<Clone>(definitions).clone(),
            .occurrences         = rstd::move(copied_occurrences),
        };
    }
};

auto default_c_warnings() -> Vec<lito::compiler::CompilerWarningOption> {
    auto result = Vec<lito::compiler::CompilerWarningOption>::make();
    result.push(lito::compiler::CompilerWarningOption {
        .warning = lito::compiler::CompilerWarning::All,
    });
    result.push(lito::compiler::CompilerWarningOption {
        .warning = lito::compiler::CompilerWarning::Pedantic,
    });
    result.push(lito::compiler::CompilerWarningOption {
        .warning = lito::compiler::CompilerWarning::UnknownAttributes,
    });
    return result;
}

auto make_c_options(lito::compiler::CommonCompileOptions common, lito::manifest::CStandard standard)
    -> CCompileOptions {
    return CCompileOptions {
        .common   = rstd::move(common),
        .standard = standard,
        .diagnostics =
            lito::compiler::DiagnosticOptions {
                .warnings = default_c_warnings(),
            },
    };
}

auto apply_c_option_layer(CCompileOptions options, CArgumentLayer layer)
    -> lito::cpp::CompilerOptionResult<CCompileOptions> {
    for (auto& include : layer.include_directories) {
        auto repeated = false;
        for (const auto& existing : options.include_directories) {
            if (existing.kind == CIncludeDirectoryKind::User &&
                existing.path.as_path() == include.as_path()) {
                repeated = true;
                break;
            }
        }
        if (! repeated) {
            options.include_directories.push(CIncludeDirectory {
                .path = rstd::move(include),
            });
        }
    }
    for (auto& definition : layer.definitions) {
        auto repeated = false;
        for (const auto& existing : options.macros) {
            if (existing.action == CMacroAction::Define && existing.value == definition.as_str()) {
                repeated = true;
                break;
            }
        }
        if (! repeated) {
            options.macros.push(CMacroDirective {
                .value = rstd::move(definition),
            });
        }
    }
    for (auto& occurrence : layer.occurrences) {
        RSTD_MATCH(rstd::move(occurrence.argument)) {
            RSTD_CASE(Macro, directive) {
                options.macros.push(rstd::move(directive));
            }
            RSTD_CASE(IncludeDirectory, directory) {
                auto repeated = false;
                for (const auto& existing : options.include_directories) {
                    if (existing.kind == directory.kind &&
                        existing.path.as_path() == directory.path.as_path()) {
                        repeated = true;
                        break;
                    }
                }
                if (! repeated) options.include_directories.push(rstd::move(directory));
            }
            RSTD_CASE(Common, argument) {
                lito::compiler::apply_common_compiler_argument(
                    options.common, options.diagnostics, rstd::move(argument));
            }
            RSTD_CASE(CodegenSetting, setting) {
                static_cast<void>(setting);
                return Err(lito::cpp::CompilerOptionError::Message(
                    String::make("compiler option overrides a Lito-owned codegen setting"_str)));
            }
            RSTD_CASE(Diagnostic, value) {
                auto repeated = false;
                for (const auto& existing : options.diagnostics.options) {
                    if (existing == value.as_str()) repeated = true;
                }
                if (! repeated) options.diagnostics.options.push(rstd::move(value));
            }
            RSTD_CASE(Vendor, option) {
                options.vendor.push(rstd::move(option));
            }
        }
    }
    return Ok(rstd::move(options));
}

auto c_public_requirements(const CCompileOptions& options) -> CPublicRequirements {
    auto result = CPublicRequirements {};
    for (const auto& include : options.include_directories)
        result.include_directories.push(include.clone());
    for (const auto& macro : options.macros) result.macros.push(macro.clone());
    return result;
}

auto append_c_vendor_identity(String& result, const CVendorOption& option) -> void {
    result.push_str(rstd::format("vendor:{}:{}:{}:{}\n",
                                 static_cast<int>(option.effect),
                                 option.native_preprocessor_unsupported,
                                 option.preserve_raw_tokens,
                                 option.value.len())
                        .as_str());
    result.push_str(option.value.as_str());
    result.push_ascii('\n');
    for (const auto& token : option.raw_tokens) {
        result.push_str(rstd::format("token:{}\n", token.len()).as_str());
        result.push_str(token.as_str());
        result.push_ascii('\n');
    }
}

auto c_compile_identity(const CCompileOptions& options) -> String {
    auto result = String::make("lito-c-compile-options-v4\n"_str);
    result.push_str(lito::manifest::c_standard_name(options.standard));
    result.push_ascii('\n');
    if (options.common.target.target.is_some()) {
        result.push_str(
            rstd::format("target:{}\n", options.common.target.target->as_str()).as_str());
    }
    if (options.common.target.sysroot.is_some()) {
        result.push_str(
            rstd::format("sysroot:{}\n", options.common.target.sysroot->as_str()).as_str());
    }
    result.push_str(rstd::format("optimization:{}\ndebug:{}\nlto:{}\npic:{}\n",
                                 options.common.codegen.optimization.is_some()
                                     ? static_cast<int>(*options.common.codegen.optimization)
                                     : -1,
                                 options.common.codegen.debug_info.is_some()
                                     ? static_cast<int>(*options.common.codegen.debug_info)
                                     : -1,
                                 options.common.codegen.lto.is_some()
                                     ? static_cast<int>(*options.common.codegen.lto)
                                     : -1,
                                 options.common.codegen.position_independent_code)
                        .as_str());
    for (const auto& include : options.include_directories) {
        result.push_str(
            rstd::format("include:{}:{}\n",
                         include.kind == CIncludeDirectoryKind::System ? "system"_str : "user"_str,
                         include.path.as_path())
                .as_str());
    }
    for (const auto& macro : options.macros) {
        result.push_str(
            rstd::format("macro:{}:{}\n",
                         macro.action == CMacroAction::Define ? "define"_str : "undefine"_str,
                         macro.value.as_str())
                .as_str());
    }
    result.push_str(lito::compiler::uses_posix_threads(options.common) ? "threads:posix\n"_str
                                                                       : "threads:none\n"_str);
    for (const auto& warning : options.diagnostics.warnings) {
        result.push_str(
            rstd::format("warning:{}:{}\n", static_cast<int>(warning.warning), warning.enabled)
                .as_str());
    }
    for (const auto& option : options.diagnostics.options) {
        result.push_str(rstd::format("diagnostic:{}\n", option.as_str()).as_str());
    }
    for (const auto& option : options.vendor) {
        append_c_vendor_identity(result, option);
    }
    return result;
}

auto c_scan_identity(const CCompileOptions& options) -> String {
    auto result = String::make("lito-c-scan-options-v4\n"_str);
    result.push_str(lito::manifest::c_standard_name(options.standard));
    result.push_ascii('\n');
    if (options.common.target.target.is_some()) {
        result.push_str(
            rstd::format("target:{}\n", options.common.target.target->as_str()).as_str());
    }
    if (options.common.target.sysroot.is_some()) {
        result.push_str(
            rstd::format("sysroot:{}\n", options.common.target.sysroot->as_str()).as_str());
    }
    for (const auto& include : options.include_directories) {
        result.push_str(
            rstd::format("include:{}:{}\n",
                         include.kind == CIncludeDirectoryKind::System ? "system"_str : "user"_str,
                         include.path.as_path())
                .as_str());
    }
    for (const auto& macro : options.macros) {
        result.push_str(
            rstd::format("macro:{}:{}\n",
                         macro.action == CMacroAction::Define ? "define"_str : "undefine"_str,
                         macro.value.as_str())
                .as_str());
    }
    result.push_str(lito::compiler::uses_posix_threads(options.common) ? "threads:posix\n"_str
                                                                       : "threads:none\n"_str);
    for (const auto& option : options.vendor) {
        if (option.effect == CVendorOptionEffect::Codegen ||
            option.effect == CVendorOptionEffect::Diagnostic) {
            continue;
        }
        append_c_vendor_identity(result, option);
    }
    return result;
}

} // namespace lito::c
