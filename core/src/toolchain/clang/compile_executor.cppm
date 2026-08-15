export module lito.toolchain:clang.compile_executor;

import rstd;
import lito.error;
import lito.toolchain.contract;
import lito.system.process;
import lito.system.environment;
import :clang.preprocessor_environment;
import :clang.support;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito
{

struct ClangBuiltinContext {
    Vec<String>                       query_command;
    toolchain::BuiltinSemanticContext semantic;
    String                            key;
    usize                             ignored_options {};
};

class ClangCompileExecutor {
public:
    explicit ClangCompileExecutor(const ResolvedProcessEnvironment& environment)
        : environment_(rstd::addressof(environment)) {}

    auto execute(const CompileInvocation& invocation) const -> ToolchainResult<CompileCommandResult> {
        auto cleared = clear_staged_output(invocation.staged_object.as_path());
        if (cleared.is_err()) return Err(rstd::move(cleared).unwrap_err());
        if (invocation.staged_bmi.is_some()) {
            cleared = clear_staged_output(invocation.staged_bmi->as_path());
            if (cleared.is_err()) return Err(rstd::move(cleared).unwrap_err());
        }
        auto output = run_command(
            invocation.arguments, *environment_, Some(invocation.working_directory.as_path()));
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
            auto published = publish_output(invocation.staged_object.as_path(),
                                            invocation.final_object.as_path());
            if (published.is_err()) return Err(rstd::move(published).unwrap_err());
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
