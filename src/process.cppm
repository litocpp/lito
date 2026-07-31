export module tenon.process;

import rstd;
import tenon.model;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace tenon
{

auto output_text(Vec<u8> bytes, ref<str> context) -> Result<String> {
    auto decoded = String::from_utf8(rstd::move(bytes));
    if (decoded.is_err()) {
        return Err(Error::make(
            ErrorKind::Toolchain, rstd::format("{} is not valid UTF-8", context)));
    }
    return Ok(rstd::move(decoded).unwrap());
}

} // namespace tenon

export namespace tenon
{

struct CommandOutput {
    i32 exit_code {};
    String    standard_output;
    String    standard_error;
};

auto run_command(const Vec<String>& arguments,
                 Option<ref<rstd::path::Path>> working_directory = None())
    -> Result<CommandOutput> {
    if (arguments.is_empty()) {
        return Err(Error::make(ErrorKind::InvalidRequest, "empty command"_str));
    }

    auto command = rstd::process::Command::make(arguments[usize {}].as_str());
    for (auto index = usize(1); index < arguments.len(); ++index) {
        command.arg(arguments[index].as_str());
    }

    if (working_directory.is_some()) {
        command.current_dir(*working_directory);
    }

    auto output = command.output();
    if (output.is_err()) {
        return Err(Error::make(
            ErrorKind::Toolchain,
            rstd::format("failed to execute '{}': {}",
                         arguments[usize {}].as_str(),
                         rstd::move(output).unwrap_err())));
    }

    auto value  = rstd::move(output).unwrap();
    auto stdout_text = output_text(rstd::move(value.stdout_buf), "command stdout"_str);
    if (stdout_text.is_err()) return Err(rstd::move(stdout_text).unwrap_err());
    auto stderr_text = output_text(rstd::move(value.stderr_buf), "command stderr"_str);
    if (stderr_text.is_err()) return Err(rstd::move(stderr_text).unwrap_err());

    auto code = value.status.code();
    return Ok(CommandOutput {
        .exit_code = code.is_some() ? *code : i32(-1),
        .standard_output = rstd::move(stdout_text).unwrap(),
        .standard_error = rstd::move(stderr_text).unwrap(),
    });
}

auto command_text(const Vec<String>& arguments) -> String {
    auto result = String::make();
    for (const auto& argument : arguments) {
        if (! result.is_empty()) result.push_ascii(u8(' '));
        auto quote = false;
        for (auto value : argument.as_str()) {
            if (value == u8(' ') || value == u8('\t')) {
                quote = true;
                break;
            }
        }
        if (quote) result.push_ascii(u8('"'));
        result.push_str(argument.as_str());
        if (quote) result.push_ascii(u8('"'));
    }
    return result;
}

auto trim_ascii(String value) -> String {
    return String::make(value.as_str().trim_ascii());
}

} // namespace tenon
