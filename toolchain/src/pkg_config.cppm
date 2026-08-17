export module lito.toolchain.pkg_config;

import rstd;
import lito.core;
import lito.cpp;
import lito.system;

using namespace rstd::prelude;
using namespace lito::system;
using namespace rstd::literals;

export namespace lito
{

template<typename T>
auto toolchain_dependency_failure(String message) -> lito::dependency::DependencyResult<T> {
    return Err(lito::dependency::DependencyError::Message(rstd::move(message)));
}

template<typename T>
auto toolchain_dependency_failure(ref<str> message) -> lito::dependency::DependencyResult<T> {
    return Err(lito::dependency::DependencyError::Message(String::make(message)));
}

auto version_operator(lito::dependency::PkgConfigVersionOperator value) noexcept -> ref<str> {
    switch (value) {
    case lito::dependency::PkgConfigVersionOperator::Equal: return "="_str;
    case lito::dependency::PkgConfigVersionOperator::Less: return "<"_str;
    case lito::dependency::PkgConfigVersionOperator::Greater: return ">"_str;
    case lito::dependency::PkgConfigVersionOperator::LessEqual: return "<="_str;
    case lito::dependency::PkgConfigVersionOperator::GreaterEqual: return ">="_str;
    }
    return "="_str;
}

auto module_spec(const lito::dependency::PkgConfigDependencyRequirement& requirement) -> String {
    auto result = requirement.module.clone();
    if (requirement.version.is_some()) {
        result.push_ascii(u8(' '));
        result.push_str(version_operator(requirement.version->comparison));
        result.push_ascii(u8(' '));
        result.push_str(requirement.version->value.as_str());
    }
    return result;
}

auto path_list(const Vec<PathBuf>& paths, const TargetInfo& target)
    -> lito::dependency::DependencyResult<String> {
    auto result    = String::make();
    auto separator = target.family == TargetFamily::Windows ? u8(';') : u8(':');
    for (const auto& path : paths) {
        auto text = path.as_path().to_str();
        if (text.is_none()) {
            return toolchain_dependency_failure<String>(
                rstd::format("pkg-config path '{}' is not valid UTF-8", path.as_path()));
        }
        if (! result.is_empty()) result.push_ascii(separator);
        result.push_str(*text);
    }
    return Ok(rstd::move(result));
}

auto provider_environment(const lito::dependency::PkgConfigProviderConfig& config,
                          const TargetInfo&                                target)
    -> lito::dependency::DependencyResult<CommandEnvironment> {
    auto result = CommandEnvironment {};
    if (! config.search_paths.is_empty()) {
        auto value = path_list(config.search_paths, target);
        if (value.is_err()) return Err(rstd::move(value).unwrap_err());
        result.entries.push(CommandEnvironmentEntry {
            .key   = String::make("PKG_CONFIG_PATH"_str),
            .value = Some(rstd::ffi::OsString::from(rstd::move(value).unwrap())),
        });
    }
    if (! config.library_paths.is_empty()) {
        auto value = path_list(config.library_paths, target);
        if (value.is_err()) return Err(rstd::move(value).unwrap_err());
        result.entries.push(CommandEnvironmentEntry {
            .key   = String::make("PKG_CONFIG_LIBDIR"_str),
            .value = Some(rstd::ffi::OsString::from(rstd::move(value).unwrap())),
        });
    }
    if (config.sysroot.is_some()) {
        auto text = config.sysroot->as_path().to_str();
        if (text.is_none()) {
            return toolchain_dependency_failure<CommandEnvironment>(rstd::format(
                "pkg-config sysroot '{}' is not valid UTF-8", config.sysroot->as_path()));
        }
        result.entries.push(CommandEnvironmentEntry {
            .key   = String::make("PKG_CONFIG_SYSROOT_DIR"_str),
            .value = Some(rstd::ffi::OsString::from(*text)),
        });
    }
    return Ok(rstd::move(result));
}

struct PkgConfigSnapshot {
    String                    module;
    String                    version;
    cpp::CppArgumentLayer     compile_arguments;
    cpp::LinkArgumentSequence link_arguments;
    String                    identity;

