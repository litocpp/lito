export module lito.dependency:pkg_config_support;

import rstd;
import lito.dependency.contract;
import lito.error;
import lito.cpp;
import lito.build.configuration;
import lito.build.profile_contract;
import lito.build.contract;
import lito.platform.contract;
import lito.lock.contract;
import lito.package.graph_contract;
import lito.system.process;
import lito.system.environment;
import lito.source;
import lito.dependency.cmake;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace lito
{

template<typename T>
auto dependency_failure(String message) -> Result<T> {
    return Err(Error::make(ErrorKind::Dependency, rstd::move(message)));
}

template<typename T>
auto dependency_failure(ref<str> message) -> Result<T> {
    return Err(Error::make(ErrorKind::Dependency, message));
}

auto version_operator(PkgConfigVersionOperator value) noexcept -> ref<str> {
    switch (value) {
    case PkgConfigVersionOperator::Equal: return "="_str;
    case PkgConfigVersionOperator::Less: return "<"_str;
    case PkgConfigVersionOperator::Greater: return ">"_str;
    case PkgConfigVersionOperator::LessEqual: return "<="_str;
    case PkgConfigVersionOperator::GreaterEqual: return ">="_str;
    }
    return "="_str;
}

auto module_spec(const PkgConfigDependencyRequirement& requirement) -> String {
    auto result = requirement.module.clone();
    if (requirement.version.is_some()) {
        result.push_ascii(u8(' '));
        result.push_str(version_operator(requirement.version->comparison));
        result.push_ascii(u8(' '));
        result.push_str(requirement.version->value.as_str());
    }
    return result;
}

auto path_list(const Vec<PathBuf>& paths, const TargetInfo& target) -> Result<String> {
    auto result    = String::make();
    auto separator = target.family == TargetFamily::Windows ? u8(';') : u8(':');
    for (const auto& path : paths) {
        auto text = path.as_path().to_str();
        if (text.is_none()) {
            return dependency_failure<String>(
                rstd::format("pkg-config path '{}' is not valid UTF-8", path.as_path()));
        }
        if (! result.is_empty()) result.push_ascii(separator);
        result.push_str(*text);
    }
    return Ok(rstd::move(result));
}

auto provider_environment(const PkgConfigProviderConfig& config, const TargetInfo& target)
    -> Result<CommandEnvironment> {
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
            return dependency_failure<CommandEnvironment>(rstd::format(
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
    String               module;
    String               version;
    CppArgumentLayer     compile_arguments;
    LinkArgumentSequence link_arguments;
    String               identity;

    auto clone() const -> PkgConfigSnapshot {
        return PkgConfigSnapshot {
            .module            = module.clone(),
            .version           = version.clone(),
            .compile_arguments = as<rstd::clone::Clone>(compile_arguments).clone(),
            .link_arguments    = link_arguments.clone(),
            .identity          = identity.clone(),
        };
    }
};

auto query_pkg_config(const PkgConfigProviderConfig&        config,
                      const PkgConfigDependencyRequirement& requirement,
                      ref<str>                              alias,
                      ref<str>                              query,
                      const CommandEnvironment&             overrides,
                      const ResolvedProcessEnvironment&     environment) -> Result<String> {
    auto executable = config.executable.as_path().to_str();
    if (executable.is_none()) {
        return dependency_failure<String>(rstd::format(
            "pkg-config executable '{}' is not valid UTF-8", config.executable.as_path()));
    }
    auto arguments = Vec<String>::make();
    arguments.push(String::make(*executable));
    if (requirement.mode == PkgConfigQueryMode::Static) {
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
        return dependency_failure<String>(
            rstd::format("pkg-config dependency '{}' module '{}' {} query could not execute: {}",
                         alias,
                         requirement.module.as_str(),
                         query,
                         rstd::move(output).unwrap_err().message.as_str()));
    }
    auto value = rstd::move(output).unwrap();
    if (value.exit_code != i32 {}) {
        return dependency_failure<String>(rstd::format(
            "pkg-config dependency '{}' module '{}' {} query failed with exit code {}: {}",
            alias,
            requirement.module.as_str(),
            query,
            value.exit_code,
            value.standard_error.as_str()));
    }
    return Ok(rstd::move(value.standard_output));
}

auto provider_version(const PkgConfigProviderConfig&     config,
                      const CommandEnvironment&          overrides,
                      const ResolvedProcessEnvironment&  environment,
                      const PkgConfigExternalDependency& declaration) -> Result<String> {
    const auto& requirement = declaration.requirement;
    auto        executable  = config.executable.as_path().to_str();
    if (executable.is_none()) {
        return dependency_failure<String>(rstd::format(
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
        return dependency_failure<String>(
            rstd::format("pkg-config dependency '{}' module '{}' cannot execute provider '{}': {}",
                         declaration.alias.as_str(),
                         requirement.module.as_str(),
                         *executable,
                         rstd::move(output).unwrap_err().message.as_str()));
    }
    auto value = rstd::move(output).unwrap();
    if (value.exit_code != i32 {}) {
        return dependency_failure<String>(rstd::format(
            "pkg-config dependency '{}' module '{}' provider '{}' failed with exit code {}: {}",
            declaration.alias.as_str(),
            requirement.module.as_str(),
            *executable,
            value.exit_code,
            value.standard_error.as_str()));
    }
    auto version = String::make(value.standard_output.as_str().trim_ascii());
    if (version.is_empty()) {
        return dependency_failure<String>(rstd::format(
            "pkg-config dependency '{}' module '{}' provider returned an empty version",
            declaration.alias.as_str(),
            requirement.module.as_str()));
    }
    return Ok(rstd::move(version));
}

auto append_identity_value(String& output, ref<str> value) -> void {
    output.push_str(rstd::format("{}:{}\n", value.len(), value).as_str());
}

auto provider_identity(const PkgConfigProviderConfig& config,
                       ref<str>                       effective_target,
                       ref<str>                       version) -> Result<String> {
    auto executable = config.executable.as_path().to_str();
    if (executable.is_none()) {
        return dependency_failure<String>("pkg-config executable path is not valid UTF-8"_str);
    }
    auto result = String::make("lito-pkg-config-provider-v1\n"_str);
    append_identity_value(result, *executable);
    append_identity_value(result, version);
    append_identity_value(result, effective_target);
    for (const auto& path : config.search_paths) {
        auto text = path.as_path().to_str();
        if (text.is_none()) return dependency_failure<String>("pkg-config path is not UTF-8"_str);
        append_identity_value(result, *text);
    }
    for (const auto& path : config.library_paths) {
        auto text = path.as_path().to_str();
        if (text.is_none()) return dependency_failure<String>("pkg-config path is not UTF-8"_str);
        append_identity_value(result, *text);
    }
    if (config.sysroot.is_some()) {
        auto text = config.sysroot->as_path().to_str();
        if (text.is_none())
            return dependency_failure<String>("pkg-config sysroot is not UTF-8"_str);
        append_identity_value(result, *text);
    }
    return Ok(rstd::move(result));
}

auto snapshot_identity(ref<str>                              provider,
                       const PkgConfigDependencyRequirement& requirement,
                       ref<str>                              version,
                       const Vec<String>&                    cflags,
                       const Vec<String>&                    libs) -> String {
    auto result = String::make("lito-external-dependency-v1\n"_str);
    append_identity_value(result, provider);
    append_identity_value(result, module_spec(requirement).as_str());
    append_identity_value(result, version);
    append_identity_value(
        result, requirement.mode == PkgConfigQueryMode::Static ? "static"_str : "shared"_str);
    for (const auto& value : cflags) append_identity_value(result, value.as_str());
    for (const auto& value : libs) append_identity_value(result, value.as_str());
    return result;
}

auto clone_cmake_declaration(const CMakeDependencyRequirement& declaration)
    -> CMakeDependencyRequirement {
    auto cache = Vec<CMakeCacheEntry>::with_capacity(declaration.cache.len());
    for (const auto& entry : declaration.cache) {
        cache.push(CMakeCacheEntry {
            .name  = entry.name.clone(),
            .value = entry.value.clone(),
        });
    }
    auto targets = Vec<CMakeTargetRequirement>::with_capacity(declaration.targets.len());
    for (const auto& target : declaration.targets) {
        targets.push(CMakeTargetRequirement {
            .name       = target.name.clone(),
            .visibility = target.visibility,
        });
    }
    auto result = CMakeDependencyRequirement {
        .alias       = declaration.alias.clone(),
        .package     = declaration.package.clone(),
        .source      = declaration.source.clone(),
        .integration = declaration.integration,
        .cache       = rstd::move(cache),
        .targets     = rstd::move(targets),
    };
    if (declaration.adapter.is_some()) result.adapter = Some(declaration.adapter->clone());
    if (declaration.config_directory.is_some()) {
        result.config_directory = Some(declaration.config_directory->clone());
    }
    if (declaration.declaration_root.is_some()) {
        result.declaration_root = Some(declaration.declaration_root->clone());
    }
    if (declaration.adapter_root.is_some()) {
        result.adapter_root = Some(declaration.adapter_root->clone());
    }
    return result;
}

} // namespace lito
