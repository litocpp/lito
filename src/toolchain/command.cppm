export module tenon.toolchain.command;

import rstd;
import tenon.model;
import tenon.process;

namespace tenon::toolchain::command_detail
{

template<typename T>
auto failure(String message) -> Result<T> {
    return rstd::Err(Error::make(ErrorKind::Toolchain, rstd::move(message)));
}

} // namespace tenon::toolchain::command_detail

export namespace tenon::toolchain::command
{

auto canonical_tool(rstd::ref<rstd::path::Path> path, rstd::ref<rstd::str> name)
    -> Result<PathBuf> {
    auto canonical = rstd::fs::canonicalize(path);
    if (canonical.is_err()) {
        return command_detail::failure<PathBuf>(rstd::format(
            "cannot resolve {} '{}': {}", name, path, rstd::move(canonical).unwrap_err()));
    }
    return rstd::Ok(rstd::move(canonical).unwrap());
}

auto push_path(Vec<String>& arguments, rstd::ref<rstd::path::Path> path)
    -> Result<rstd::empty> {
    auto text = path.to_str();
    if (text.is_none()) {
        return command_detail::failure<rstd::empty>(
            rstd::format("tool path '{}' is not valid UTF-8", path));
    }
    arguments.push(String::make(*text));
    return rstd::Ok(rstd::empty {});
}

auto push_option(Vec<String>& arguments, rstd::ref<rstd::str> option) -> void {
    arguments.push(String::make(option));
}

auto tool_output(Vec<String> arguments, rstd::ref<rstd::str> description) -> Result<String> {
    auto output = run_command(arguments);
    if (output.is_err()) return rstd::Err(rstd::move(output).unwrap_err());
    auto value = rstd::move(output).unwrap();
    if (value.exit_code != rstd::i32 {}) {
        return command_detail::failure<String>(rstd::format(
            "{} failed with exit code {}:\n{}",
            description,
            value.exit_code,
            value.standard_error.as_str()));
    }
    return rstd::Ok(trim_ascii(rstd::move(value.standard_output)));
}

} // namespace tenon::toolchain::command
