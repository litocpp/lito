module;
#include <rstd/macro.hpp>

export module lito.tools:command;

import rstd;
import lito.system;
import :error;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito::system;

namespace lito::tools::command
{

template<typename T>
auto failure(String message) -> ToolResult<T> {
    return Err(ToolError::Message(rstd::move(message)));
}

} // namespace lito::tools::command

export namespace lito::tools::command
{

auto push_path(Vec<String>& arguments, ref<rstd::path::Path> path) -> ToolResult<empty> {
    auto text = path.to_str();
    if (text.is_none()) {
        return failure<empty>(rstd::format("tool path '{}' is not valid UTF-8", path));
    }
    arguments.push(String::make(*text));
    return Ok(empty {});
}

auto push_path_option(Vec<String>& arguments, ref<str> prefix, ref<rstd::path::Path> path)
    -> ToolResult<empty> {
    auto text = path.to_str();
    if (text.is_none()) {
        return failure<empty>(rstd::format("tool option path '{}' is not valid UTF-8", path));
    }
    arguments.push(rstd::format("{}{}", prefix, *text));
    return Ok(empty {});
}

auto push_option(Vec<String>& arguments, ref<str> option) -> void {
    arguments.push(String::make(option));
}

auto tool_output_raw(Vec<String>                       arguments,
                     ref<str>                          description,
                     const ResolvedProcessEnvironment& environment) -> ToolResult<String> {
    auto output = run_command(arguments, environment);
    if (output.is_err()) return Err(rstd::into<ToolError>(rstd::move(output).unwrap_err()));
    auto value = rstd::move(output).unwrap();
    if (value.exit_code != i32 {}) {
        return Err(ToolError::Execution(String::make(description),
                                        value.exit_code,
                                        rstd::move(value.standard_output),
                                        rstd::move(value.standard_error)));
    }
    return Ok(rstd::move(value.standard_output));
}

auto tool_output(Vec<String>                       arguments,
                 ref<str>                          description,
                 const ResolvedProcessEnvironment& environment) -> ToolResult<String> {
    return Ok(
        trim_ascii(rstd_try(tool_output_raw(rstd::move(arguments), description, environment))));
}

} // namespace lito::tools::command
