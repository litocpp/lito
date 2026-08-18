export module lito.cpp:c.compiler;

import rstd;
import lito.core;
import :compiler.common;

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

struct CCompileOptions {
    lito::compiler::CommonCompileOptions common;
    lito::manifest::CStandard            standard { lito::manifest::CStandard::C99 };
    Vec<CIncludeDirectory>               include_directories;
    Vec<CMacroDirective>                 macros;
    lito::compiler::DiagnosticOptions    diagnostics;

    auto clone() const -> CCompileOptions {
        auto includes = Vec<CIncludeDirectory>::with_capacity(include_directories.len());
        for (const auto& include : include_directories) includes.push(include.clone());
        auto copied_macros = Vec<CMacroDirective>::with_capacity(macros.len());
        for (const auto& macro : macros) copied_macros.push(macro.clone());
        return CCompileOptions {
            .common              = common.clone(),
            .standard            = standard,
            .include_directories = rstd::move(includes),
            .macros              = rstd::move(copied_macros),
            .diagnostics         = diagnostics.clone(),
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

struct COptionLayer {
    Vec<PathBuf>                                include_directories;
    Vec<String>                                 definitions;
    Vec<lito::compiler::CommonCompilerArgument> arguments;
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

auto apply_c_option_layer(CCompileOptions options, COptionLayer layer) -> CCompileOptions {
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
    for (auto& argument : layer.arguments) {
        lito::compiler::apply_common_compiler_argument(
            options.common, options.diagnostics, rstd::move(argument));
    }
    return options;
}

auto c_public_requirements(const CCompileOptions& options) -> CPublicRequirements {
    auto result = CPublicRequirements {};
    for (const auto& include : options.include_directories)
        result.include_directories.push(include.clone());
    for (const auto& macro : options.macros) result.macros.push(macro.clone());
    return result;
}

auto c_compile_identity(const CCompileOptions& options) -> String {
    auto result = String::make("lito-c-compile-options-v3\n"_str);
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
                                 static_cast<int>(options.common.codegen.optimization),
                                 static_cast<int>(options.common.codegen.debug_info),
                                 static_cast<int>(options.common.codegen.lto),
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
    return result;
}

auto c_scan_identity(const CCompileOptions& options) -> String {
    auto result = String::make("lito-c-scan-options-v3\n"_str);
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
    return result;
}

} // namespace lito::c
