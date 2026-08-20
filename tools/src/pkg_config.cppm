module;
#include <rstd/macro.hpp>

export module lito.tools:pkg_config;

import rstd;
import lito.system;
import :error;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito::system;
using PathBuf = rstd::path::PathBuf;

export namespace lito::tools::pkg_config
{

enum class VersionOperator
{
    Equal,
    Less,
    Greater,
    LessEqual,
    GreaterEqual,
};

enum class QueryMode
{
    Shared,
    Static,
};

struct VersionRequirement {
    VersionOperator comparison { VersionOperator::Equal };
    String          value;
};

struct Request {
    String                     alias;
    String                     module;
    Option<VersionRequirement> version;
    QueryMode                  mode { QueryMode::Shared };
};

struct Provider {
    PathBuf         executable;
    Vec<PathBuf>    search_paths;
    Vec<PathBuf>    library_paths;
    Option<PathBuf> sysroot;
    u8              path_separator { u8(':') };
    String          effective_target;
};

struct Snapshot {
    String      module;
    String      version;
    Vec<String> compile_fragments;
    Vec<String> link_fragments;
    String      identity;

    auto clone() const -> Snapshot {
        return Snapshot {
            .module            = module.clone(),
            .version           = version.clone(),
            .compile_fragments = as<Clone>(compile_fragments).clone(),
            .link_fragments    = as<Clone>(link_fragments).clone(),
            .identity          = identity.clone(),
        };
    }
};

auto module_spec(const Request& request) -> String;
auto query(const Provider&                   provider,
           const Request&                    request,
           const ResolvedProcessEnvironment& environment) -> ToolResult<Snapshot>;
auto tokenize_fragments(ref<str> input) -> ToolResult<Vec<String>>;

} // namespace lito::tools::pkg_config

