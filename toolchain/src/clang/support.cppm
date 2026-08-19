module;
#include <rstd/macro.hpp>

export module lito.toolchain.clang:support;

import rstd;
import lito.core;
import lito.cpp;
import lito.toolchain.common;
import lito.system;
import lito.frontend;
import :arguments;
import :options;
import :preprocessor_environment;

using namespace rstd::prelude;
using namespace lito::system;
using namespace rstd::literals;

namespace lito
{
namespace preprocessor = frontend::preprocessor;
}

namespace lito
{

template<typename T>
auto failure(String message) -> ToolchainResult<T> {
    return Err(ToolchainError::Message(rstd::move(message)));
}

template<typename T>
auto failure(ref<str> message) -> ToolchainResult<T> {
    return Err(ToolchainError::Message(String::make(message)));
}

template<typename T>
auto io_failure(ref<str> operation, ref<rstd::path::Path> path, rstd::io::error::Error source)
    -> ToolchainResult<T> {
    return Err(
        ToolchainError::Io(String::make(operation), PathBuf::from(path), rstd::move(source)));
}

auto create_parent(ref<rstd::path::Path> path) -> ToolchainResult<empty> {
    auto parent = path.parent();
    if (parent.is_none()) {
        return failure<empty>(rstd::format("output path '{}' has no parent", path));
    }
    auto created = rstd::fs::create_dir_all(*parent);
    if (created.is_err()) {
        return io_failure<empty>("create directory"_str, *parent, rstd::move(created).unwrap_err());
    }
    return Ok(empty {});
}

auto staging_path(ref<rstd::path::Path> output) -> ToolchainResult<PathBuf> {
    auto text = output.to_str();
    if (text.is_none()) {
        return failure<PathBuf>(rstd::format("output path '{}' is not valid UTF-8", output));
    }
    auto value = String::make(*text);
    value.push_str(".lito-building"_str);
    return Ok(PathBuf::from(rstd::move(value)));
}

auto clear_staged_output(ref<rstd::path::Path> path) -> ToolchainResult<empty> {
    auto exists = rstd::fs::exists(path);
    if (exists.is_err()) {
        return io_failure<empty>("inspect staged output"_str, path, exists.unwrap_err());
    }
    if (! *exists) return Ok(empty {});
    auto removed = rstd::fs::remove_file(path);
    if (removed.is_err()) {
        return io_failure<empty>("remove stale staged output"_str, path, removed.unwrap_err());
    }
    return Ok(empty {});
}

auto publish_output(ref<rstd::path::Path> staged, ref<rstd::path::Path> final)
    -> ToolchainResult<empty> {
    auto exists = rstd::fs::exists(staged);
    if (exists.is_err()) {
        return io_failure<empty>("inspect staged output"_str, staged, exists.unwrap_err());
    }
    if (! *exists) {
        return failure<empty>(rstd::format("compiler did not produce staged output '{}'", staged));
    }
    auto published = rstd::fs::rename(staged, final);
    if (published.is_err()) {
        return io_failure<empty>(rstd::format("publish compiler output as '{}'", final).as_str(),
                                 staged,
                                 published.unwrap_err());
    }
    return Ok(empty {});
}

auto verify_staged_output(ref<rstd::path::Path> path) -> ToolchainResult<empty> {
    auto exists = rstd::fs::exists(path);
    if (exists.is_err()) {
        return io_failure<empty>("inspect staged output"_str, path, exists.unwrap_err());
    }
    if (! *exists) {
        return failure<empty>(rstd::format("compiler did not produce staged output '{}'", path));
    }
    return Ok(empty {});
}

auto invocation_identity(const Vec<String>& arguments, ref<rstd::path::Path> working_directory)
    -> ToolchainResult<String> {
    auto working = working_directory.to_str();
    if (working.is_none()) {
        return failure<String>(
            rstd::format("compile working directory '{}' is not valid UTF-8", working_directory));
    }
    auto identity = String::make("lito-clang-compile-invocation-v1\n"_str);
    identity.push_str(rstd::format("{}:{}\n", working->size(), *working).as_str());
    for (const auto& argument : arguments) {
        identity.push_str(rstd::format("{}:{}\n", argument.size(), argument.as_str()).as_str());
    }
    return Ok(rstd::move(identity));
}

auto argument_identity(ref<str> recipe, const Vec<String>& arguments) -> String {
    auto identity = String::make(recipe);
    identity.push_ascii('\n');
    for (const auto& argument : arguments) {
        identity.push_str(rstd::format("{}:{}\n", argument.size(), argument.as_str()).as_str());
    }
    return identity;
}

auto clang_warning_option(compiler::CompilerWarningOption option) noexcept -> ref<str> {
    switch (option.warning) {
    case compiler::CompilerWarning::All: return option.enabled ? "-Wall"_str : "-Wno-all"_str;
    case compiler::CompilerWarning::Pedantic:
        return option.enabled ? "-Wpedantic"_str : "-Wno-pedantic"_str;
    case compiler::CompilerWarning::GnuStatementExpression:
        return option.enabled ? "-Wgnu-statement-expression"_str
                              : "-Wno-gnu-statement-expression"_str;
    case compiler::CompilerWarning::DeprecatedDeclarations:
        return option.enabled ? "-Wdeprecated-declarations"_str
                              : "-Wno-deprecated-declarations"_str;
    case compiler::CompilerWarning::UnknownAttributes:
        return option.enabled ? "-Wunknown-attributes"_str : "-Wno-unknown-attributes"_str;
    }
    return {};
}

auto append_typed_options(Vec<String>&                  command,
                          const cpp::CppCompileOptions& options,
                          const TargetInfo&             target,
                          bool                          semantic_only) -> void {
    if (options.common.target.target.is_some()) {
        command.push(rstd::format("--target={}", options.common.target.target->as_str()));
    }
    if (options.common.target.sysroot.is_some()) {
        command.push(rstd::format("--sysroot={}", options.common.target.sysroot->as_str()));
    }
    auto optimization = cpp::cpp_optimization_option(options.common.codegen.optimization);
    if (! optimization.is_empty()) toolchain::command::push_option(command, optimization);
    auto debug = cpp::cpp_debug_option(options.common.codegen.debug_info);
    if (! semantic_only && ! debug.is_empty()) toolchain::command::push_option(command, debug);
    auto lto = cpp::cpp_lto_option(options.common.codegen.lto);
    if (! semantic_only && ! lto.is_empty()) toolchain::command::push_option(command, lto);
    if (target.family != TargetFamily::Windows) {
        toolchain::command::push_option(
            command,
            options.common.codegen.position_independent_code ? "-fPIC"_str : "-fno-PIC"_str);
    }
    if (! semantic_only) {
        command.push(
            rstd::format("-fvisibility={}",
                         cpp::cpp_symbol_visibility_name(options.codegen.visibility.symbols)));
        if (options.codegen.visibility.types.is_some()) {
            command.push(
                rstd::format("-ftype-visibility={}",
                             cpp::cpp_symbol_visibility_name(*options.codegen.visibility.types)));
        }
        if (options.codegen.visibility.inlines_hidden) {
            toolchain::command::push_option(command, "-fvisibility-inlines-hidden"_str);
        }
    }
    switch (options.language.sized_deallocation) {
    case cpp::CppSizedDeallocation::Auto: break;
    case cpp::CppSizedDeallocation::Enabled:
        toolchain::command::push_option(command, "-fsized-deallocation"_str);
        break;
    case cpp::CppSizedDeallocation::Disabled:
        toolchain::command::push_option(command, "-fno-sized-deallocation"_str);
        break;
    }
    for (const auto& option : options.language.modes) command.push(option.value.clone());
    for (const auto& option : options.abi.modes) command.push(option.value.clone());
    if (target.environment == TargetEnvironment::Msvc &&
        options.common.microsoft_runtime_library.is_some()) {
        command.push(rstd::format("-fms-runtime-lib={}",
                                  lito::compiler::microsoft_runtime_library_name(
                                      *options.common.microsoft_runtime_library)));
    }
    if (compiler::uses_posix_threads(options.common)) command.push(String::make("-pthread"_str));
    for (const auto& option : options.target.features) command.push(option.value.clone());
    for (const auto& option : options.codegen.modes) command.push(option.value.clone());
    for (const auto& option : options.codegen.instrumentation) command.push(option.clone());
    toolchain::command::push_option(command,
                                    options.language.rtti ? toolchain::clang_options::RTTI
                                                          : toolchain::clang_options::NO_RTTI);
    toolchain::command::push_option(command,
                                    options.language.exceptions
                                        ? toolchain::clang_options::EXCEPTIONS
                                        : toolchain::clang_options::NO_EXCEPTIONS);
    for (const auto& option : options.vendor) {
        if (semantic_only && (option.effect == cpp::CppVendorOptionEffect::Codegen ||
                              option.effect == cpp::CppVendorOptionEffect::Diagnostic)) {
            continue;
        }
        if (option.preserve_raw_tokens) {
            for (const auto& token : option.raw_tokens) command.push(token.clone());
        } else {
            command.push(option.value.clone());
        }
    }
    if (! semantic_only) {
        for (const auto& warning : options.diagnostics.warnings) {
            toolchain::command::push_option(command, clang_warning_option(warning));
        }
        for (const auto& option : options.diagnostics.options) command.push(option.clone());
    }
}

auto append_c_typed_options(Vec<String>&                    command,
                            const lito::c::CCompileOptions& options,
                            const TargetInfo&               target,
                            bool                            semantic_only) -> void {
    if (options.common.target.target.is_some()) {
        command.push(rstd::format("--target={}", options.common.target.target->as_str()));
    }
    if (options.common.target.sysroot.is_some()) {
        command.push(rstd::format("--sysroot={}", options.common.target.sysroot->as_str()));
    }
    auto optimization = cpp::cpp_optimization_option(options.common.codegen.optimization);
    if (! optimization.is_empty()) toolchain::command::push_option(command, optimization);
    auto debug = cpp::cpp_debug_option(options.common.codegen.debug_info);
    if (! semantic_only && ! debug.is_empty()) toolchain::command::push_option(command, debug);
    if (! semantic_only) {
        auto lto = cpp::cpp_lto_option(options.common.codegen.lto);
        if (! lto.is_empty()) toolchain::command::push_option(command, lto);
    }
    if (target.family != TargetFamily::Windows) {
        toolchain::command::push_option(
            command,
            options.common.codegen.position_independent_code ? "-fPIC"_str : "-fno-PIC"_str);
    }
    if (compiler::uses_posix_threads(options.common)) {
        toolchain::command::push_option(command, "-pthread"_str);
    }
    if (target.environment == TargetEnvironment::Msvc &&
        options.common.microsoft_runtime_library.is_some()) {
        command.push(rstd::format("-fms-runtime-lib={}",
                                  lito::compiler::microsoft_runtime_library_name(
                                      *options.common.microsoft_runtime_library)));
    }
    for (const auto& option : options.vendor) {
        if (semantic_only && (option.effect == lito::c::CVendorOptionEffect::Codegen ||
                              option.effect == lito::c::CVendorOptionEffect::Diagnostic)) {
            continue;
        }
        if (option.preserve_raw_tokens) {
            for (const auto& token : option.raw_tokens) command.push(token.clone());
        } else {
            command.push(option.value.clone());
        }
    }
    if (! semantic_only) {
        for (const auto& warning : options.diagnostics.warnings) {
            toolchain::command::push_option(command, clang_warning_option(warning));
        }
        for (const auto& option : options.diagnostics.options) command.push(option.clone());
    }
}

} // namespace lito
