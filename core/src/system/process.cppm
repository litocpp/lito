export module lito.system:process;

import rstd;
import :error;
import :environment;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;
using namespace rstd::literals;

namespace lito::system
{

auto output_text(Vec<u8> bytes, ref<str> context) -> SystemResult<String> {
    auto decoded = String::from_utf8(rstd::move(bytes));
    if (decoded.is_err()) {
        return Err(SystemError::Utf8(String::make(context), rstd::move(decoded).unwrap_err()));
    }
    return Ok(rstd::move(decoded).unwrap());
}

enum class FragmentQuote
{
    None,
    Single,
    Double,
};

auto push_fragment_word(Vec<String>& output, Vec<u8>& current, ref<str> context)
    -> SystemResult<empty> {
    auto word = String::from_utf8(rstd::move(current));
    if (word.is_err()) {
        return Err(SystemError::Utf8(String::make(context), rstd::move(word).unwrap_err()));
    }
    output.push(rstd::move(word).unwrap());
    current = Vec<u8>::make();
    return Ok(empty {});
}

} // namespace lito::system

export namespace lito::system
{

struct CommandOutput {
    i32                  exit_code {};
    String               standard_output;
    String               standard_error;
    rstd::time::Duration elapsed;
};

struct CommandEnvironmentEntry {
    String                      key;
    Option<rstd::ffi::OsString> value;
};

struct CommandEnvironment {
    bool                         clear { false };
    Vec<CommandEnvironmentEntry> entries;
};

auto apply_command_environment(rstd::process::Command&           command,
                               const ResolvedProcessEnvironment& environment,
                               Option<ref<CommandEnvironment>>   overrides = None()) -> void {
    command.env("PATH"_str, environment.child_path());
    if (overrides.is_none()) return;
    if ((*overrides)->clear) command.env_clear().env("PATH"_str, environment.child_path());
    for (const auto& entry : (*overrides)->entries) {
        if (entry.value.is_some())
            command.env(entry.key.as_str(), entry.value->as_os_str());
        else
            command.env_remove(entry.key.as_str());
    }
}

auto decode_command_output(rstd::process::Output value, rstd::time::Duration elapsed)
    -> SystemResult<CommandOutput> {
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

auto run_command(const Vec<String>&                arguments,
                 const ResolvedProcessEnvironment& environment,
                 Option<ref<rstd::path::Path>>     working_directory = None(),
                 Option<ref<CommandEnvironment>>   overrides         = None())
    -> SystemResult<CommandOutput> {
    if (arguments.is_empty()) {
        return Err(SystemError::InvalidCommand(String::make("empty command"_str)));
    }
    auto program = PathBuf::from(arguments[usize {}].as_str());
    if (! program.as_path().is_absolute()) {
        return Err(SystemError::InvalidCommand(rstd::format(
            "command program '{}' is not an absolute path", arguments[usize {}].as_str())));
    }

    auto command = rstd::process::Command::make(arguments[usize {}].as_str());
    for (auto index = usize(1); index < arguments.len(); ++index) {
        command.arg(arguments[index].as_str());
    }

    if (working_directory.is_some()) {
        command.current_dir(*working_directory);
    }
    apply_command_environment(command, environment, overrides);

    auto started = rstd::time::Instant::now();
    auto output  = command.output();
    auto elapsed = started.elapsed();
    if (output.is_err()) {
        return Err(SystemError::Io(String::make("failed to execute"_str),
                                   rstd::move(program),
                                   rstd::move(output).unwrap_err()));
    }

    return decode_command_output(rstd::move(output).unwrap(), elapsed);
}

auto run_command_observed(const Vec<String>&                arguments,
                          const ResolvedProcessEnvironment& environment,
                          rstd::process::OutputObserver     observer,
                          Option<ref<rstd::path::Path>>     working_directory = None(),
                          Option<ref<CommandEnvironment>>   overrides         = None())
    -> SystemResult<CommandOutput> {
    if (arguments.is_empty()) {
        return Err(SystemError::InvalidCommand(String::make("empty command"_str)));
    }
    auto program = PathBuf::from(arguments[usize {}].as_str());
    if (! program.as_path().is_absolute()) {
        return Err(SystemError::InvalidCommand(rstd::format(
            "command program '{}' is not an absolute path", arguments[usize {}].as_str())));
    }

    auto command = rstd::process::Command::make(arguments[usize {}].as_str());
    for (auto index = usize(1); index < arguments.len(); ++index) {
        command.arg(arguments[index].as_str());
    }
    if (working_directory.is_some()) command.current_dir(*working_directory);
    apply_command_environment(command, environment, overrides);

    auto started = rstd::time::Instant::now();
    auto output  = command.output(observer);
    auto elapsed = started.elapsed();
    if (output.is_err()) {
        return Err(SystemError::Io(String::make("failed to execute"_str),
                                   rstd::move(program),
                                   rstd::move(output).unwrap_err()));
    }
    return decode_command_output(rstd::move(output).unwrap(), elapsed);
}

auto run_command_with_input(const Vec<String>&                arguments,
                            ref<str>                          standard_input,
                            const ResolvedProcessEnvironment& environment,
                            Option<ref<rstd::path::Path>>     working_directory = None(),
                            Option<ref<CommandEnvironment>>   overrides         = None())
    -> SystemResult<CommandOutput> {
    if (arguments.is_empty()) {
        return Err(SystemError::InvalidCommand(String::make("empty command"_str)));
    }
    auto program = PathBuf::from(arguments[usize {}].as_str());
    if (! program.as_path().is_absolute()) {
        return Err(SystemError::InvalidCommand(rstd::format(
            "command program '{}' is not an absolute path", arguments[usize {}].as_str())));
    }

    auto command = rstd::process::Command::make(arguments[usize {}].as_str());
    for (auto index = usize(1); index < arguments.len(); ++index) {
        command.arg(arguments[index].as_str());
    }
    if (working_directory.is_some()) command.current_dir(*working_directory);
    apply_command_environment(command, environment, overrides);
    command.set_stdin(rstd::process::Stdio::piped());
    command.set_stdout(rstd::process::Stdio::piped());
    command.set_stderr(rstd::process::Stdio::piped());

    auto started = rstd::time::Instant::now();
    auto spawned = command.spawn();
    if (spawned.is_err()) {
        return Err(SystemError::Io(String::make("failed to execute"_str),
                                   rstd::move(program),
                                   rstd::move(spawned).unwrap_err()));
    }
    auto child = rstd::move(spawned).unwrap();
    {
        auto input = child.take_stdin();
        if (input.is_none()) {
            return Err(
                SystemError::InvalidCommand(String::make("command stdin pipe is unavailable"_str)));
        }
        auto written = rstd::io::write_all(*input, standard_input.as_bytes());
        if (written.is_err()) {
            return Err(SystemError::Io(String::make("failed to write command stdin"_str),
                                       PathBuf::make(),
                                       rstd::move(written).unwrap_err()));
        }
    }
    auto output = child.wait_with_output();
    if (output.is_err()) {
        return Err(SystemError::Io(String::make("failed to collect command output"_str),
                                   PathBuf::make(),
                                   rstd::move(output).unwrap_err()));
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

auto tokenize_command_fragments(ref<str> input, ref<str> context) -> SystemResult<Vec<String>> {
    auto result      = Vec<String>::make();
    auto current     = Vec<u8>::make();
    auto quote       = FragmentQuote::None;
    auto escaping    = false;
    auto word_active = false;
    for (auto byte : input.as_bytes()) {
        if (byte == u8()) {
            return Err(
                SystemError::Fragment(String::make(context), String::make("contains NUL"_str)));
        }
        if (escaping) {
            if (quote == FragmentQuote::Double && byte != u8('"') && byte != u8('\\') &&
                byte != u8('$') && byte != u8('`') && byte != u8('\n')) {
                current.emplace_back(u8('\\'));
            }
            if (quote == FragmentQuote::Double && byte == u8('\n')) {
                escaping = false;
                continue;
            }
            current.emplace_back(byte);
            escaping    = false;
            word_active = true;
            continue;
        }
        if (quote == FragmentQuote::Single) {
            if (byte == u8('\''))
                quote = FragmentQuote::None;
            else
                current.emplace_back(byte);
            word_active = true;
            continue;
        }
        if (quote == FragmentQuote::Double) {
            if (byte == u8('"')) {
                quote = FragmentQuote::None;
            } else if (byte == u8('\\')) {
                escaping = true;
            } else {
                current.emplace_back(byte);
            }
            word_active = true;
            continue;
        }
        if (byte == u8('\\')) {
            escaping    = true;
            word_active = true;
        } else if (byte == u8('\'')) {
            quote       = FragmentQuote::Single;
            word_active = true;
        } else if (byte == u8('"')) {
            quote       = FragmentQuote::Double;
            word_active = true;
        } else if (byte == u8(' ') || byte == u8('\t') || byte == u8('\n') || byte == u8('\r')) {
            if (word_active) {
                auto pushed = push_fragment_word(result, current, context);
                if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
                word_active = false;
            }
        } else {
            current.emplace_back(byte);
            word_active = true;
        }
    }
    if (escaping) {
        return Err(
            SystemError::Fragment(String::make(context), String::make("ends with an escape"_str)));
    }
    if (quote != FragmentQuote::None) {
        return Err(SystemError::Fragment(String::make(context),
                                         String::make("contains an unclosed quote"_str)));
    }
    if (word_active) {
        auto pushed = push_fragment_word(result, current, context);
        if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
    }
    return Ok(rstd::move(result));
}

auto tokenize_windows_command_fragments(ref<str> input, ref<str> context)
    -> SystemResult<Vec<String>> {
    auto result      = Vec<String>::make();
    auto current     = Vec<u8>::make();
    auto quoted      = false;
    auto word_active = false;
    auto bytes       = input.as_bytes();
    auto index       = usize {};
    while (index < bytes.len()) {
        auto value = bytes[index];
        if (value == u8()) {
            return Err(
                SystemError::Fragment(String::make(context), String::make("contains NUL"_str)));
        }
        if (value == u8('\\')) {
            auto slashes = usize {};
            while (index < bytes.len() && bytes[index] == u8('\\')) {
                ++slashes;
                ++index;
            }
            if (index < bytes.len() && bytes[index] == u8('"')) {
                for (usize slash {}; slash < slashes / usize(2); ++slash) {
                    current.emplace_back(u8('\\'));
                }
                if (slashes % usize(2) == usize {})
                    quoted = ! quoted;
                else
                    current.emplace_back(u8('"'));
                word_active = true;
                ++index;
                continue;
            }
            for (usize slash {}; slash < slashes; ++slash) current.emplace_back(u8('\\'));
            word_active = true;
            continue;
        }
        if (value == u8('"')) {
            quoted      = ! quoted;
            word_active = true;
            ++index;
            continue;
        }
        if (! quoted &&
            (value == u8(' ') || value == u8('\t') || value == u8('\n') || value == u8('\r'))) {
            if (word_active) {
                auto pushed = push_fragment_word(result, current, context);
                if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
                word_active = false;
            }
            ++index;
            continue;
        }
        current.emplace_back(value);
        word_active = true;
        ++index;
    }
    if (quoted) {
        return Err(SystemError::Fragment(String::make(context),
                                         String::make("contains an unclosed quote"_str)));
    }
    if (word_active) {
        auto pushed = push_fragment_word(result, current, context);
        if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
    }
    return Ok(rstd::move(result));
}

} // namespace lito::system