namespace lito::tools::pkg_config
{

template<typename T>
auto failure(String message) -> ToolResult<T> {
    return Err(ToolError::Message(rstd::move(message)));
}

template<typename T>
auto failure(ref<str> message) -> ToolResult<T> {
    return failure<T>(String::make(message));
}

auto version_operator(VersionOperator value) noexcept -> ref<str> {
    switch (value) {
    case VersionOperator::Equal: return "="_str;
    case VersionOperator::Less: return "<"_str;
    case VersionOperator::Greater: return ">"_str;
    case VersionOperator::LessEqual: return "<="_str;
    case VersionOperator::GreaterEqual: return ">="_str;
    }
    return "="_str;
}

auto module_spec(const Request& request) -> String {
    auto result = request.module.clone();
    if (request.version.is_some()) {
        result.push_ascii(u8(' '));
        result.push_str(version_operator(request.version->comparison));
        result.push_ascii(u8(' '));
        result.push_str(request.version->value.as_str());
    }
    return result;
}

auto path_list(const Vec<PathBuf>& paths, u8 separator) -> ToolResult<String> {
    auto result = String::make();
    for (const auto& path : paths) {
        auto text = path.as_path().to_str();
        if (text.is_none()) {
            return failure<String>(
                rstd::format("pkg-config path '{}' is not valid UTF-8", path.as_path()));
        }
        if (! result.is_empty()) result.push_ascii(separator);
        result.push_str(*text);
    }
    return Ok(rstd::move(result));
}

auto provider_environment(const Provider& provider) -> ToolResult<CommandEnvironment> {
    auto result = CommandEnvironment {};
    if (! provider.search_paths.is_empty()) {
        auto value = rstd_try(path_list(provider.search_paths, provider.path_separator));
        result.entries.push(CommandEnvironmentEntry {
            .key   = String::make("PKG_CONFIG_PATH"_str),
            .value = Some(rstd::ffi::OsString::from(rstd::move(value))),
        });
    }
    if (! provider.library_paths.is_empty()) {
        auto value = rstd_try(path_list(provider.library_paths, provider.path_separator));
        result.entries.push(CommandEnvironmentEntry {
            .key   = String::make("PKG_CONFIG_LIBDIR"_str),
            .value = Some(rstd::ffi::OsString::from(rstd::move(value))),
        });
    }
    if (provider.sysroot.is_some()) {
        auto text = provider.sysroot->as_path().to_str();
        if (text.is_none()) {
            return failure<CommandEnvironment>(rstd::format(
                "pkg-config sysroot '{}' is not valid UTF-8", provider.sysroot->as_path()));
        }
        result.entries.push(CommandEnvironmentEntry {
            .key   = String::make("PKG_CONFIG_SYSROOT_DIR"_str),
            .value = Some(rstd::ffi::OsString::from(*text)),
        });
    }
    return Ok(rstd::move(result));
}

auto run_query(const Provider&                   provider,
               const Request&                    request,
               ref<str>                          query_name,
               const CommandEnvironment&         overrides,
               const ResolvedProcessEnvironment& environment) -> ToolResult<String> {
    auto executable = provider.executable.as_path().to_str();
    if (executable.is_none()) {
        return failure<String>(rstd::format("pkg-config executable '{}' is not valid UTF-8",
                                            provider.executable.as_path()));
    }
    auto arguments = Vec<String>::make();
    arguments.push(String::make(*executable));
    if (request.mode == QueryMode::Static) arguments.push(String::make("--static"_str));
    arguments.push(String::make("--print-errors"_str));
    arguments.push(rstd::format("--{}", query_name));
    arguments.push(module_spec(request));
    auto output =
        run_command(arguments,
                    environment,
                    None(),
                    Some(ref<CommandEnvironment>::from_raw_parts(rstd::addressof(overrides))));
    if (output.is_err()) return Err(rstd::into<ToolError>(rstd::move(output).unwrap_err()));
    auto value = rstd::move(output).unwrap();
    if (value.exit_code != i32 {}) {
        return Err(
            ToolError::Execution(rstd::format("pkg-config dependency '{}' module '{}' {} query",
                                              request.alias.as_str(),
                                              request.module.as_str(),
                                              query_name),
                                 value.exit_code,
                                 rstd::move(value.standard_output),
                                 rstd::move(value.standard_error)));
    }
    return Ok(rstd::move(value.standard_output));
}

auto provider_version(const Provider&                   provider,
                      const Request&                    request,
                      const CommandEnvironment&         overrides,
                      const ResolvedProcessEnvironment& environment) -> ToolResult<String> {
    auto executable = provider.executable.as_path().to_str();
    if (executable.is_none()) {
        return failure<String>(rstd::format("pkg-config provider '{}' is not valid UTF-8",
                                            provider.executable.as_path()));
    }
    auto arguments = Vec<String>::make();
    arguments.push(String::make(*executable));
    arguments.push(String::make("--version"_str));
    auto output =
        run_command(arguments,
                    environment,
                    None(),
                    Some(ref<CommandEnvironment>::from_raw_parts(rstd::addressof(overrides))));
    if (output.is_err()) return Err(rstd::into<ToolError>(rstd::move(output).unwrap_err()));
    auto value = rstd::move(output).unwrap();
    if (value.exit_code != i32 {}) {
        return Err(
            ToolError::Execution(rstd::format("pkg-config dependency '{}' module '{}' provider",
                                              request.alias.as_str(),
                                              request.module.as_str()),
                                 value.exit_code,
                                 rstd::move(value.standard_output),
                                 rstd::move(value.standard_error)));
    }
    auto version = String::make(value.standard_output.as_str().trim_ascii());
    if (version.is_empty()) return failure<String>("pkg-config returned an empty version"_str);
    return Ok(rstd::move(version));
}

auto append_identity_value(String& output, ref<str> value) -> void {
    output.push_str(rstd::format("{}:{}\n", value.len(), value).as_str());
}

auto provider_identity(const Provider& provider, ref<str> version) -> ToolResult<String> {
    auto executable = provider.executable.as_path().to_str();
    if (executable.is_none()) {
        return failure<String>("pkg-config executable path is not valid UTF-8"_str);
    }
    auto result = String::make("lito-pkg-config-provider-v1\n"_str);
    append_identity_value(result, *executable);
    append_identity_value(result, version);
    append_identity_value(result, provider.effective_target.as_str());
    for (const auto& path : provider.search_paths) {
        auto text = path.as_path().to_str();
        if (text.is_none()) return failure<String>("pkg-config path is not UTF-8"_str);
        append_identity_value(result, *text);
    }
    for (const auto& path : provider.library_paths) {
        auto text = path.as_path().to_str();
        if (text.is_none()) return failure<String>("pkg-config path is not UTF-8"_str);
        append_identity_value(result, *text);
    }
    if (provider.sysroot.is_some()) {
        auto text = provider.sysroot->as_path().to_str();
        if (text.is_none()) return failure<String>("pkg-config sysroot is not UTF-8"_str);
        append_identity_value(result, *text);
    }
    return Ok(rstd::move(result));
}

auto snapshot_identity(ref<str>           provider,
                       const Request&     request,
                       ref<str>           version,
                       const Vec<String>& cflags,
                       const Vec<String>& libs) -> String {
    auto result = String::make("lito-external-dependency-v1\n"_str);
    append_identity_value(result, provider);
    append_identity_value(result, module_spec(request).as_str());
    append_identity_value(result, version);
    append_identity_value(result, request.mode == QueryMode::Static ? "static"_str : "shared"_str);
    for (const auto& value : cflags) append_identity_value(result, value.as_str());
    for (const auto& value : libs) append_identity_value(result, value.as_str());
    return result;
}

auto tokenize_fragments(ref<str> input) -> ToolResult<Vec<String>> {
    auto tokens = tokenize_command_fragments(input, "pkg-config output"_str);
    if (tokens.is_err()) {
        return Err(ToolError::Message(rstd::format("{}", rstd::move(tokens).unwrap_err())));
    }
    return Ok(rstd::move(tokens).unwrap());
}

auto query(const Provider&                   provider,
           const Request&                    request,
           const ResolvedProcessEnvironment& environment) -> ToolResult<Snapshot> {
    auto overrides      = rstd_try(provider_environment(provider));
    auto provider_value = rstd_try(provider_version(provider, request, overrides, environment));
    auto provider_id    = rstd_try(provider_identity(provider, provider_value.as_str()));
    auto module_version =
        rstd_try(run_query(provider, request, "modversion"_str, overrides, environment));
    auto version = String::make(module_version.as_str().trim_ascii());
    if (version.is_empty())
        return failure<Snapshot>("pkg-config returned an empty module version"_str);
    auto compile_output =
        rstd_try(run_query(provider, request, "cflags"_str, overrides, environment));
    auto link_output = rstd_try(run_query(provider, request, "libs"_str, overrides, environment));
    auto compile     = rstd_try(tokenize_fragments(compile_output.as_str()));
    auto link        = rstd_try(tokenize_fragments(link_output.as_str()));
    auto identity =
        snapshot_identity(provider_id.as_str(), request, version.as_str(), compile, link);
    return Ok(Snapshot {
        .module            = request.module.clone(),
        .version           = rstd::move(version),
        .compile_fragments = rstd::move(compile),
        .link_fragments    = rstd::move(link),
        .identity          = rstd::move(identity),
    });
}

} // namespace lito::tools::pkg_config
