export module lito.toolchain:command;

import rstd;
import lito.error;
import lito.toolchain.contract;
import lito.system.process;
import lito.system.environment;

using namespace rstd::prelude;

namespace lito::toolchain::command
{

template<typename T>
auto failure(String message) -> Result<T> {
    return Err(Error::make(ErrorKind::Toolchain, rstd::move(message)));
}

} // namespace lito::toolchain::command

export namespace lito::toolchain::command
{

auto is_searchable_tool_name(ref<rstd::path::Path> path) -> bool {
    auto components = path.components();
    auto first      = components.next();
    return first.is_some() && first->is_normal() && components.next().is_none();
}

auto resolve_path(ref<rstd::path::Path> path, ref<str> name) -> Result<PathBuf> {
    auto canonical = rstd::fs::canonicalize(path);
    if (canonical.is_err()) {
        return failure<PathBuf>(rstd::format(
            "cannot resolve {} '{}': {}", name, path, rstd::move(canonical).unwrap_err()));
    }
    return Ok(rstd::move(canonical).unwrap());
}

auto push_path(Vec<String>& arguments, ref<rstd::path::Path> path) -> Result<empty> {
    auto text = path.to_str();
    if (text.is_none()) {
        return failure<empty>(rstd::format("tool path '{}' is not valid UTF-8", path));
    }
    arguments.push(String::make(*text));
    return Ok(empty {});
}

auto push_path_option(Vec<String>& arguments, ref<str> prefix, ref<rstd::path::Path> path)
    -> Result<empty> {
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
                     const ResolvedProcessEnvironment& environment) -> Result<String> {
    auto output = run_command(arguments, environment);
    if (output.is_err()) return Err(rstd::move(output).unwrap_err());
    auto value = rstd::move(output).unwrap();
    if (value.exit_code != i32 {}) {
        return failure<String>(rstd::format("{} failed with exit code {}:\n{}",
                                            description,
                                            value.exit_code,
                                            value.standard_error.as_str()));
    }
    return Ok(rstd::move(value.standard_output));
}

auto tool_output(Vec<String>                       arguments,
                 ref<str>                          description,
                 const ResolvedProcessEnvironment& environment) -> Result<String> {
    auto output = tool_output_raw(rstd::move(arguments), description, environment);
    if (output.is_err()) return Err(rstd::move(output).unwrap_err());
    return Ok(trim_ascii(rstd::move(output).unwrap()));
}

} // namespace lito::toolchain::command
