module;
#include <rstd/macro.hpp>

export module lito.tools.cargo:executor;

import rstd;
import rstd.json;
import lito.crypto;
import lito.system;
import lito.tools;
import :model;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito::system;
using PathBuf   = rstd::path::PathBuf;
using Json      = rstd::json::Value;
using JsonArray = rstd::json::Array;

export namespace lito::tools::cargo
{

template<typename T>
auto cargo_failure(String message) -> lito::tools::ToolResult<T> {
    return Err(lito::tools::ToolError::Message(rstd::move(message)));
}

template<typename T>
auto cargo_failure(ref<str> message) -> lito::tools::ToolResult<T> {
    return cargo_failure<T>(String::make(message));
}

template<typename T>
auto cargo_io_failure(ref<str> operation, ref<rstd::path::Path> path, rstd::io::error::Error source)
    -> lito::tools::ToolResult<T> {
    return Err(lito::tools::ToolError::Io(
        String::make(operation), PathBuf::from(path), rstd::move(source)));
}

auto emit_cargo(const Option<EventSink>& observer,
                EventKind                kind,
                ref<str>                 alias,
                ref<rstd::path::Path>    path,
                rstd::time::Duration     elapsed   = {},
                bool                     completed = false) noexcept -> void {
    if (observer.is_none() || observer->notify == nullptr) return;
    observer->notify(observer->context, Event { kind, alias, path, elapsed, completed });
}

auto cargo_path_text(ref<rstd::path::Path> path, ref<str> context)
    -> lito::tools::ToolResult<String> {
    auto text = path.to_str();
    if (text.is_none()) {
        return cargo_failure<String>(
            rstd::format("{} path '{}' is not valid UTF-8", context, path));
    }
    return Ok(String::make(*text));
}

auto cargo_profile_environment_prefix(const ProfileConfiguration& profile) -> String {
    auto result = String::make("CARGO_PROFILE_"_str);
    for (auto byte : profile.selected.as_str().as_bytes()) {
        if (byte >= u8('a') && byte <= u8('z')) {
            result.push_ascii(byte - u8('a') + u8('A'));
        } else {
            result.push_ascii(byte == u8('-') ? u8('_') : byte);
        }
    }
    result.push_ascii('_');
    return result;
}

auto cargo_environment(const ProfileConfiguration* profile = nullptr) -> CommandEnvironment {
    constexpr ref<str> variables[] = {
        "CARGO_TARGET_DIR"_str,
        "CARGO_BUILD_TARGET_DIR"_str,
        "CARGO_BUILD_TARGET"_str,
        "RUSTFLAGS"_str,
        "CARGO_ENCODED_RUSTFLAGS"_str,
        "CARGO_BUILD_RUSTFLAGS"_str,
        "RUSTC"_str,
        "RUSTC_WRAPPER"_str,
        "RUSTC_WORKSPACE_WRAPPER"_str,
        "CARGO_BUILD_RUSTC"_str,
        "CARGO_BUILD_RUSTC_WRAPPER"_str,
        "CARGO_BUILD_RUSTC_WORKSPACE_WRAPPER"_str,
    };
    auto result = CommandEnvironment {};
    for (auto variable : variables) {
        result.entries.push(CommandEnvironmentEntry {
            .key = String::make(variable),
        });
    }
    if (profile != nullptr) {
        auto               prefix     = cargo_profile_environment_prefix(*profile);
        constexpr ref<str> settings[] = {
            "OPT_LEVEL"_str, "DEBUG"_str, "LTO"_str, "DEBUG_ASSERTIONS"_str, "STRIP"_str,
        };
        for (auto setting : settings) {
            auto key = prefix.clone();
            key.push_str(setting);
            result.entries.push(CommandEnvironmentEntry { .key = rstd::move(key) });
        }
    }
    return result;
}

auto invoke_cargo(const Vec<String>&                arguments,
                  const ResolvedProcessEnvironment& environment,
                  Option<ref<rstd::path::Path>>     working_directory   = None(),
                  bool                              forward_diagnostics = false,
                  const ProfileConfiguration*       profile             = nullptr)
    -> lito::tools::ToolResult<CommandOutput> {
    auto overrides    = cargo_environment(profile);
    auto override_ref = Some(ref<CommandEnvironment>::from_raw_parts(rstd::addressof(overrides)));
    auto output       = [&]() {
        if (! forward_diagnostics) {
            return run_command(arguments, environment, working_directory, override_ref);
        }
        auto observer = rstd::process::OutputObserver {
            .notify =
                +[](void*, rstd::process::OutputStream stream, slice<u8> chunk) noexcept {
                    if (stream != rstd::process::OutputStream::Stderr) return;
                    auto output = rstd::io::stderr();
                    (void)rstd::io::write_all(output, chunk);
                },
        };
        return run_command_observed(
            arguments, environment, observer, working_directory, override_ref);
    }();
    if (output.is_err()) {
        return Err(rstd::into<lito::tools::ToolError>(rstd::move(output).unwrap_err()));
    }
    return Ok(rstd::move(output).unwrap());
}

auto cargo_profile_string(ref<str> value) -> String {
    auto result = String::make("\""_str);
    result.push_str(value);
    result.push_ascii('"');
    return result;
}

auto append_cargo_profile_config(Vec<String>& arguments,
                                 ref<str>     profile,
                                 ref<str>     key,
                                 String       value) -> void {
    arguments.push(String::make("--config"_str));
    arguments.push(rstd::format("profile.{}.{}={}", profile, key, value.as_str()));
}

auto append_cargo_profile_arguments(Vec<String>& arguments, const ProfileConfiguration& profile)
    -> lito::tools::ToolResult<empty> {
    if (profile.selected.as_str().is_empty() || profile.inherits.as_str().is_empty()) {
        return cargo_failure<empty>("Cargo profile configuration names must not be empty"_str);
    }
    auto selected = profile.selected.as_str();
    append_cargo_profile_config(
        arguments, selected, "inherits"_str, cargo_profile_string(profile.inherits.as_str()));
    if (profile.optimization.is_some()) {
        auto value = profile_optimization_name(*profile.optimization);
        append_cargo_profile_config(arguments,
                                    selected,
                                    "opt-level"_str,
                                    value == "s"_str || value == "z"_str
                                        ? cargo_profile_string(value)
                                        : String::make(value));
    }
    if (profile.debug_info.is_some()) {
        append_cargo_profile_config(
            arguments,
            selected,
            "debug"_str,
            cargo_profile_string(profile_debug_info_name(*profile.debug_info)));
    }
    if (profile.lto.is_some()) {
        auto value = *profile.lto == ProfileLto::Off
                         ? String::make("false"_str)
                         : cargo_profile_string(profile_lto_name(*profile.lto));
        append_cargo_profile_config(arguments, selected, "lto"_str, rstd::move(value));
    }
    if (profile.debug_assertions.is_some()) {
        append_cargo_profile_config(
            arguments,
            selected,
            "debug-assertions"_str,
            String::make(*profile.debug_assertions ? "true"_str : "false"_str));
    }
    if (profile.strip.is_some()) {
        append_cargo_profile_config(arguments,
                                    selected,
                                    "strip"_str,
                                    cargo_profile_string(profile_strip_name(*profile.strip)));
    }
    return Ok(empty {});
}

auto required_member(const Json& value, ref<str> key, ref<str> context)
    -> lito::tools::ToolResult<ref<Json>> {
    auto member = value.get(key);
    if (member.is_none()) {
        return cargo_failure<ref<Json>>(rstd::format("{} is missing '{}'", context, key));
    }
    return Ok(*member);
}

auto required_string(const Json& value, ref<str> key, ref<str> context)
    -> lito::tools::ToolResult<ref<str>> {
    auto member = rstd_try(required_member(value, key, context));
    auto text   = member->as_str();
    if (text.is_none()) {
        return cargo_failure<ref<str>>(rstd::format("{}.{} must be a string", context, key));
    }
    return Ok(*text);
}

auto required_array(const Json& value, ref<str> key, ref<str> context)
    -> lito::tools::ToolResult<ref<JsonArray>> {
    auto member = rstd_try(required_member(value, key, context));
    auto array  = member->as_array();
    if (array.is_none()) {
        return cargo_failure<ref<JsonArray>>(rstd::format("{}.{} must be an array", context, key));
    }
    return Ok(*array);
}

auto required_bool(const Json& value, ref<str> key, ref<str> context)
    -> lito::tools::ToolResult<bool> {
    auto member = rstd_try(required_member(value, key, context));
    auto parsed = member->as_bool();
    if (parsed.is_none()) {
        return cargo_failure<bool>(rstd::format("{}.{} must be a boolean", context, key));
    }
    return Ok(*parsed);
}

auto string_array_contains(const JsonArray& values, ref<str> expected, ref<str> context)
    -> lito::tools::ToolResult<bool> {
    for (const auto& value : values) {
        auto text = value.as_str();
        if (text.is_none()) {
            return cargo_failure<bool>(rstd::format("{} contains a non-string value", context));
        }
        if (*text == expected) return Ok(true);
    }
    return Ok(false);
}

auto string_array(const JsonArray& values, ref<str> context)
    -> lito::tools::ToolResult<Vec<String>> {
    auto result = Vec<String>::with_capacity(values.len());
    for (const auto& value : values) {
        auto text = value.as_str();
        if (text.is_none()) {
            return cargo_failure<Vec<String>>(
                rstd::format("{} contains a non-string value", context));
        }
        result.push(String::make(*text));
    }
    return Ok(rstd::move(result));
}

auto canonical_owned_path(ref<rstd::path::Path> path,
                          ref<rstd::path::Path> owner,
                          ref<str>              context,
                          bool                  file) -> lito::tools::ToolResult<PathBuf> {
    if (file) {
        auto direct = rstd::fs::symlink_metadata(path);
        if (direct.is_err()) {
            return cargo_io_failure<PathBuf>(rstd::format("inspect {}", context).as_str(),
                                             path,
                                             rstd::move(direct).unwrap_err());
        }
        if (! direct->is_file() || direct->is_symlink()) {
            return cargo_failure<PathBuf>(
                rstd::format("{} '{}' must be a regular non-symlink file", context, path));
        }
    }
    auto root = rstd::fs::canonicalize(owner);
    if (root.is_err()) {
        return cargo_io_failure<PathBuf>(rstd::format("resolve {} owner", context).as_str(),
                                         owner,
                                         rstd::move(root).unwrap_err());
    }
    auto resolved = rstd::fs::canonicalize(path);
    if (resolved.is_err()) {
        return cargo_io_failure<PathBuf>(
            rstd::format("resolve {}", context).as_str(), path, rstd::move(resolved).unwrap_err());
    }
    if (resolved->as_path().strip_prefix(root->as_path()).is_none()) {
        return cargo_failure<PathBuf>(rstd::format(
            "{} '{}' escapes owner '{}'", context, resolved->as_path(), root->as_path()));
    }
    auto metadata = rstd::fs::metadata(resolved->as_path());
    if (metadata.is_err()) {
        return cargo_io_failure<PathBuf>(rstd::format("inspect {}", context).as_str(),
                                         resolved->as_path(),
                                         rstd::move(metadata).unwrap_err());
    }
    if ((file && ! metadata->is_file()) || (! file && ! metadata->is_dir())) {
        return cargo_failure<PathBuf>(rstd::format("{} '{}' is not {}",
                                                   context,
                                                   resolved->as_path(),
                                                   file ? "a file"_str : "a directory"_str));
    }
    return Ok(rstd::move(resolved).unwrap());
}

auto identify_provider(PathBuf executable, const ResolvedProcessEnvironment& environment)
    -> lito::tools::ToolResult<Provider> {
    auto program   = rstd_try(cargo_path_text(executable.as_path(), "Cargo executable"_str));
    auto arguments = Vec<String>::make();
    arguments.push(rstd::move(program));
    arguments.push(String::make("--version"_str));
    arguments.push(String::make("--verbose"_str));
    auto output = rstd_try(invoke_cargo(arguments, environment));
    if (output.exit_code != i32 {}) {
        return Err(lito::tools::ToolError::Execution(String::make("Cargo provider identity"_str),
                                                     output.exit_code,
                                                     rstd::move(output.standard_output),
                                                     rstd::move(output.standard_error)));
    }
    auto identity = String::make(output.standard_output.as_str().trim_ascii());
    if (identity.is_empty()) {
        return cargo_failure<Provider>("Cargo provider returned an empty identity"_str);
    }
    auto host      = Option<String> {};
    auto remaining = identity.as_str();
    while (! remaining.is_empty()) {
        auto split = remaining.split_once("\n"_str);
        auto line  = split.is_some() ? split->template get<0>() : remaining;
        if (line.starts_with("host:"_str)) {
            auto value = line.strip_prefix("host:"_str).unwrap().trim_ascii();
            if (! value.is_empty()) host = Some(String::make(value));
        }
        if (split.is_none()) break;
        remaining = split->template get<1>();
    }
    if (host.is_none()) {
        return cargo_failure<Provider>("Cargo provider identity is missing host target"_str);
    }
    return Ok(Provider {
        .executable  = rstd::move(executable),
        .identity    = rstd::move(identity),
        .host_target = rstd::move(host).unwrap(),
    });
}

auto query_metadata(const Provider&                   provider,
                    const MetadataRequest&            request,
                    const ResolvedProcessEnvironment& environment,
                    const Option<EventSink>&          observer = None())
    -> lito::tools::ToolResult<PackageMetadata> {
    auto source_root = rstd_try(canonical_owned_path(request.source_root.as_path(),
                                                     request.source_root.as_path(),
                                                     "Cargo source root"_str,
                                                     false));
    auto manifest    = rstd_try(canonical_owned_path(
        request.manifest.as_path(), source_root.as_path(), "Cargo manifest"_str, true));
    auto arguments   = Vec<String>::make();
    arguments.push(
        rstd_try(cargo_path_text(provider.executable.as_path(), "Cargo executable"_str)));
    arguments.push(String::make("metadata"_str));
    arguments.push(String::make("--format-version"_str));
    arguments.push(String::make("1"_str));
    arguments.push(String::make("--no-deps"_str));
    arguments.push(String::make("--manifest-path"_str));
    arguments.push(rstd_try(cargo_path_text(manifest.as_path(), "Cargo manifest"_str)));
    arguments.push(String::make("--locked"_str));
    if (request.offline) arguments.push(String::make("--offline"_str));
    emit_cargo(observer, EventKind::Metadata, request.package.as_str(), manifest.as_path());
    auto output = rstd_try(invoke_cargo(arguments, environment, Some(source_root.as_path())));
    emit_cargo(observer,
               EventKind::Metadata,
               request.package.as_str(),
               manifest.as_path(),
               output.elapsed,
               true);
    if (output.exit_code != i32 {}) {
        return Err(lito::tools::ToolError::Execution(String::make("Cargo metadata"_str),
                                                     output.exit_code,
                                                     rstd::move(output.standard_output),
                                                     rstd::move(output.standard_error)));
    }
    auto document = rstd::json::from_str(output.standard_output.as_str());
    if (document.is_err()) {
        return cargo_failure<PackageMetadata>(rstd::format("cannot parse Cargo metadata JSON: {}",
                                                           rstd::move(document).unwrap_err()));
    }
    auto value          = rstd::move(document).unwrap();
    auto format_version = rstd_try(required_member(value, "version"_str, "Cargo metadata"_str));
    if (format_version->as_u64() != Some(u64(1))) {
        return cargo_failure<PackageMetadata>("Cargo metadata.version must be the integer 1"_str);
    }
    auto workspace_text =
        rstd_try(required_string(value, "workspace_root"_str, "Cargo metadata"_str));
    auto workspace = rstd_try(canonical_owned_path(PathBuf::from(workspace_text).as_path(),
                                                   source_root.as_path(),
                                                   "Cargo workspace root"_str,
                                                   false));
    auto lock      = workspace.join(PathBuf::from("Cargo.lock"_str).as_path());
    lock           = rstd_try(
        canonical_owned_path(lock.as_path(), workspace.as_path(), "Cargo lock file"_str, true));
    auto packages = rstd_try(required_array(value, "packages"_str, "Cargo metadata"_str));
    auto selected = Option<ref<Json>> {};
    for (const auto& package : *packages) {
        auto name = rstd_try(required_string(package, "name"_str, "Cargo package"_str));
        if (name != request.package.as_str()) continue;
        if (selected.is_some()) {
            return cargo_failure<PackageMetadata>(rstd::format(
                "Cargo metadata contains more than one package named '{}'", request.package));
        }
        selected = Some(ref<Json>::from_raw_parts(rstd::addressof(package)));
    }
    if (selected.is_none()) {
        return cargo_failure<PackageMetadata>(
            rstd::format("Cargo workspace contains no package named '{}'", request.package));
    }
    const auto& package = **selected;
    auto        id      = rstd_try(required_string(package, "id"_str, "Cargo package"_str));
    auto        name    = rstd_try(required_string(package, "name"_str, "Cargo package"_str));
    auto        version = rstd_try(required_string(package, "version"_str, "Cargo package"_str));
    auto        package_manifest_text =
        rstd_try(required_string(package, "manifest_path"_str, "Cargo package"_str));
    auto package_manifest =
        rstd_try(canonical_owned_path(PathBuf::from(package_manifest_text).as_path(),
                                      source_root.as_path(),
                                      "Cargo package manifest"_str,
                                      true));
    auto targets  = rstd_try(required_array(package, "targets"_str, "Cargo package"_str));
    auto library  = Option<TargetMetadata> {};
    auto binaries = Vec<TargetMetadata>::make();
    for (const auto& target : *targets) {
        auto kinds       = rstd_try(required_array(target, "kind"_str, "Cargo target"_str));
        auto crate_types = rstd_try(required_array(target, "crate_types"_str, "Cargo target"_str));
        auto metadata    = TargetMetadata {
            .name = String::make(rstd_try(required_string(target, "name"_str, "Cargo target"_str))),
            .crate_types = rstd_try(string_array(*crate_types, "Cargo target.crate_types"_str)),
        };
        auto is_library =
            rstd_try(string_array_contains(*kinds, "lib"_str, "Cargo target.kind"_str)) ||
            rstd_try(string_array_contains(*kinds, "rlib"_str, "Cargo target.kind"_str)) ||
            rstd_try(string_array_contains(*kinds, "dylib"_str, "Cargo target.kind"_str)) ||
            rstd_try(string_array_contains(*kinds, "cdylib"_str, "Cargo target.kind"_str)) ||
            rstd_try(string_array_contains(*kinds, "staticlib"_str, "Cargo target.kind"_str));
        if (is_library && library.is_some()) {
            return cargo_failure<PackageMetadata>(rstd::format(
                "Cargo package '{}' contains more than one library target", request.package));
        }
        if (is_library) library = Some(metadata.clone());
        if (rstd_try(string_array_contains(*kinds, "bin"_str, "Cargo target.kind"_str))) {
            for (const auto& existing : binaries) {
                if (existing.name == metadata.name.as_str()) {
                    return cargo_failure<PackageMetadata>(
                        rstd::format("Cargo package '{}' repeats binary target '{}'",
                                     request.package,
                                     metadata.name.as_str()));
                }
            }
            binaries.push(rstd::move(metadata));
        }
    }
    rstd::slice_::sort_unstable_by(binaries.as_mut_slice().as_mut_ref(),
                                   [](const TargetMetadata& left, const TargetMetadata& right) {
                                       return left.name < right.name;
                                   });
    return Ok(PackageMetadata {
        .id             = String::make(id),
        .name           = String::make(name),
        .version        = String::make(version),
        .library        = rstd::move(library),
        .binaries       = rstd::move(binaries),
        .source_root    = rstd::move(source_root),
        .workspace_root = rstd::move(workspace),
        .manifest       = rstd::move(package_manifest),
        .lock_file      = rstd::move(lock),
    });
}

struct ParsedBuildMessages {
    Option<PathBuf> archive;
    Vec<String>     native_arguments;
    bool            artifact_fresh { false };
    bool            found_artifact { false };
    bool            found_finished { false };
};

auto parse_native_arguments(ref<str> message) -> lito::tools::ToolResult<Vec<String>> {
    auto payload = message.strip_prefix("native-static-libs:"_str);
    if (payload.is_none()) {
        return cargo_failure<Vec<String>>("Cargo native library note has an invalid prefix"_str);
    }
#if RSTD_OS_WINDOWS
    auto parsed =
        tokenize_windows_command_fragments(payload->trim_ascii(), "Cargo native-static-libs"_str);
#else
    auto parsed = tokenize_command_fragments(payload->trim_ascii(), "Cargo native-static-libs"_str);
#endif
    if (parsed.is_err()) {
        return Err(rstd::into<lito::tools::ToolError>(rstd::move(parsed).unwrap_err()));
    }
    const auto safe_name = [](ref<str> value) {
        if (value.is_empty()) return false;
        for (auto byte : value.as_bytes()) {
            const auto character = byte.to_primitive();
            const auto alpha =
                (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z');
            const auto digit = character >= '0' && character <= '9';
            if (! (alpha || digit || character == '_' || character == '+' || character == '.' ||
                   character == '-')) {
                return false;
            }
        }
        return true;
    };
    auto framework_name = false;
    for (const auto& token : *parsed) {
        if (token.is_empty() || token.as_str().starts_with("@"_str) ||
            token.as_str().contains("\0"_str)) {
            return cargo_failure<Vec<String>>(
                rstd::format("Cargo native-static-libs contains unsupported token '{}'", token));
        }
        if (framework_name) {
            if (! safe_name(token.as_str())) {
                return cargo_failure<Vec<String>>(rstd::format(
                    "Cargo native-static-libs contains invalid framework name '{}'", token));
            }
            framework_name = false;
            continue;
        }
        if (token.as_str() == "-framework"_str) {
            framework_name = true;
            continue;
        }
        if (auto library = token.as_str().strip_prefix("-l"_str);
            library.is_some() && safe_name(**library)) {
            continue;
        }
        auto path = PathBuf::from(token.as_str());
        if (path.as_path().is_absolute()) continue;
        if (token.as_str().ends_with(".lib"_str) && safe_name(token.as_str())) continue;
        return cargo_failure<Vec<String>>(
            rstd::format("Cargo native-static-libs contains unsupported token '{}'", token));
    }
    if (framework_name) {
        return cargo_failure<Vec<String>>("Cargo native-static-libs ends after '-framework'"_str);
    }
    return Ok(rstd::move(parsed).unwrap());
}

auto parse_rendered_native_arguments(ref<str> output) -> lito::tools::ToolResult<Vec<String>> {
    auto result    = Option<Vec<String>> {};
    auto remaining = output;
    while (! remaining.is_empty()) {
        auto split  = remaining.split_once("\n"_str);
        auto line   = (split.is_some() ? split->template get<0>() : remaining).trim_ascii();
        auto native = line.split_once("native-static-libs:"_str);
        if (native.is_some()) {
            if (result.is_some()) {
                return cargo_failure<Vec<String>>(
                    "Cargo emitted duplicate native-static-libs notes"_str);
            }
            auto message = String::make("native-static-libs:"_str);
            message.push_str(native->template get<1>());
            result = Some(rstd_try(parse_native_arguments(message.as_str())));
        }
        if (split.is_none()) break;
        remaining = split->template get<1>();
    }
    if (result.is_none()) {
        return cargo_failure<Vec<String>>("Cargo emitted no native-static-libs note"_str);
    }
    return Ok(rstd::move(result).unwrap());
}

auto parse_build_message(const Json&            message,
                         const PackageMetadata& metadata,
                         const BuildRequest&    request,
                         ref<str>               archive_suffix,
                         ParsedBuildMessages&   result) -> lito::tools::ToolResult<empty> {
    auto reason = rstd_try(required_string(message, "reason"_str, "Cargo build message"_str));
    if (reason == "compiler-artifact"_str) {
        auto package =
            rstd_try(required_string(message, "package_id"_str, "Cargo compiler-artifact"_str));
        if (package != metadata.id.as_str()) return Ok(empty {});
        auto target =
            rstd_try(required_member(message, "target"_str, "Cargo compiler-artifact"_str));
        auto name = rstd_try(required_string(*target, "name"_str, "Cargo artifact target"_str));
        if (metadata.library.is_none() || name != metadata.library->name.as_str())
            return Ok(empty {});
        auto crate_types =
            rstd_try(required_array(*target, "crate_types"_str, "Cargo artifact target"_str));
        if (! rstd_try(string_array_contains(
                *crate_types, "staticlib"_str, "Cargo artifact target.crate_types"_str))) {
            return cargo_failure<empty>("Cargo root artifact is not a staticlib"_str);
        }
        if (result.found_artifact) {
            return cargo_failure<empty>("Cargo emitted duplicate root staticlib artifacts"_str);
        }
        auto filenames =
            rstd_try(required_array(message, "filenames"_str, "Cargo compiler-artifact"_str));
        auto archive = Option<PathBuf> {};
        for (const auto& filename : *filenames) {
            auto text = filename.as_str();
            if (text.is_none()) {
                return cargo_failure<empty>("Cargo artifact filename must be a string"_str);
            }
            if (! text->ends_with(archive_suffix)) continue;
            if (archive.is_some()) {
                return cargo_failure<empty>(
                    "Cargo root artifact contains multiple static archives"_str);
            }
            archive = Some(PathBuf::from(*text));
        }
        if (archive.is_none()) {
            return cargo_failure<empty>(
                rstd::format("Cargo root artifact contains no '{}' archive", archive_suffix));
        }
        result.archive = rstd::move(archive);
        result.artifact_fresh =
            rstd_try(required_bool(message, "fresh"_str, "Cargo compiler-artifact"_str));
        result.found_artifact = true;
        return Ok(empty {});
    }
    if (reason == "build-finished"_str) {
        if (result.found_finished) {
            return cargo_failure<empty>("Cargo emitted duplicate build-finished messages"_str);
        }
        if (! rstd_try(required_bool(message, "success"_str, "Cargo build-finished"_str))) {
            return cargo_failure<empty>("Cargo build-finished reported failure"_str);
        }
        result.found_finished = true;
    }
    return Ok(empty {});
}

auto parse_build_messages(ref<str>               output,
                          ref<str>               diagnostics,
                          const PackageMetadata& metadata,
                          const BuildRequest&    request,
                          ref<str> archive_suffix) -> lito::tools::ToolResult<ParsedBuildMessages> {
    auto result    = ParsedBuildMessages {};
    auto remaining = output;
    while (! remaining.is_empty()) {
        auto split = remaining.split_once("\n"_str);
        auto line  = (split.is_some() ? split->template get<0>() : remaining).trim_ascii();
        if (! line.is_empty() && line.starts_with("{"_str)) {
            auto parsed = rstd::json::from_str(line);
            if (parsed.is_err()) {
                return cargo_failure<ParsedBuildMessages>(rstd::format(
                    "cannot parse Cargo build JSON message: {}", rstd::move(parsed).unwrap_err()));
            }
            auto message = rstd::move(parsed).unwrap();
            if (message.get("reason"_str).is_some()) {
                rstd_try(parse_build_message(message, metadata, request, archive_suffix, result));
            }
        }
        if (split.is_none()) break;
        remaining = split->template get<1>();
    }
    if (! result.found_artifact) {
        return cargo_failure<ParsedBuildMessages>("Cargo emitted no root staticlib artifact"_str);
    }
    result.native_arguments = rstd_try(parse_rendered_native_arguments(diagnostics));
    if (! result.found_finished) {
        return cargo_failure<ParsedBuildMessages>("Cargo emitted no build-finished message"_str);
    }
    return Ok(rstd::move(result));
}

auto build_static_library(const Provider&                   provider,
                          const PackageMetadata&            metadata,
                          const BuildRequest&               request,
                          ref<str>                          archive_suffix,
                          const ResolvedProcessEnvironment& environment,
                          const Option<EventSink>&          observer = None())
    -> lito::tools::ToolResult<StaticLibrarySnapshot> {
    if (request.jobs == usize {}) {
        return cargo_failure<StaticLibrarySnapshot>(
            "Cargo build jobs must be greater than zero"_str);
    }
    if (metadata.library.is_none()) {
        return cargo_failure<StaticLibrarySnapshot>(
            rstd::format("Cargo package '{}' contains no library target", metadata.name));
    }
    auto static_library = false;
    for (const auto& crate_type : metadata.library->crate_types) {
        if (crate_type.as_str() == "staticlib"_str) static_library = true;
    }
    if (! static_library) {
        return cargo_failure<StaticLibrarySnapshot>(rstd::format(
            "Cargo package '{}' library target '{}' does not declare crate-type 'staticlib'",
            metadata.name,
            metadata.library->name));
    }
    auto created = rstd::fs::create_dir_all(request.work_root.as_path());
    if (created.is_err()) {
        return cargo_io_failure<StaticLibrarySnapshot>("create Cargo work directory"_str,
                                                       request.work_root.as_path(),
                                                       rstd::move(created).unwrap_err());
    }
    auto lock_path = request.work_root.join(PathBuf::from("lock"_str).as_path());
    auto lock_file = rstd::fs::File::create(lock_path.as_path());
    if (lock_file.is_err()) {
        return cargo_io_failure<StaticLibrarySnapshot>("open Cargo dependency lock"_str,
                                                       lock_path.as_path(),
                                                       rstd::move(lock_file).unwrap_err());
    }
    auto locked = rstd::fs::FileLock::acquire(rstd::move(lock_file).unwrap(),
                                              rstd::fs::FileLockMode::Exclusive);
    if (locked.is_err()) {
        return cargo_io_failure<StaticLibrarySnapshot>(
            "lock Cargo dependency"_str, lock_path.as_path(), rstd::move(locked).unwrap_err());
    }
    auto arguments = Vec<String>::make();
    arguments.push(
        rstd_try(cargo_path_text(provider.executable.as_path(), "Cargo executable"_str)));
    rstd_try(append_cargo_profile_arguments(arguments, request.profile));
    arguments.push(String::make("rustc"_str));
    arguments.push(String::make("--manifest-path"_str));
    arguments.push(rstd_try(cargo_path_text(request.manifest.as_path(), "Cargo manifest"_str)));
    arguments.push(String::make("--package"_str));
    arguments.push(request.package.clone());
    arguments.push(String::make("--lib"_str));
    arguments.push(String::make("--profile"_str));
    arguments.push(request.profile.selected.value.clone());
    arguments.push(String::make("--target"_str));
    arguments.push(request.target.clone());
    arguments.push(String::make("--target-dir"_str));
    arguments.push(rstd_try(
        cargo_path_text(request.target_directory.as_path(), "Cargo target directory"_str)));
    arguments.push(String::make("--jobs"_str));
    arguments.push(rstd::format("{}", request.jobs));
    arguments.push(String::make("--message-format"_str));
    arguments.push(String::make("json-render-diagnostics"_str));
    arguments.push(String::make("--locked"_str));
    if (request.offline) arguments.push(String::make("--offline"_str));
    if (! request.features.is_empty()) {
        auto features = String::make();
        for (usize index {}; index < request.features.len(); ++index) {
            if (index != usize {}) features.push_ascii(u8(','));
            features.push_str(request.features[index].as_str());
        }
        arguments.push(String::make("--features"_str));
        arguments.push(rstd::move(features));
    }
    if (! request.default_features) {
        arguments.push(String::make("--no-default-features"_str));
    }
    arguments.push(String::make("--"_str));
    arguments.push(String::make("--print"_str));
    arguments.push(String::make("native-static-libs"_str));
    emit_cargo(observer, EventKind::Build, request.alias.as_str(), request.work_root.as_path());
    auto output = rstd_try(invoke_cargo(arguments,
                                        environment,
                                        Some(metadata.workspace_root.as_path()),
                                        true,
                                        rstd::addressof(request.profile)));
    if (output.exit_code != i32 {}) {
        return Err(lito::tools::ToolError::Execution(
            rstd::format("Cargo dependency '{}' build", request.alias),
            output.exit_code,
            rstd::move(output.standard_output),
            rstd::move(output.standard_error)));
    }
    auto messages = rstd_try(parse_build_messages(output.standard_output.as_str(),
                                                  output.standard_error.as_str(),
                                                  metadata,
                                                  request,
                                                  archive_suffix));
    auto archive  = rstd_try(canonical_owned_path(messages.archive->as_path(),
                                                  request.target_directory.as_path(),
                                                  "Cargo staticlib artifact"_str,
                                                  true));
    auto contents = rstd::fs::read(archive.as_path());
    if (contents.is_err()) {
        return cargo_io_failure<StaticLibrarySnapshot>("read Cargo staticlib artifact"_str,
                                                       archive.as_path(),
                                                       rstd::move(contents).unwrap_err());
    }
    auto digest        = lito::crypto::sha256_hex(contents->as_slice());
    auto identity_text = String::make("lito-cargo-staticlib-v1\n"_str);
    identity_text.push_str(request.request_identity.as_str());
    identity_text.push_ascii(u8('\n'));
    identity_text.push_str(metadata.id.as_str());
    identity_text.push_ascii(u8('\n'));
    identity_text.push_str(metadata.version.as_str());
    identity_text.push_ascii(u8('\n'));
    identity_text.push_str(metadata.library->name.as_str());
    identity_text.push_ascii(u8('\n'));
    identity_text.push_str(digest.as_str());
    identity_text.push_ascii(u8('\n'));
    for (const auto& argument : messages.native_arguments) {
        identity_text.push_str(rstd::format("{}:{}\n", argument.len(), argument).as_str());
    }
    auto identity = lito::crypto::sha256_hex(identity_text.as_str());
    emit_cargo(observer,
               messages.artifact_fresh ? EventKind::Reuse : EventKind::Build,
               request.alias.as_str(),
               archive.as_path(),
               output.elapsed,
               true);
    return Ok(StaticLibrarySnapshot {
        .package               = metadata.clone(),
        .archive               = rstd::move(archive),
        .archive_digest        = rstd::move(digest),
        .archive_size          = u64(contents->len().to_primitive()),
        .native_link_arguments = rstd::move(messages.native_arguments),
        .identity              = rstd::move(identity),
        .fresh                 = messages.artifact_fresh,
        .elapsed               = output.elapsed,
    });
}

struct ParsedBinaryArtifact {
    String  name;
    PathBuf executable;
    bool    fresh { false };
};

struct ParsedBinaryMessages {
    Vec<ParsedBinaryArtifact> artifacts;
    bool                      found_finished { false };
};

auto binary_target(const PackageMetadata& metadata, ref<str> name) -> bool {
    for (const auto& target : metadata.binaries) {
        if (target.name == name) return true;
    }
    return false;
}

auto parse_binary_message(const Json&            message,
                          const PackageMetadata& metadata,
                          ParsedBinaryMessages&  result) -> lito::tools::ToolResult<empty> {
    auto reason = rstd_try(required_string(message, "reason"_str, "Cargo build message"_str));
    if (reason == "compiler-artifact"_str) {
        auto package =
            rstd_try(required_string(message, "package_id"_str, "Cargo compiler-artifact"_str));
        if (package != metadata.id.as_str()) return Ok(empty {});
        auto target =
            rstd_try(required_member(message, "target"_str, "Cargo compiler-artifact"_str));
        auto kinds = rstd_try(required_array(*target, "kind"_str, "Cargo artifact target"_str));
        if (! rstd_try(
                string_array_contains(*kinds, "bin"_str, "Cargo artifact target.kind"_str))) {
            return Ok(empty {});
        }
        auto name = rstd_try(required_string(*target, "name"_str, "Cargo artifact target"_str));
        if (! binary_target(metadata, name)) return Ok(empty {});
        for (const auto& artifact : result.artifacts) {
            if (artifact.name == name) {
                return cargo_failure<empty>(
                    rstd::format("Cargo emitted duplicate binary artifact '{}'", name));
            }
        }
        auto executable =
            rstd_try(required_string(message, "executable"_str, "Cargo compiler-artifact"_str));
        result.artifacts.push(ParsedBinaryArtifact {
            .name       = String::make(name),
            .executable = PathBuf::from(executable),
            .fresh = rstd_try(required_bool(message, "fresh"_str, "Cargo compiler-artifact"_str)),
        });
        return Ok(empty {});
    }
    if (reason == "build-finished"_str) {
        if (result.found_finished) {
            return cargo_failure<empty>("Cargo emitted duplicate build-finished messages"_str);
        }
        if (! rstd_try(required_bool(message, "success"_str, "Cargo build-finished"_str))) {
            return cargo_failure<empty>("Cargo build-finished reported failure"_str);
        }
        result.found_finished = true;
    }
    return Ok(empty {});
}

auto parse_binary_messages(ref<str> output, const PackageMetadata& metadata)
    -> lito::tools::ToolResult<ParsedBinaryMessages> {
    auto result    = ParsedBinaryMessages {};
    auto remaining = output;
    while (! remaining.is_empty()) {
        auto split = remaining.split_once("\n"_str);
        auto line  = (split.is_some() ? split->template get<0>() : remaining).trim_ascii();
        if (! line.is_empty() && line.starts_with("{"_str)) {
            auto parsed = rstd::json::from_str(line);
            if (parsed.is_err()) {
                return cargo_failure<ParsedBinaryMessages>(rstd::format(
                    "cannot parse Cargo build JSON message: {}", rstd::move(parsed).unwrap_err()));
            }
            auto message = rstd::move(parsed).unwrap();
            if (message.get("reason"_str).is_some()) {
                rstd_try(parse_binary_message(message, metadata, result));
            }
        }
        if (split.is_none()) break;
        remaining = split->template get<1>();
    }
    if (result.artifacts.is_empty()) {
        return cargo_failure<ParsedBinaryMessages>(
            rstd::format("Cargo package '{}' emitted no binary artifacts", metadata.name));
    }
    if (! result.found_finished) {
        return cargo_failure<ParsedBinaryMessages>("Cargo emitted no build-finished message"_str);
    }
    rstd::slice_::sort_unstable_by(
        result.artifacts.as_mut_slice().as_mut_ref(),
        [](const ParsedBinaryArtifact& left, const ParsedBinaryArtifact& right) {
            return left.name < right.name;
        });
    return Ok(rstd::move(result));
}

auto build_binaries(const Provider&                   provider,
                    const PackageMetadata&            metadata,
                    const BuildRequest&               request,
                    const ResolvedProcessEnvironment& environment,
                    const Option<EventSink>&          observer = None())
    -> lito::tools::ToolResult<BinarySnapshot> {
    if (request.jobs == usize {}) {
        return cargo_failure<BinarySnapshot>("Cargo build jobs must be greater than zero"_str);
    }
    if (metadata.binaries.is_empty()) {
        return cargo_failure<BinarySnapshot>(
            rstd::format("Cargo package '{}' contains no binary targets", metadata.name));
    }
    auto created = rstd::fs::create_dir_all(request.work_root.as_path());
    if (created.is_err()) {
        return cargo_io_failure<BinarySnapshot>("create Cargo work directory"_str,
                                                request.work_root.as_path(),
                                                rstd::move(created).unwrap_err());
    }
    auto lock_path = request.work_root.join(PathBuf::from("lock"_str).as_path());
    auto lock_file = rstd::fs::File::create(lock_path.as_path());
    if (lock_file.is_err()) {
        return cargo_io_failure<BinarySnapshot>("open Cargo dependency lock"_str,
                                                lock_path.as_path(),
                                                rstd::move(lock_file).unwrap_err());
    }
    auto locked = rstd::fs::FileLock::acquire(rstd::move(lock_file).unwrap(),
                                              rstd::fs::FileLockMode::Exclusive);
    if (locked.is_err()) {
        return cargo_io_failure<BinarySnapshot>(
            "lock Cargo dependency"_str, lock_path.as_path(), rstd::move(locked).unwrap_err());
    }
    auto arguments = Vec<String>::make();
    arguments.push(
        rstd_try(cargo_path_text(provider.executable.as_path(), "Cargo executable"_str)));
    rstd_try(append_cargo_profile_arguments(arguments, request.profile));
    arguments.push(String::make("build"_str));
    arguments.push(String::make("--manifest-path"_str));
    arguments.push(rstd_try(cargo_path_text(request.manifest.as_path(), "Cargo manifest"_str)));
    arguments.push(String::make("--package"_str));
    arguments.push(request.package.clone());
    arguments.push(String::make("--bins"_str));
    arguments.push(String::make("--profile"_str));
    arguments.push(request.profile.selected.value.clone());
    arguments.push(String::make("--target"_str));
    arguments.push(request.target.clone());
    arguments.push(String::make("--target-dir"_str));
    arguments.push(rstd_try(
        cargo_path_text(request.target_directory.as_path(), "Cargo target directory"_str)));
    arguments.push(String::make("--jobs"_str));
    arguments.push(rstd::format("{}", request.jobs));
    arguments.push(String::make("--message-format"_str));
    arguments.push(String::make("json-render-diagnostics"_str));
    arguments.push(String::make("--locked"_str));
    if (request.offline) arguments.push(String::make("--offline"_str));
    if (! request.features.is_empty()) {
        auto features = String::make();
        for (usize index {}; index < request.features.len(); ++index) {
            if (index != usize {}) features.push_ascii(u8(','));
            features.push_str(request.features[index].as_str());
        }
        arguments.push(String::make("--features"_str));
        arguments.push(rstd::move(features));
    }
    if (! request.default_features) {
        arguments.push(String::make("--no-default-features"_str));
    }
    emit_cargo(observer, EventKind::Build, request.alias.as_str(), request.work_root.as_path());
    auto output = rstd_try(invoke_cargo(arguments,
                                        environment,
                                        Some(metadata.workspace_root.as_path()),
                                        true,
                                        rstd::addressof(request.profile)));
    if (output.exit_code != i32 {}) {
        return Err(lito::tools::ToolError::Execution(
            rstd::format("Cargo dependency '{}' build", request.alias),
            output.exit_code,
            rstd::move(output.standard_output),
            rstd::move(output.standard_error)));
    }
    auto parsed    = rstd_try(parse_binary_messages(output.standard_output.as_str(), metadata));
    auto artifacts = Vec<BinaryArtifactSnapshot>::with_capacity(parsed.artifacts.len());
    for (auto& artifact : parsed.artifacts) {
        auto executable = rstd_try(canonical_owned_path(artifact.executable.as_path(),
                                                        request.target_directory.as_path(),
                                                        "Cargo binary artifact"_str,
                                                        true));
        auto contents   = rstd::fs::read(executable.as_path());
        if (contents.is_err()) {
            return cargo_io_failure<BinarySnapshot>("read Cargo binary artifact"_str,
                                                    executable.as_path(),
                                                    rstd::move(contents).unwrap_err());
        }
        auto digest        = lito::crypto::sha256_hex(contents->as_slice());
        auto identity_text = String::make("lito-cargo-bin-v1\n"_str);
        identity_text.push_str(request.request_identity.as_str());
        identity_text.push_ascii(u8('\n'));
        identity_text.push_str(metadata.id.as_str());
        identity_text.push_ascii(u8('\n'));
        identity_text.push_str(metadata.version.as_str());
        identity_text.push_ascii(u8('\n'));
        identity_text.push_str(artifact.name.as_str());
        identity_text.push_ascii(u8('\n'));
        identity_text.push_str(digest.as_str());
        identity_text.push_ascii(u8('\n'));
        auto identity = lito::crypto::sha256_hex(identity_text.as_str());
        emit_cargo(observer,
                   artifact.fresh ? EventKind::Reuse : EventKind::Build,
                   request.alias.as_str(),
                   executable.as_path(),
                   output.elapsed,
                   true);
        artifacts.push(BinaryArtifactSnapshot {
            .name              = rstd::move(artifact.name),
            .executable        = rstd::move(executable),
            .executable_digest = rstd::move(digest),
            .executable_size   = u64(contents->len().to_primitive()),
            .identity          = rstd::move(identity),
            .fresh             = artifact.fresh,
        });
    }
    return Ok(BinarySnapshot {
        .package   = metadata.clone(),
        .artifacts = rstd::move(artifacts),
        .elapsed   = output.elapsed,
    });
}

} // namespace lito::tools::cargo