    auto clone() const -> PkgConfigSnapshot {
        return PkgConfigSnapshot {
            .module            = module.clone(),
            .version           = version.clone(),
            .compile_arguments = as<Clone>(compile_arguments).clone(),
            .link_arguments    = link_arguments.clone(),
            .identity          = identity.clone(),
        };
    }
};

auto query_pkg_config(const lito::dependency::PkgConfigProviderConfig&        config,
                      const lito::dependency::PkgConfigDependencyRequirement& requirement,
                      ref<str>                                                alias,
                      ref<str>                                                query,
                      const CommandEnvironment&                               overrides,
                      const ResolvedProcessEnvironment&                       environment)
    -> lito::dependency::DependencyResult<String> {
    auto executable = config.executable.as_path().to_str();
    if (executable.is_none()) {
        return toolchain_dependency_failure<String>(rstd::format(
            "pkg-config executable '{}' is not valid UTF-8", config.executable.as_path()));
    }
    auto arguments = Vec<String>::make();
    arguments.push(String::make(*executable));
    if (requirement.mode == lito::dependency::PkgConfigQueryMode::Static) {
        arguments.push(String::make("--static"_str));
    }
    arguments.push(String::make("--print-errors"_str));
    arguments.push(rstd::format("--{}", query));
    arguments.push(module_spec(requirement));
    auto output =
        run_command(arguments,
                    environment,
                    None(),
                    Some(ref<CommandEnvironment>::from_raw_parts(rstd::addressof(overrides))));
    if (output.is_err()) {
        return Err(lito::dependency::DependencyError::Operation(
            rstd::format("pkg-config dependency '{}' module '{}' {} query",
                         alias,
                         requirement.module.as_str(),
                         query),
            rstd::move(output).unwrap_err()));
    }
    auto value = rstd::move(output).unwrap();
    if (value.exit_code != i32 {}) {
        return toolchain_dependency_failure<String>(rstd::format(
            "pkg-config dependency '{}' module '{}' {} query failed with exit code {}: {}",
            alias,
            requirement.module.as_str(),
            query,
            value.exit_code,
            value.standard_error.as_str()));
    }
    return Ok(rstd::move(value.standard_output));
}

auto provider_version(const lito::dependency::PkgConfigProviderConfig&     config,
                      const CommandEnvironment&                            overrides,
                      const ResolvedProcessEnvironment&                    environment,
                      const lito::dependency::PkgConfigExternalDependency& declaration)
    -> lito::dependency::DependencyResult<String> {
    const auto& requirement = declaration.requirement;
    auto        executable  = config.executable.as_path().to_str();
    if (executable.is_none()) {
        return toolchain_dependency_failure<String>(rstd::format(
            "pkg-config dependency '{}' module '{}' has a provider path that is not valid UTF-8",
            declaration.alias.as_str(),
            requirement.module.as_str()));
    }
    auto arguments = Vec<String>::make();
    arguments.push(String::make(*executable));
    arguments.push(String::make("--version"_str));
    auto output =
        run_command(arguments,
                    environment,
                    None(),
                    Some(ref<CommandEnvironment>::from_raw_parts(rstd::addressof(overrides))));
    if (output.is_err()) {
        return Err(lito::dependency::DependencyError::Operation(
            rstd::format("pkg-config dependency '{}' module '{}' provider '{}'",
                         declaration.alias.as_str(),
                         requirement.module.as_str(),
                         *executable),
            rstd::move(output).unwrap_err()));
    }
    auto value = rstd::move(output).unwrap();
    if (value.exit_code != i32 {}) {
        return toolchain_dependency_failure<String>(rstd::format(
            "pkg-config dependency '{}' module '{}' provider '{}' failed with exit code {}: {}",
            declaration.alias.as_str(),
            requirement.module.as_str(),
            *executable,
            value.exit_code,
            value.standard_error.as_str()));
    }
    auto version = String::make(value.standard_output.as_str().trim_ascii());
    if (version.is_empty()) {
        return toolchain_dependency_failure<String>(rstd::format(
            "pkg-config dependency '{}' module '{}' provider returned an empty version",
            declaration.alias.as_str(),
            requirement.module.as_str()));
    }
    return Ok(rstd::move(version));
}

auto append_identity_value(String& output, ref<str> value) -> void {
    output.push_str(rstd::format("{}:{}\n", value.len(), value).as_str());
}

auto provider_identity(const lito::dependency::PkgConfigProviderConfig& config,
                       ref<str>                                         effective_target,
                       ref<str> version) -> lito::dependency::DependencyResult<String> {
    auto executable = config.executable.as_path().to_str();
    if (executable.is_none()) {
        return toolchain_dependency_failure<String>(
            "pkg-config executable path is not valid UTF-8"_str);
    }
    auto result = String::make("lito-pkg-config-provider-v1\n"_str);
    append_identity_value(result, *executable);
    append_identity_value(result, version);
    append_identity_value(result, effective_target);
    for (const auto& path : config.search_paths) {
        auto text = path.as_path().to_str();
        if (text.is_none())
            return toolchain_dependency_failure<String>("pkg-config path is not UTF-8"_str);
        append_identity_value(result, *text);
    }
    for (const auto& path : config.library_paths) {
        auto text = path.as_path().to_str();
        if (text.is_none())
            return toolchain_dependency_failure<String>("pkg-config path is not UTF-8"_str);
        append_identity_value(result, *text);
    }
    if (config.sysroot.is_some()) {
        auto text = config.sysroot->as_path().to_str();
        if (text.is_none())
            return toolchain_dependency_failure<String>("pkg-config sysroot is not UTF-8"_str);
        append_identity_value(result, *text);
    }
    return Ok(rstd::move(result));
}

auto snapshot_identity(ref<str>                                                provider,
                       const lito::dependency::PkgConfigDependencyRequirement& requirement,
                       ref<str>                                                version,
                       const Vec<String>&                                      cflags,
                       const Vec<String>&                                      libs) -> String {
    auto result = String::make("lito-external-dependency-v1\n"_str);
    append_identity_value(result, provider);
    append_identity_value(result, module_spec(requirement).as_str());
    append_identity_value(result, version);
    append_identity_value(result,
                          requirement.mode == lito::dependency::PkgConfigQueryMode::Static
                              ? "static"_str
                              : "shared"_str);
    for (const auto& value : cflags) append_identity_value(result, value.as_str());
    for (const auto& value : libs) append_identity_value(result, value.as_str());
    return result;
}

} // namespace lito
