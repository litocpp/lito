export module tenon.toolchain.clang_scan_deps;

import rstd;
import rstd.json;
import tenon.model;
import tenon.process;
import tenon.toolchain.clang_options;
import tenon.toolchain.clang_scan_deps_options;
import tenon.toolchain.command;

using namespace rstd::prelude;
using namespace rstd::literals;
using Json      = rstd::json::Value;
using StringSet = rstd::collections::BTreeMap<String, empty>;

namespace tenon::toolchain
{

template<typename T>
auto failure(String message) -> Result<T> {
    return Err(Error::make(ErrorKind::Toolchain, rstd::move(message)));
}

template<typename T>
auto failure(ref<str> message) -> Result<T> {
    return Err(Error::make(ErrorKind::Toolchain, message));
}

auto member(const Json& value, ref<str> key) -> Option<ref<Json>> {
    return value.get(key);
}

auto json_string(const Json& value, ref<str> context) -> Result<String> {
    auto text = value.as_str();
    if (text.is_none()) return failure<String>(rstd::format("{} must be a string", context));
    return Ok(String::make(*text));
}

auto required_json_string(const Json& object,
                          ref<str> key,
                          ref<str> context) -> Result<String> {
    auto value = member(object, key);
    if (value.is_none()) {
        return failure<String>(rstd::format("{} is missing '{}'", context, key));
    }
    return json_string(**value, rstd::format("{}.{}", context, key).as_str());
}

auto read_file(ref<rstd::path::Path> path) -> Result<String> {
    auto contents = rstd::fs::read_to_string(path);
    if (contents.is_err()) {
        return failure<String>(
            rstd::format("cannot read '{}': {}", path, rstd::move(contents).unwrap_err()));
    }
    return Ok(rstd::move(contents).unwrap());
}

auto parse_depfile(ref<rstd::path::Path> depfile,
                   ref<rstd::path::Path> working_directory) -> Result<Vec<PathBuf>> {
    auto contents_result = read_file(depfile);
    if (contents_result.is_err()) return Err(rstd::move(contents_result).unwrap_err());
    auto contents = rstd::move(contents_result).unwrap();
    auto bytes    = contents.as_str().as_bytes();

    auto colon   = Option<usize> {};
    auto escaped = false;
    for (auto index = usize {}; index < contents.len(); ++index) {
        const auto value = bytes[index];
        if (! escaped && value == u8(':')) {
            colon = Some(index);
            break;
        }
        if (! escaped && value == u8('\\')) {
            escaped = true;
        } else {
            escaped = false;
        }
    }
    if (colon.is_none()) {
        return failure<Vec<PathBuf>>(
            rstd::format("invalid dependency file '{}': missing ':'", depfile));
    }

    auto tokens  = Vec<String>::make();
    auto current = String::make();
    for (auto index = *colon + usize(1); index < contents.len(); ++index) {
        const auto value = bytes[index];
        if (value == u8('\\') && index + usize(1) < contents.len()) {
            const auto next = bytes[index + usize(1)];
            if (next == u8('\n')) {
                ++index;
                continue;
            }
            if (next == u8('\r') && index + usize(2) < contents.len() &&
                bytes[index + usize(2)] == u8('\n')) {
                index += usize(2);
                continue;
            }
            current.push_ascii(next);
            ++index;
            continue;
        }
        if (value == u8(' ') || value == u8('\t') || value == u8('\r') ||
            value == u8('\n')) {
            if (! current.is_empty()) {
                tokens.push(rstd::move(current));
                current = String::make();
            }
            continue;
        }
        current.push_ascii(value);
    }
    if (! current.is_empty()) tokens.push(rstd::move(current));

    auto result = Vec<PathBuf>::make();
    auto seen   = StringSet::make();
    for (const auto& token : tokens) {
        auto path = PathBuf::from(token.as_str());
        if (path.as_path().is_relative()) {
            path = PathBuf::from(working_directory).join(path.as_path());
        }
        auto canonical = rstd::fs::canonicalize(path.as_path());
        if (canonical.is_err()) {
            return failure<Vec<PathBuf>>(rstd::format(
                "cannot resolve dependency input '{}': {}",
                path.as_path(),
                rstd::move(canonical).unwrap_err()));
        }
        auto resolved = rstd::move(canonical).unwrap();
        auto text = resolved.as_path().to_str();
        if (text.is_none()) {
            return failure<Vec<PathBuf>>(
                rstd::format("dependency input '{}' is not valid UTF-8", resolved.as_path()));
        }
        if (! seen.contains_key(*text)) {
            seen.insert(String::make(*text), empty {});
            result.push(rstd::move(resolved));
        }
    }
    return Ok(rstd::move(result));
}

auto parse_scan_json(ref<str> output, UnitId unit) -> Result<ScanResult> {
    auto parsed = rstd::json::from_str(output);
    if (parsed.is_err()) {
        return failure<ScanResult>(rstd::format(
            "cannot parse clang-scan-deps P1689 output: {}", rstd::move(parsed).unwrap_err()));
    }
    auto document = rstd::move(parsed).unwrap();
    auto rules    = member(document, "rules"_str);
    if (rules.is_none()) {
        return failure<ScanResult>("P1689 output must contain exactly one rule"_str);
    }
    auto rules_array = (**rules).as_array();
    if (rules_array.is_none() || (**rules_array).len() != usize(1)) {
        return failure<ScanResult>("P1689 output must contain exactly one rule"_str);
    }
    const auto& rule = (**rules_array)[usize {}];
    auto result      = ScanResult { .unit = unit };

    auto provides = member(rule, "provides"_str);
    if (provides.is_some()) {
        auto array = (**provides).as_array();
        if (array.is_none() || (**array).len() != usize(1)) {
            return failure<ScanResult>("P1689 provides must contain exactly one module"_str);
        }
        const auto& provided = (**array)[usize {}];
        auto logical_name =
            required_json_string(provided, "logical-name"_str, "P1689 provides"_str);
        if (logical_name.is_err()) return Err(rstd::move(logical_name).unwrap_err());
        auto interface_value = member(provided, "is-interface"_str);
        if (interface_value.is_none()) {
            return failure<ScanResult>("P1689 provides.is-interface must be a bool"_str);
        }
        auto interface_flag = (**interface_value).as_bool();
        if (interface_flag.is_none()) {
            return failure<ScanResult>("P1689 provides.is-interface must be a bool"_str);
        }
        result.provided = Some(ProvidedModule {
            .logical_name = rstd::move(logical_name).unwrap(),
            .is_interface = *interface_flag,
        });
    }

    auto requirements = member(rule, "requires"_str);
    if (requirements.is_some()) {
        auto array = (**requirements).as_array();
        if (array.is_none()) {
            return failure<ScanResult>("P1689 requires must be an array"_str);
        }
        auto names = StringSet::make();
        for (const auto& required : **array) {
            auto logical_name =
                required_json_string(required, "logical-name"_str, "P1689 requires"_str);
            if (logical_name.is_err()) return Err(rstd::move(logical_name).unwrap_err());
            auto name = rstd::move(logical_name).unwrap();
            if (! names.contains_key(name.as_str())) {
                names.insert(name.clone(), empty {});
                result.required_modules.push(rstd::move(name));
            }
        }
    }
    return Ok(rstd::move(result));
}

} // namespace tenon::toolchain

