export module lito.toolchain.common:command;

import rstd;
import lito.core;
import :error;
import lito.system;

using namespace rstd::prelude;
using namespace lito::system;

namespace lito::toolchain::command
{

template<typename T>
auto failure(String message) -> ToolchainResult<T> {
    return Err(ToolchainError::Message(rstd::move(message)));
}

} // namespace lito::toolchain::command

export namespace lito::toolchain::command
{

auto is_searchable_tool_name(ref<rstd::path::Path> path) -> bool {
    return is_searchable_executable_name(path);
}

auto resolve_path(ref<rstd::path::Path> path, ref<str> name) -> ToolchainResult<PathBuf> {
    auto canonical = rstd::fs::canonicalize(path);
    if (canonical.is_err()) {
        return Err(ToolchainError::Io(rstd::format("resolve {}", name),
                                      PathBuf::from(path),
                                      rstd::move(canonical).unwrap_err()));
    }
    return Ok(rstd::move(canonical).unwrap());
}

auto push_path(Vec<String>& arguments, ref<rstd::path::Path> path) -> ToolchainResult<empty> {
    auto text = path.to_str();
    if (text.is_none()) {
        return failure<empty>(rstd::format("tool path '{}' is not valid UTF-8", path));
    }
    arguments.push(String::make(*text));
    return Ok(empty {});
}

auto push_path_option(Vec<String>& arguments, ref<str> prefix, ref<rstd::path::Path> path)
    -> ToolchainResult<empty> {
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
                     const ResolvedProcessEnvironment& environment) -> ToolchainResult<String> {
    auto output = run_command(arguments, environment);
    if (output.is_err()) {
        return Err(rstd::into<ToolchainError>(rstd::move(output).unwrap_err()));
    }
    auto value = rstd::move(output).unwrap();
    if (value.exit_code != i32 {}) {
        return Err(ToolchainError::Execution(String::make(description),
                                             value.exit_code,
                                             rstd::move(value.standard_output),
                                             rstd::move(value.standard_error)));
    }
    return Ok(rstd::move(value.standard_output));
}

auto tool_output(Vec<String>                       arguments,
                 ref<str>                          description,
                 const ResolvedProcessEnvironment& environment) -> ToolchainResult<String> {
    auto output = tool_output_raw(rstd::move(arguments), description, environment);
    if (output.is_err()) return Err(rstd::move(output).unwrap_err());
    return Ok(trim_ascii(rstd::move(output).unwrap()));
}

} // namespace lito::toolchain::command
