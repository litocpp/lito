export module tenon.toolchain.command;

import rstd;
import tenon.model;
import tenon.process;

using namespace rstd::prelude;

namespace tenon::toolchain::command
{

template<typename T>
auto failure(String message) -> Result<T> {
    return Err(Error::make(ErrorKind::Toolchain, rstd::move(message)));
}

} // namespace tenon::toolchain::command

export namespace tenon::toolchain::command
{

auto canonical_tool(ref<rstd::path::Path> path, ref<str> name)
    -> Result<PathBuf> {
    auto canonical = rstd::fs::canonicalize(path);
    if (canonical.is_err()) {
        return failure<PathBuf>(rstd::format(
            "cannot resolve {} '{}': {}", name, path, rstd::move(canonical).unwrap_err()));
    }
    return Ok(rstd::move(canonical).unwrap());
}

auto push_path(Vec<String>& arguments, ref<rstd::path::Path> path)
    -> Result<empty> {
    auto text = path.to_str();
    if (text.is_none()) {
        return failure<empty>(
            rstd::format("tool path '{}' is not valid UTF-8", path));
    }
    arguments.push(String::make(*text));
    return Ok(empty {});
}

auto push_option(Vec<String>& arguments, ref<str> option) -> void {
    arguments.push(String::make(option));
}

auto tool_output(Vec<String> arguments, ref<str> description) -> Result<String> {
    auto output = run_command(arguments);
    if (output.is_err()) return Err(rstd::move(output).unwrap_err());
    auto value = rstd::move(output).unwrap();
    if (value.exit_code != i32 {}) {
        return failure<String>(rstd::format(
            "{} failed with exit code {}:\n{}",
            description,
            value.exit_code,
            value.standard_error.as_str()));
    }
    return Ok(trim_ascii(rstd::move(value.standard_output)));
}

} // namespace tenon::toolchain::command