export namespace tenon::toolchain
{

class ClangScanDeps {
public:
    static auto create(ref<rstd::path::Path> scanner_path) -> Result<ClangScanDeps> {

        auto scanner = command::resolve_tool(scanner_path, "clang-scan-deps"_str);
        if (scanner.is_err()) return Err(rstd::move(scanner).unwrap_err());
        auto path = rstd::move(scanner).unwrap();

        auto version_command = Vec<String>::make();
        auto pushed = command::push_path(version_command, path.as_path());
        if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
        command::push_option(version_command, clang_scan_deps_options::VERSION);
        auto version = command::tool_output(
            rstd::move(version_command), "clang-scan-deps --version"_str);
        if (version.is_err()) return Err(rstd::move(version).unwrap_err());

        return Ok(ClangScanDeps {
            rstd::move(path),
            rstd::move(version).unwrap(),
        });
    }

    auto path() const -> ref<rstd::path::Path> { return path_.as_path(); }
    auto version() const -> ref<str> { return version_.as_str(); }

    auto scan(const PreparedUnit& prepared, Vec<String> compiler_arguments) const
        -> Result<ScanResult> {

        auto arguments = Vec<String>::make();
        auto pushed = command::push_path(arguments, path_.as_path());
        if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
        command::push_option(arguments, clang_scan_deps_options::FORMAT_P1689);
        command::push_option(arguments, clang_scan_deps_options::DRIVER_ARGUMENTS);
        for (auto& argument : compiler_arguments) {
            arguments.push(rstd::move(argument));
        }
        command::push_option(arguments, clang_options::DEPENDENCIES);
        command::push_option(arguments, clang_options::DEPENDENCY_TARGET);
        pushed = command::push_path(arguments, prepared.unit.object.as_path());
        if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
        command::push_option(arguments, clang_options::DEPENDENCY_FILE);
        pushed = command::push_path(arguments, prepared.unit.depfile.as_path());
        if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
        command::push_option(arguments, clang_options::COMPILE);
        pushed = command::push_path(arguments, prepared.unit.source.as_path());
        if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
        command::push_option(arguments, clang_options::OUTPUT);
        pushed = command::push_path(arguments, prepared.unit.object.as_path());
        if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());

        auto output = run_command(arguments, Some(prepared.working_directory.as_path()));
        if (output.is_err()) return Err(rstd::move(output).unwrap_err());
        auto command_output = rstd::move(output).unwrap();
        if (command_output.exit_code != i32 {}) {
            return failure<ScanResult>(rstd::format(
                "clang-scan-deps failed for '{}'\n{}\n{}",
                prepared.unit.source.as_path(),
                command_text(arguments).as_str(),
                command_output.standard_error.as_str()));
        }
        auto scan_result = parse_scan_json(
            command_output.standard_output.as_str(), prepared.unit.id);
        if (scan_result.is_err()) return scan_result;
        auto headers = parse_depfile(
            prepared.unit.depfile.as_path(), prepared.working_directory.as_path());
        if (headers.is_err()) return Err(rstd::move(headers).unwrap_err());
        scan_result->header_inputs = rstd::move(headers).unwrap();
        return scan_result;
    }

private:
    ClangScanDeps(PathBuf path, String version)
        : path_(rstd::move(path)), version_(rstd::move(version)) {}

    PathBuf path_;
    String  version_;
};

} // namespace tenon::toolchain
