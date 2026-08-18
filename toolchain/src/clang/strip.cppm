module;
#include <rstd/macro.hpp>

export module lito.toolchain.clang:strip;

import rstd;
import lito.core;
import lito.system;
import lito.toolchain.common;

using namespace rstd::prelude;
using namespace lito::system;
using namespace rstd::literals;

export namespace lito
{

class LlvmStrip {
public:
    LlvmStrip(PathBuf executable, const ResolvedProcessEnvironment& environment)
        : executable_(rstd::move(executable)), environment_(rstd::addressof(environment)) {}

    auto strip_to(ref<rstd::path::Path>     input,
                  ref<rstd::path::Path>     output,
                  lito::artifact::StripMode mode,
                  ref<rstd::path::Path>     working_directory) const
        -> ToolchainResult<rstd::time::Duration> {
        auto arguments = rstd_try(arguments_for(mode));
        toolchain::command::push_option(arguments, "-o"_str);
        rstd_try(toolchain::command::push_path(arguments, output));
        rstd_try(toolchain::command::push_path(arguments, input));
        return execute(rstd::move(arguments), input, working_directory);
    }

    auto strip_in_place(ref<rstd::path::Path>     path,
                        lito::artifact::StripMode mode,
                        ref<rstd::path::Path>     working_directory) const
        -> ToolchainResult<rstd::time::Duration> {
        auto arguments = rstd_try(arguments_for(mode));
        rstd_try(toolchain::command::push_path(arguments, path));
        return execute(rstd::move(arguments), path, working_directory);
    }

private:
    auto arguments_for(lito::artifact::StripMode mode) const -> ToolchainResult<Vec<String>> {
        auto arguments = Vec<String>::make();
        rstd_try(toolchain::command::push_path(arguments, executable_.as_path()));
        if (mode == lito::artifact::StripMode::None) {
            return Err(ToolchainError::Message(
                String::make("llvm-strip requires a non-empty strip policy"_str)));
        }
        toolchain::command::push_option(
            arguments,
            mode == lito::artifact::StripMode::DebugInfo ? "--strip-debug"_str : "--strip-all"_str);
        return Ok(rstd::move(arguments));
    }

    auto execute(Vec<String>           arguments,
                 ref<rstd::path::Path> subject,
                 ref<rstd::path::Path> working_directory) const
        -> ToolchainResult<rstd::time::Duration> {
        auto output = run_command(arguments, *environment_, Some(working_directory));
        if (output.is_err()) {
            return Err(rstd::into<ToolchainError>(rstd::move(output).unwrap_err()));
        }
        auto result = rstd::move(output).unwrap();
        if (result.exit_code != i32 {}) {
            return Err(ToolchainError::Execution(rstd::format("llvm-strip '{}'", subject),
                                                 result.exit_code,
                                                 rstd::move(result.standard_output),
                                                 rstd::move(result.standard_error)));
        }
        return Ok(result.elapsed);
    }

    PathBuf                           executable_;
    const ResolvedProcessEnvironment* environment_ {};
};

} // namespace lito
