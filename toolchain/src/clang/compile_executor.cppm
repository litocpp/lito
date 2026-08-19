export module lito.toolchain.clang:compile_executor;

import rstd;
import lito.core;
import lito.toolchain.common;
import lito.system;
import :preprocessor_environment;
import :support;

using namespace rstd::prelude;
using namespace lito::system;
using namespace rstd::literals;

namespace lito
{

namespace
{

constexpr auto WINDOWS_DIRECT_COMMAND_LIMIT = usize(24000);

auto command_length(const Vec<String>& arguments) -> usize {
    auto length = usize {};
    for (const auto& argument : arguments) length += argument.len() + usize(1);
    return length;
}

auto response_file_path(ref<rstd::path::Path> staged_object) -> ToolchainResult<PathBuf> {
    auto text = staged_object.to_str();
    if (text.is_none()) {
        return failure<PathBuf>(
            rstd::format("staged object path '{}' is not valid UTF-8", staged_object));
    }
    return Ok(PathBuf::from(rstd::format("{}.rsp", *text)));
}

void append_response_argument(String& output, ref<str> argument) {
    output.push_ascii(u8('"'));
    for (auto value : argument) {
        if (value == u8('\\')) {
            output.push_str("\\\\"_str);
        } else {
            if (value == u8('"')) output.push_ascii(u8('\\'));
            output.push_ascii(value);
        }
    }
    output.push_str("\"\n"_str);
}

auto write_response_file(const CompileInvocation& invocation) -> ToolchainResult<Option<PathBuf>> {
#if defined(_WIN32)
    if (command_length(invocation.arguments) <= WINDOWS_DIRECT_COMMAND_LIMIT) return Ok(None());
    if (invocation.arguments.is_empty()) {
        return failure<Option<PathBuf>>("compile invocation has no executable"_str);
    }
    auto path_result = response_file_path(invocation.staged_object.as_path());
    if (path_result.is_err()) return Err(rstd::move(path_result).unwrap_err());
    auto path     = rstd::move(path_result).unwrap();
    auto contents = String::make();
    for (auto index = usize(1); index < invocation.arguments.len(); ++index)
        append_response_argument(contents, invocation.arguments[index].as_str());
    auto written = rstd::fs::write_atomic(path.as_path(), contents.as_str().as_bytes());
    if (written.is_err()) {
        return io_failure<Option<PathBuf>>(
            "write compiler response file"_str, path.as_path(), rstd::move(written).unwrap_err());
    }
    return Ok(Some(rstd::move(path)));
#else
    (void)invocation;
    return Ok(None());
#endif
}

auto response_command(const CompileInvocation& invocation, ref<rstd::path::Path> response_file)
    -> ToolchainResult<Vec<String>> {
    auto text = response_file.to_str();
    if (text.is_none()) {
        return failure<Vec<String>>(
            rstd::format("compiler response file '{}' is not valid UTF-8", response_file));
    }
    auto result = Vec<String>::with_capacity(usize(2));
    result.push(invocation.arguments[usize {}].clone());
    result.push(rstd::format("@{}", *text));
    return Ok(rstd::move(result));
}

} // namespace

} // namespace lito

export namespace lito
{

struct ClangBuiltinContext {
    Vec<String>                       query_command;
    toolchain::BuiltinSemanticContext semantic;
    String                            key;
    toolchain::PreprocessorLanguage   language { toolchain::PreprocessorLanguage::Cpp };
    usize                             ignored_options {};
};

class ClangCompileExecutor {
public:
    explicit ClangCompileExecutor(const ResolvedProcessEnvironment& environment)
        : environment_(rstd::addressof(environment)) {}

    auto execute(const CompileInvocation& invocation) const
        -> ToolchainResult<CompileCommandResult> {
        auto cleared = clear_staged_output(invocation.staged_object.as_path());
        if (cleared.is_err()) return Err(rstd::move(cleared).unwrap_err());
        if (invocation.staged_bmi.is_some()) {
            cleared = clear_staged_output(invocation.staged_bmi->as_path());
            if (cleared.is_err()) return Err(rstd::move(cleared).unwrap_err());
        }
        auto response_file = write_response_file(invocation);
        if (response_file.is_err()) return Err(rstd::move(response_file).unwrap_err());
        auto response  = rstd::move(response_file).unwrap();
        auto arguments = Option<Vec<String>> {};
        if (response.is_some()) {
            auto command = response_command(invocation, response->as_path());
            if (command.is_err()) return Err(rstd::move(command).unwrap_err());
            arguments = Some(rstd::move(command).unwrap());
        }
        const auto& command = arguments.is_some() ? *arguments : invocation.arguments;
        auto        output =
            run_command(command, *environment_, Some(invocation.working_directory.as_path()));
        if (response.is_some()) (void)rstd::fs::remove_file(response->as_path());
        if (output.is_err()) {
            return Err(rstd::into<ToolchainError>(rstd::move(output).unwrap_err()));
        }
        auto command_output = rstd::move(output).unwrap();
        if (command_output.exit_code == i32 {}) {
            auto verified = verify_staged_output(invocation.staged_object.as_path());
            if (verified.is_err()) return Err(rstd::move(verified).unwrap_err());
            if (invocation.staged_bmi.is_some()) {
                if (invocation.final_bmi.is_none()) {
                    return failure<CompileCommandResult>(
                        "compile invocation has a staged BMI without a final output"_str);
                }
                verified = verify_staged_output(invocation.staged_bmi->as_path());
                if (verified.is_err()) return Err(rstd::move(verified).unwrap_err());
            }
            if (invocation.staged_bmi.is_some() && invocation.final_bmi.is_some()) {
                auto published = publish_output(invocation.staged_bmi->as_path(),
                                                invocation.final_bmi->as_path());
                if (published.is_err()) return Err(rstd::move(published).unwrap_err());
            }
            if (invocation.final_object.is_some()) {
                auto published = publish_output(invocation.staged_object.as_path(),
                                                invocation.final_object->as_path());
                if (published.is_err()) return Err(rstd::move(published).unwrap_err());
            } else {
                auto removed = clear_staged_output(invocation.staged_object.as_path());
                if (removed.is_err()) return Err(rstd::move(removed).unwrap_err());
            }
        }
        return Ok(CompileCommandResult {
            .exit_code       = command_output.exit_code,
            .standard_output = rstd::move(command_output.standard_output),
            .standard_error  = rstd::move(command_output.standard_error),
            .elapsed         = command_output.elapsed,
        });
    }

private:
    const ResolvedProcessEnvironment* environment_ {};
};

} // namespace lito
