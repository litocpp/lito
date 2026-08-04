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
        return Err(
            Error::make(ErrorKind::Toolchain, rstd::format("{} is not valid UTF-8", context)));
    }
    return Ok(rstd::move(decoded).unwrap());
}

} // namespace tenon

export namespace tenon
{

struct CommandOutput {
    i32                  exit_code {};
    String               standard_output;
    String               standard_error;
    rstd::time::Duration elapsed;
};

struct CommandEnvironmentEntry {
    String         key;
    Option<String> value;
};

struct CommandEnvironment {
    bool                         clear { false };
    Vec<CommandEnvironmentEntry> entries;
};

auto decode_command_output(rstd::process::Output value, rstd::time::Duration elapsed)
    -> Result<CommandOutput> {
    auto stdout_text = output_text(rstd::move(value.stdout_buf), "command stdout"_str);
    if (stdout_text.is_err()) return Err(rstd::move(stdout_text).unwrap_err());
    auto stderr_text = output_text(rstd::move(value.stderr_buf), "command stderr"_str);
    if (stderr_text.is_err()) return Err(rstd::move(stderr_text).unwrap_err());

    auto code = value.status.code();
    return Ok(CommandOutput {
        .exit_code       = code.is_some() ? *code : i32(-1),
        .standard_output = rstd::move(stdout_text).unwrap(),
        .standard_error  = rstd::move(stderr_text).unwrap(),
        .elapsed         = elapsed,
    });
}

auto run_command(const Vec<String>&              arguments,
                 Option<ref<rstd::path::Path>>   working_directory = None(),
                 Option<ref<CommandEnvironment>> environment = None()) -> Result<CommandOutput> {
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
    if (environment.is_some()) {
        if ((*environment)->clear) command.env_clear();
        for (const auto& entry : (*environment)->entries) {
            if (entry.value.is_some())
                command.env(entry.key.as_str(), entry.value->as_str());
            else
                command.env_remove(entry.key.as_str());
        }
    }

    auto started = rstd::time::Instant::now();
    auto output  = command.output();
    auto elapsed = started.elapsed();
    if (output.is_err()) {
        return Err(Error::make(ErrorKind::Toolchain,
                               rstd::format("failed to execute '{}': {}",
                                            arguments[usize {}].as_str(),
                                            rstd::move(output).unwrap_err())));
    }

    return decode_command_output(rstd::move(output).unwrap(), elapsed);
}

auto run_command_with_input(const Vec<String>&            arguments,
                            ref<str>                      standard_input,
                            Option<ref<rstd::path::Path>> working_directory = None())
    -> Result<CommandOutput> {
    if (arguments.is_empty()) {
        return Err(Error::make(ErrorKind::InvalidRequest, "empty command"_str));
    }

    auto command = rstd::process::Command::make(arguments[usize {}].as_str());
    for (auto index = usize(1); index < arguments.len(); ++index) {
        command.arg(arguments[index].as_str());
    }
    if (working_directory.is_some()) command.current_dir(*working_directory);
    command.set_stdin(rstd::process::Stdio::piped());
    command.set_stdout(rstd::process::Stdio::piped());
    command.set_stderr(rstd::process::Stdio::piped());

    auto started = rstd::time::Instant::now();
    auto spawned = command.spawn();
    if (spawned.is_err()) {
        return Err(Error::make(ErrorKind::Toolchain,
                               rstd::format("failed to execute '{}': {}",
                                            arguments[usize {}].as_str(),
                                            rstd::move(spawned).unwrap_err())));
    }
    auto child = rstd::move(spawned).unwrap();
    {
        auto input = child.take_stdin();
        if (input.is_none()) {
            return Err(Error::make(ErrorKind::Toolchain, "command stdin pipe is unavailable"_str));
        }
        auto written = rstd::io::write_all(*input, standard_input.as_bytes());
        if (written.is_err()) {
            return Err(Error::make(ErrorKind::Toolchain,
                                   rstd::format("failed to write command stdin: {}",
                                                rstd::move(written).unwrap_err())));
        }
    }
    auto output = child.wait_with_output();
    if (output.is_err()) {
        return Err(Error::make(
            ErrorKind::Toolchain,
            rstd::format("failed to collect command output: {}", rstd::move(output).unwrap_err())));
    }
    return decode_command_output(rstd::move(output).unwrap(), started.elapsed());
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
