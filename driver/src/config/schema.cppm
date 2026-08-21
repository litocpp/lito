module;
#include <rstd/macro.hpp>

module lito.driver:config.schema;

import rstd;
import rstd.toml;
import lito.core;
import lito.tools;
import :config.project;
import lito.system;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;
using namespace lito::system;
using namespace lito::tools;
using namespace rstd::literals;
using Toml  = rstd::toml::Value;
using Table = rstd::toml::Table;
using lito::parse::NodePath;
namespace parse_toml = lito::parse::toml;
using namespace lito::config;

template<typename T>
auto config_failure(String message) -> ConfigResult<T> {
    return Err(ConfigError::Schema(rstd::move(message)));
}

template<typename T>
auto config_failure(ref<str> message) -> ConfigResult<T> {
    return Err(ConfigError::Schema(String::make(message)));
}

template<typename T>
auto config_io_failure(ref<str>               operation,
                       ref<rstd::path::Path>  path,
                       rstd::io::error::Error source) -> ConfigResult<T> {
    return Err(ConfigError::Io(String::make(operation), PathBuf::from(path), rstd::move(source)));
}

auto config_member(const Toml& value, ref<str> key) -> Option<ref<Toml>> {
    return value.get(key);
}

auto normalize_host_tool_provider_shorthand(Toml& document) -> void {
    auto tools = document.get_mut("tools"_str);
    if (tools.is_none()) return;
    auto table = (**tools).as_table_mut();
    if (table.is_none()) return;
    constexpr ref<str> providers[] = {
        "cmake"_str,
        "pkg-config"_str,
    };
    for (const auto provider : providers) {
        auto value = (**table).remove(provider);
        if (value.is_none()) continue;
        auto entry = rstd::move(value).unwrap();
        if (entry.as_str().is_some()) {
            auto normalized = Table::make();
            normalized.insert(String::make("executable"_str), rstd::move(entry));
            entry = Toml::Table(rstd::move(normalized));
        }
        (**table).insert(String::make(provider), rstd::move(entry));
    }
}

auto root_config_key(ref<str> key) -> bool {
    return key == "environment"_str || key == "tools"_str || key == "toolchain"_str ||
           key == "patch"_str || key == "lock"_str || key == "install"_str || key == "build"_str ||
           key == "doc"_str;
}

auto environment_config_key(ref<str> key) -> bool {
    return key == "append-path"_str;
}

auto toolchain_config_key(ref<str> key) -> bool {
    return key == "cc"_str || key == "cxx"_str || key == "ld"_str || key == "ar"_str ||
           key == "stdlib"_str || key == "stdlib-runtime"_str || key == "sdk"_str;
}

auto toolchain_sdk_config_key(ref<str> key) -> bool {
    return key == "kind"_str || key == "version"_str || key == "path"_str;
}

auto tools_config_key(ref<str> key) -> bool {
    return key == "cmake"_str || key == "tar"_str || key == "bsdtar"_str ||
           key == "clang-format"_str || key == "curl"_str || key == "git"_str ||
           key == "pkg-config"_str || key == "strip"_str;
}

auto build_config_key(ref<str> key) -> bool {
    return key == "options"_str || key == "linker-options"_str || key == "c"_str ||
           key == "target"_str;
}

auto build_target_config_key(ref<str> key) -> bool {
    return key == "kind"_str || key == "abi"_str || key == "min-api"_str;
}

auto c_build_config_key(ref<str> key) -> bool {
    return key == "options"_str;
}

auto patch_config_key(ref<str> key) -> bool {
    return key == "path"_str;
}

auto lock_config_key(ref<str> key) -> bool {
    return key == "path"_str;
}

auto install_config_key(ref<str> key) -> bool {
    return key == "root"_str;
}

auto doc_config_key(ref<str> key) -> bool {
    return key == "litodoc-path"_str;
}

auto pkg_config_key(ref<str> key) -> bool {
    return key == "executable"_str || key == "search-path"_str || key == "library-path"_str ||
           key == "sysroot"_str;
}

auto cmake_key(ref<str> key) -> bool {
    return key == "executable"_str || key == "generator"_str || key == "search-path"_str ||
           key == "overrides"_str;
}

auto cmake_override_key(ref<str> key) -> bool {
    return key == "source"_str;
}

auto configured_executable(const Toml& value, ref<str> context) -> ConfigResult<PathBuf> {
    auto text = value.as_str();
    if (text.is_none()) {
        return config_failure<PathBuf>(rstd::format("{} must be a string", context));
    }
    if (text->is_empty()) {
        return config_failure<PathBuf>(rstd::format("{} must not be empty", context));
    }
    auto path = PathBuf::from(*text);
    if (! path.as_path().is_absolute() && ! is_searchable_executable_name(path.as_path())) {
        return config_failure<PathBuf>(
            rstd::format("{} must be an executable name or absolute path", context));
    }
    return Ok(rstd::move(path));
}

auto configured_tool_override(const Toml& toolchain_value, ref<str> key, ref<str> context)
    -> ConfigResult<Option<PathBuf>> {
    auto value = config_member(toolchain_value, key);
    if (value.is_none()) return Ok(Option<PathBuf> {});
    auto field = rstd::format("{}.{}", context, key);
    return Ok(Some(rstd_try(configured_executable(**value, field.as_str()))));
}

auto configured_toolchain_sdk(const Toml& toolchain_value)
    -> ConfigResult<Option<ToolchainSdkSelection>> {
    auto value = config_member(toolchain_value, "sdk"_str);
    if (value.is_none()) return Ok(Option<ToolchainSdkSelection> {});
    auto path  = NodePath::root("config.toolchain.sdk"_str);
    auto table = rstd_try(parse_toml::table(**value, path));
    rstd_try(parse_toml::reject_unknown(*table, path, toolchain_sdk_config_key));
    auto kind_text = rstd_try(parse_toml::required_non_empty_string(**value, "kind"_str, path));
    auto kind      = parse_sdk_kind(kind_text.as_str());
    if (kind.is_none()) {
        return config_failure<Option<ToolchainSdkSelection>>(
            "config.toolchain.sdk.kind must be 'llvm' or 'android-ndk'"_str);
    }
    auto version  = config_member(**value, "version"_str);
    auto sdk_path = config_member(**value, "path"_str);
    if (version.is_some() == sdk_path.is_some()) {
        return config_failure<Option<ToolchainSdkSelection>>(
            "config.toolchain.sdk must contain exactly one of 'version' or 'path'"_str);
    }
    if (version.is_some()) {
        auto text = (**version).as_str();
        if (text.is_none() || text->is_empty()) {
            return config_failure<Option<ToolchainSdkSelection>>(
                "config.toolchain.sdk.version must be a non-empty string"_str);
        }
        return Ok(Some(ToolchainSdkSelection::Managed(*kind, String::make(*text))));
    }
    auto text = (**sdk_path).as_str();
    if (text.is_none() || text->is_empty()) {
        return config_failure<Option<ToolchainSdkSelection>>(
            "config.toolchain.sdk.path must be a non-empty absolute path"_str);
    }
    auto directory = PathBuf::from(*text);
    if (! directory.as_path().is_absolute()) {
        return config_failure<Option<ToolchainSdkSelection>>(
            "config.toolchain.sdk.path must be absolute"_str);
    }
    return Ok(Some(ToolchainSdkSelection::Directory(*kind, rstd::move(directory))));
}

auto configured_standard_library(const Toml& toolchain_value)
    -> ConfigResult<StandardLibrarySelection> {
    auto value = config_member(toolchain_value, "stdlib"_str);
    if (value.is_none()) return Ok(StandardLibrarySelection::Auto);
    auto text = (**value).as_str();
    if (text.is_none()) {
        return config_failure<StandardLibrarySelection>(
            "config.toolchain.stdlib must be a string"_str);
    }
    auto parsed = parse_standard_library_selection(*text);
    if (parsed.is_some()) return Ok(*parsed);
    return config_failure<StandardLibrarySelection>(
        "config.toolchain.stdlib must be 'auto', 'libc++', 'libstdc++', or 'msvc'"_str);
}

auto configured_standard_library_runtime(const Toml& toolchain_value)
    -> ConfigResult<StandardLibraryRuntime> {
    auto value = config_member(toolchain_value, "stdlib-runtime"_str);
    if (value.is_none()) return Ok(StandardLibraryRuntime::Dynamic);
    auto text = (**value).as_str();
    if (text.is_none()) {
        return config_failure<StandardLibraryRuntime>(
            "config.toolchain.stdlib-runtime must be a string"_str);
    }
    auto parsed = parse_standard_library_runtime(*text);
    if (parsed.is_some()) return Ok(*parsed);
    return config_failure<StandardLibraryRuntime>(
        "config.toolchain.stdlib-runtime must be 'dynamic' or 'static'"_str);
}

auto configured_build_target(const Toml& document) -> ConfigResult<BuildTargetRequest> {
    auto build = config_member(document, "build"_str);
    if (build.is_none()) return Ok(BuildTargetRequest::Default());
    auto target = config_member(**build, "target"_str);
    if (target.is_none()) return Ok(BuildTargetRequest::Default());
    auto path  = NodePath::root("config.build.target"_str);
    auto table = rstd_try(parse_toml::table(**target, path));
    rstd_try(parse_toml::reject_unknown(*table, path, build_target_config_key));
    auto kind = rstd_try(parse_toml::required_non_empty_string(**target, "kind"_str, path));
    if (kind.as_str() != "android"_str) {
        return config_failure<BuildTargetRequest>("config.build.target.kind must be 'android'"_str);
    }
    auto abi         = rstd_try(parse_toml::required_non_empty_string(**target, "abi"_str, path));
    auto minimum_api = config_member(**target, "min-api"_str);
    if (minimum_api.is_none()) {
        return config_failure<BuildTargetRequest>("config.build.target is missing 'min-api'"_str);
    }
    auto integer = (**minimum_api).as_integer();
    if (integer.is_none() || integer->to_primitive() <= 0 ||
        integer->to_primitive() > u32::MAX.to_primitive()) {
        return config_failure<BuildTargetRequest>(
            "config.build.target.min-api must be a positive 32-bit integer"_str);
    }
    return Ok(BuildTargetRequest::Android(AndroidTargetRequest {
        .abi         = rstd::move(abi),
        .minimum_api = u32(integer->to_primitive()),
    }));
}

auto configured_build_option_input(Option<ref<Toml>> value, ref<str> context)
    -> ConfigResult<Option<BuildOptionInput>> {
    if (value.is_none()) return Ok(Option<BuildOptionInput> {});
    auto array = (**value).as_array();
    if (array.is_none()) {
        return config_failure<Option<BuildOptionInput>>(
            rstd::format("{} must be an array", context));
    }
    auto arguments = Vec<String>::with_capacity((**array).len());
    for (const auto& item : **array) {
        auto text = item.as_str();
        if (text.is_none() || text->is_empty()) {
            return config_failure<Option<BuildOptionInput>>(
                rstd::format("{} entries must be non-empty strings", context));
        }
        arguments.push(String::make(*text));
    }
    if (arguments.is_empty()) return Ok(Option<BuildOptionInput> {});
    return Ok(Some(BuildOptionInput {
        .arguments = rstd::move(arguments),
        .source    = String::make(context),
    }));
}

auto configured_build_options(const Toml& document) -> ConfigResult<ProjectBuildOptions> {
    auto result = ProjectBuildOptions {};
    auto value  = config_member(document, "build"_str);
    if (value.is_none()) return Ok(rstd::move(result));
    auto table = rstd_try(parse_toml::table(**value, NodePath::root("config.build"_str)));
    rstd_try(
        parse_toml::reject_unknown(*table, NodePath::root("config.build"_str), build_config_key));
    auto cpp = rstd_try(configured_build_option_input(config_member(**value, "options"_str),
                                                      "config.build.options"_str));
    if (cpp.is_some()) result.cpp.push(rstd::move(cpp).unwrap());
    auto linker = rstd_try(configured_build_option_input(
        config_member(**value, "linker-options"_str), "config.build.linker-options"_str));
    if (linker.is_some()) result.linker.push(rstd::move(linker).unwrap());

    auto c = config_member(**value, "c"_str);
    if (c.is_none()) return Ok(rstd::move(result));
    auto c_table = rstd_try(parse_toml::table(**c, NodePath::root("config.build.c"_str)));
    rstd_try(parse_toml::reject_unknown(
        *c_table, NodePath::root("config.build.c"_str), c_build_config_key));
    auto c_options = rstd_try(configured_build_option_input(config_member(**c, "options"_str),
                                                            "config.build.c.options"_str));
    if (c_options.is_some()) result.c.push(rstd::move(c_options).unwrap());
    return Ok(rstd::move(result));
}

auto environment_build_option(ToolchainEnvironmentVariable variable)
    -> ConfigResult<Option<BuildOptionInput>> {
    auto name  = toolchain_environment_variable_name(variable);
    auto value = rstd::env::var_os(name);
    if (value.is_none()) return Ok(Option<BuildOptionInput> {});
    auto text = value->as_os_str().to_str();
    if (text.is_none()) {
        return Err(ConfigError::EnvironmentFlags(
            String::make(name),
            lito::system::SystemError::Environment(rstd::format("{} is not valid UTF-8", name))));
    }
    auto arguments = lito::system::tokenize_command_fragments(*text, name);
    if (arguments.is_err()) {
        return Err(
            ConfigError::EnvironmentFlags(String::make(name), rstd::move(arguments).unwrap_err()));
    }
    if (arguments->is_empty()) return Ok(Option<BuildOptionInput> {});
    return Ok(Some(BuildOptionInput {
        .arguments = rstd::move(arguments).unwrap(),
        .source    = String::make(name),
    }));
}

auto append_environment_build_options(ProjectBuildOptions& options, EnvironmentFlagPolicy policy)
    -> ConfigResult<empty> {
    if (policy == EnvironmentFlagPolicy::Ignore) return Ok(empty {});
    auto cpp = rstd_try(environment_build_option(ToolchainEnvironmentVariable::CxxFlags));
    if (cpp.is_some()) options.cpp.push(rstd::move(cpp).unwrap());
    auto c = rstd_try(environment_build_option(ToolchainEnvironmentVariable::CFlags));
    if (c.is_some()) options.c.push(rstd::move(c).unwrap());
    auto linker = rstd_try(environment_build_option(ToolchainEnvironmentVariable::LdFlags));
    if (linker.is_some()) options.linker.push(rstd::move(linker).unwrap());
    return Ok(empty {});
}

auto configured_directories(const Toml&           table,
                            ref<str>              key,
                            ref<str>              context,
                            ref<rstd::path::Path> project_root) -> ConfigResult<Vec<PathBuf>> {
    auto result = Vec<PathBuf>::make();
    auto value  = config_member(table, key);
    if (value.is_none()) return Ok(rstd::move(result));
    auto array = (**value).as_array();
    if (array.is_none()) {
        return config_failure<Vec<PathBuf>>(rstd::format("{}.{} must be an array", context, key));
    }
    for (const auto& item : **array) {
        auto text = item.as_str();
        if (text.is_none() || text->is_empty()) {
            return config_failure<Vec<PathBuf>>(
                rstd::format("{}.{} entries must be non-empty strings", context, key));
        }
        auto path = PathBuf::from(*text);
        if (path.as_path().is_relative()) path = PathBuf::from(project_root).join(path.as_path());
        auto canonical = rstd::fs::canonicalize(path.as_path());
        if (canonical.is_err()) {
            return config_io_failure<Vec<PathBuf>>(
                rstd::format("resolve {}.{} path", context, key).as_str(),
                path.as_path(),
                rstd::move(canonical).unwrap_err());
        }
        auto metadata = rstd::fs::metadata(canonical->as_path());
        if (metadata.is_err()) {
            return config_io_failure<Vec<PathBuf>>(
                rstd::format("inspect {}.{} path", context, key).as_str(),
                canonical->as_path(),
                rstd::move(metadata).unwrap_err());
        }
        if (! metadata->is_dir()) {
            return config_failure<Vec<PathBuf>>(rstd::format(
                "{}.{} path '{}' is not a directory", context, key, canonical->as_path()));
        }
        result.push(rstd::move(canonical).unwrap());
    }
    return Ok(rstd::move(result));
}

auto configured_pkg_config(const Toml& value, ref<rstd::path::Path> project_root)
    -> ConfigResult<lito::dependency::PkgConfigProviderConfig> {
    auto result = lito::dependency::PkgConfigProviderConfig {};
    auto table  = rstd_try(parse_toml::table(value, NodePath::root("config.tools.pkg-config"_str)));
    rstd_try(parse_toml::reject_unknown(
        *table, NodePath::root("config.tools.pkg-config"_str), pkg_config_key));
    result.search_paths  = rstd_try(configured_directories(
        value, "search-path"_str, "config.tools.pkg-config"_str, project_root));
    result.library_paths = rstd_try(configured_directories(
        value, "library-path"_str, "config.tools.pkg-config"_str, project_root));
    auto sysroot         = config_member(value, "sysroot"_str);
    if (sysroot.is_some()) {
        auto text = (**sysroot).as_str();
        if (text.is_none() || text->is_empty()) {
            return config_failure<lito::dependency::PkgConfigProviderConfig>(
                "config.tools.pkg-config.sysroot must be a non-empty string"_str);
        }
        auto path = PathBuf::from(*text);
        if (path.as_path().is_relative()) path = PathBuf::from(project_root).join(path.as_path());
        auto canonical = rstd::fs::canonicalize(path.as_path());
        if (canonical.is_err()) {
            return config_io_failure<lito::dependency::PkgConfigProviderConfig>(
                "resolve config.tools.pkg-config.sysroot"_str,
                path.as_path(),
                rstd::move(canonical).unwrap_err());
        }
        result.sysroot = Some(rstd::move(canonical).unwrap());
    }
    result.target_configured = ! result.library_paths.is_empty() || result.sysroot.is_some();
    return Ok(rstd::move(result));
}

auto configured_environment(const Toml& document, ref<rstd::path::Path> project_root)
    -> ConfigResult<ProcessEnvironmentSpec> {
    auto value = config_member(document, "environment"_str);
    if (value.is_none()) return Ok(ProcessEnvironmentSpec {});
    auto table = rstd_try(parse_toml::table(**value, NodePath::root("config.environment"_str)));
    rstd_try(parse_toml::reject_unknown(
        *table, NodePath::root("config.environment"_str), environment_config_key));
    auto append_path =
        configured_directories(**value, "append-path"_str, "config.environment"_str, project_root);
    if (append_path.is_err()) return Err(rstd::move(append_path).unwrap_err());
    return Ok(ProcessEnvironmentSpec {
        .append_path = rstd::move(append_path).unwrap(),
    });
}

auto configured_cmake(const Toml& value, ref<rstd::path::Path> project_root)
    -> ConfigResult<lito::dependency::CMakeProviderConfig> {
    auto result = lito::dependency::CMakeProviderConfig {
        .generator = String::make("Ninja"_str),
    };
    auto table = rstd_try(parse_toml::table(value, NodePath::root("config.tools.cmake"_str)));
    rstd_try(
        parse_toml::reject_unknown(*table, NodePath::root("config.tools.cmake"_str), cmake_key));
    auto generator = config_member(value, "generator"_str);
    if (generator.is_some()) {
        auto text = (**generator).as_str();
        if (text.is_none() || text->is_empty()) {
            return config_failure<lito::dependency::CMakeProviderConfig>(
                "config.tools.cmake.generator must be a non-empty string"_str);
        }
        result.generator = String::make(*text);
    }
    result.search_paths = rstd_try(
        configured_directories(value, "search-path"_str, "config.tools.cmake"_str, project_root));
    return Ok(rstd::move(result));
}

auto configured_cmake_build_overrides(const Toml& cmake)
    -> ConfigResult<lito::dependency::CMakeBuildOverrideSet> {
    auto result    = lito::dependency::CMakeBuildOverrideSet {};
    auto overrides = config_member(cmake, "overrides"_str);
    if (overrides.is_none()) return Ok(rstd::move(result));
    auto table = rstd_try(
        parse_toml::table(**overrides, NodePath::root("config.tools.cmake.overrides"_str)));
    auto keys = table->keys();
    for (auto key = keys.next(); key.is_some(); key = keys.next()) {
        const auto& package = **key;
        auto        context = rstd::format("config.tools.cmake.overrides.'{}'", package.as_str());
        if (! lito::dependency::cmake_package_name_is_valid(package.as_str())) {
            return config_failure<lito::dependency::CMakeBuildOverrideSet>(
                rstd::format("{} package name is unsafe", context.as_str()));
        }
        auto specification = table->get(package.as_str());
        auto fields =
            rstd_try(parse_toml::table(**specification, NodePath::root(context.as_str())));
        rstd_try(parse_toml::reject_unknown(
            *fields, NodePath::root(context.as_str()), cmake_override_key));
        auto source = config_member(**specification, "source"_str);
        if (source.is_none()) {
            return config_failure<lito::dependency::CMakeBuildOverrideSet>(
                rstd::format("{} is missing 'source'", context.as_str()));
        }
        auto value = (**source).as_str();
        if (value.is_none() || *value != "installed"_str) {
            return config_failure<lito::dependency::CMakeBuildOverrideSet>(
                rstd::format("{}.source must be 'installed'", context.as_str()));
        }
        result.entries.push(lito::dependency::CMakeBuildOverride {
            .package = package.clone(),
        });
    }
    rstd::slice_::sort_unstable_by(result.entries.as_mut_slice().as_mut_ref(),
                                   [](const lito::dependency::CMakeBuildOverride& left,
                                      const lito::dependency::CMakeBuildOverride& right) {
                                       return left.package < right.package;
                                   });
    return Ok(rstd::move(result));
}

auto default_toolchain() -> ToolchainSpec {
    return ToolchainSpec {
        .cc  = PathBuf::from("clang"_str),
        .cxx = PathBuf::from("clang++"_str),
        .ld  = PathBuf::from("ld.lld"_str),
        .ar  = PathBuf::from("llvm-ar"_str),
    };
}

auto configured_toolchain(const Toml& document, ToolchainSpec toolchain)
    -> ConfigResult<ToolchainSpec> {
    auto toolchain_value = config_member(document, "toolchain"_str);
    if (toolchain_value.is_none()) return Ok(rstd::move(toolchain));
    auto path  = NodePath::root("config.toolchain"_str);
    auto table = rstd_try(parse_toml::table(**toolchain_value, path));
    rstd_try(parse_toml::reject_unknown(*table, path, toolchain_config_key));
    return Ok(apply_toolchain_override(
        rstd::move(toolchain),
        ToolchainOverride {
            .cc = rstd_try(
                configured_tool_override(**toolchain_value, "cc"_str, "config.toolchain"_str)),
            .cxx = rstd_try(
                configured_tool_override(**toolchain_value, "cxx"_str, "config.toolchain"_str)),
            .ld = rstd_try(
                configured_tool_override(**toolchain_value, "ld"_str, "config.toolchain"_str)),
            .ar = rstd_try(
                configured_tool_override(**toolchain_value, "ar"_str, "config.toolchain"_str)),
            .sdk = rstd_try(configured_toolchain_sdk(**toolchain_value)),
        }));
}

struct DecodedHostTools {
    lito::tools::ToolSpec                     executables;
    lito::dependency::PkgConfigProviderConfig pkg_config;
    lito::dependency::CMakeProviderConfig     cmake;
    lito::dependency::CMakeBuildOverrideSet   cmake_build_overrides;
};

auto configured_host_tools(const Toml&           document,
                           ref<rstd::path::Path> project_root,
                           lito::tools::ToolSpec executables) -> ConfigResult<DecodedHostTools> {
    auto result = DecodedHostTools {
        .executables = rstd::move(executables),
        .cmake =
            lito::dependency::CMakeProviderConfig {
                .generator = String::make("Ninja"_str),
            },
    };
    auto value = config_member(document, "tools"_str);
    if (value.is_none()) return Ok(rstd::move(result));
    auto table = rstd_try(parse_toml::table(**value, NodePath::root("config.tools"_str)));
    rstd_try(
        parse_toml::reject_unknown(*table, NodePath::root("config.tools"_str), tools_config_key));
    auto tar = rstd_try(configured_tool_override(**value, "tar"_str, "config.tools"_str));
    if (tar.is_some()) result.executables.tar = rstd::move(tar).unwrap();
    auto bsdtar = rstd_try(configured_tool_override(**value, "bsdtar"_str, "config.tools"_str));
    if (bsdtar.is_some()) result.executables.bsdtar = rstd::move(bsdtar).unwrap();
    auto clang_format =
        rstd_try(configured_tool_override(**value, "clang-format"_str, "config.tools"_str));
    if (clang_format.is_some()) {
        result.executables.clang_format = rstd::move(clang_format).unwrap();
    }
    auto curl = rstd_try(configured_tool_override(**value, "curl"_str, "config.tools"_str));
    if (curl.is_some()) result.executables.curl = rstd::move(curl).unwrap();
    auto git = rstd_try(configured_tool_override(**value, "git"_str, "config.tools"_str));
    if (git.is_some()) result.executables.git = rstd::move(git).unwrap();
    auto strip = rstd_try(configured_tool_override(**value, "strip"_str, "config.tools"_str));
    if (strip.is_some()) result.executables.strip = rstd::move(strip).unwrap();
    constexpr lito::tools::Tool tool_values[] = {
        lito::tools::Tool::Tar,  lito::tools::Tool::BsdTar, lito::tools::Tool::ClangFormat,
        lito::tools::Tool::Curl, lito::tools::Tool::Git,    lito::tools::Tool::Strip,
    };
    for (const auto tool : tool_values) {
        if (config_member(**value, lito::tools::tool_name(tool)).is_some()) {
            result.executables.mark_configured(tool);
        }
    }

    auto cmake = config_member(**value, "cmake"_str);
    if (cmake.is_some()) {
        auto shorthand = (**cmake).as_str();
        if (shorthand.is_some()) {
            result.executables.cmake =
                rstd_try(configured_executable(**cmake, "config.tools.cmake"_str));
            result.executables.mark_configured(lito::tools::Tool::CMake);
        } else {
            result.cmake    = rstd_try(configured_cmake(**cmake, project_root));
            auto executable = rstd_try(
                configured_tool_override(**cmake, "executable"_str, "config.tools.cmake"_str));
            if (executable.is_some()) {
                result.executables.cmake = rstd::move(executable).unwrap();
                result.executables.mark_configured(lito::tools::Tool::CMake);
            }
            result.cmake_build_overrides = rstd_try(configured_cmake_build_overrides(**cmake));
        }
    }

    auto pkg_config = config_member(**value, "pkg-config"_str);
    if (pkg_config.is_some()) {
        auto shorthand = (**pkg_config).as_str();
        if (shorthand.is_some()) {
            result.executables.pkg_config =
                rstd_try(configured_executable(**pkg_config, "config.tools.pkg-config"_str));
            result.executables.mark_configured(lito::tools::Tool::PkgConfig);
            result.pkg_config.target_configured = true;
        } else {
            result.pkg_config = rstd_try(configured_pkg_config(**pkg_config, project_root));
            auto executable   = rstd_try(configured_tool_override(
                **pkg_config, "executable"_str, "config.tools.pkg-config"_str));
            if (executable.is_some()) {
                result.executables.pkg_config = rstd::move(executable).unwrap();
                result.executables.mark_configured(lito::tools::Tool::PkgConfig);
                result.pkg_config.target_configured = true;
            }
        }
    }
    return Ok(rstd::move(result));
}

auto default_lock_config(ref<rstd::path::Path> project_root) -> lito::lock::LockConfig {
    return lito::lock::LockConfig {
        .path = PathBuf::from(project_root).join(PathBuf::from("lito.lock"_str).as_path()),
    };
}

auto configured_lock(const Toml& document, ref<rstd::path::Path> project_root)
    -> ConfigResult<lito::lock::LockConfig> {
    auto value = config_member(document, "lock"_str);
    if (value.is_none()) return Ok(default_lock_config(project_root));
    auto table = rstd_try(parse_toml::table(**value, NodePath::root("config.lock"_str)));
    rstd_try(
        parse_toml::reject_unknown(*table, NodePath::root("config.lock"_str), lock_config_key));
    auto path_value = config_member(**value, "path"_str);
    if (path_value.is_none()) {
        return config_failure<lito::lock::LockConfig>("config.lock is missing 'path'"_str);
    }
    auto text = (**path_value).as_str();
    if (text.is_none() || text->is_empty()) {
        return config_failure<lito::lock::LockConfig>(
            "config.lock.path must be a non-empty string"_str);
    }
    auto requested = PathBuf::from(*text);
    if (requested.as_path().is_relative()) {
        requested = PathBuf::from(project_root).join(requested.as_path());
    }
    auto parent = requested.as_path().parent();
    auto name   = requested.as_path().file_name();
    if (parent.is_none() || name.is_none()) {
        return config_failure<lito::lock::LockConfig>(
            rstd::format("config.lock.path '{}' must name a file", requested.as_path()));
    }
    auto canonical_parent = rstd::fs::canonicalize(*parent);
    if (canonical_parent.is_err()) {
        return config_io_failure<lito::lock::LockConfig>("resolve config.lock.path parent"_str,
                                                         *parent,
                                                         rstd::move(canonical_parent).unwrap_err());
    }
    auto path   = canonical_parent->join(PathBuf::from(*name).as_path());
    auto exists = rstd::fs::exists(path.as_path());
    if (exists.is_err()) {
        return config_io_failure<lito::lock::LockConfig>(
            "inspect config.lock.path"_str, path.as_path(), rstd::move(exists).unwrap_err());
    }
    if (*exists) {
        auto metadata = rstd::fs::metadata(path.as_path());
        if (metadata.is_err()) {
            return config_io_failure<lito::lock::LockConfig>(
                "inspect config.lock.path"_str, path.as_path(), rstd::move(metadata).unwrap_err());
        }
        if (! metadata->is_file()) {
            return config_failure<lito::lock::LockConfig>(
                rstd::format("config.lock.path '{}' is not a file", path.as_path()));
        }
    }
    return Ok(lito::lock::LockConfig { .path = rstd::move(path) });
}

auto configured_install(const Toml& document, ref<rstd::path::Path> project_root)
    -> ConfigResult<InstallConfig> {
    auto value = config_member(document, "install"_str);
    if (value.is_none()) return Ok(InstallConfig {});
    auto table = rstd_try(parse_toml::table(**value, NodePath::root("config.install"_str)));
    rstd_try(parse_toml::reject_unknown(
        *table, NodePath::root("config.install"_str), install_config_key));
    auto root_value = config_member(**value, "root"_str);
    if (root_value.is_none()) {
        return config_failure<InstallConfig>("config.install is missing 'root'"_str);
    }
    auto text = (**root_value).as_str();
    if (text.is_none() || text->is_empty()) {
        return config_failure<InstallConfig>("config.install.root must be a non-empty string"_str);
    }
    auto root = PathBuf::from(*text);
    if (root.as_path().is_relative()) root = PathBuf::from(project_root).join(root.as_path());
    return Ok(InstallConfig { .root = Some(rstd::move(root)) });
}

auto configured_doc(const Toml& document, ref<rstd::path::Path> project_root)
    -> ConfigResult<DocConfig> {
    auto value = config_member(document, "doc"_str);
    if (value.is_none()) return Ok(DocConfig {});
    auto table = rstd_try(parse_toml::table(**value, NodePath::root("config.doc"_str)));
    rstd_try(parse_toml::reject_unknown(*table, NodePath::root("config.doc"_str), doc_config_key));
    auto path_value = config_member(**value, "litodoc-path"_str);
    if (path_value.is_none()) return Ok(DocConfig {});
    auto text = (**path_value).as_str();
    if (text.is_none() || text->is_empty()) {
        return config_failure<DocConfig>("config.doc.litodoc-path must be a non-empty string"_str);
    }
    auto path = PathBuf::from(*text);
    if (path.as_path().is_relative()) path = PathBuf::from(project_root).join(path.as_path());
    auto canonical = rstd::fs::canonicalize(path.as_path());
    if (canonical.is_err()) {
        return config_io_failure<DocConfig>("resolve config.doc.litodoc-path"_str,
                                            path.as_path(),
                                            rstd::move(canonical).unwrap_err());
    }
    auto metadata = rstd::fs::metadata(canonical->as_path());
    if (metadata.is_err()) {
        return config_io_failure<DocConfig>("inspect config.doc.litodoc-path"_str,
                                            canonical->as_path(),
                                            rstd::move(metadata).unwrap_err());
    }
    if (! metadata->is_dir()) {
        return config_failure<DocConfig>(
            rstd::format("config.doc.litodoc-path '{}' is not a directory", canonical->as_path()));
    }
    return Ok(DocConfig { .litodoc_path = Some(rstd::move(canonical).unwrap()) });
}

auto configured_sources(const Toml& document, ref<rstd::path::Path> project_root)
    -> ConfigResult<lito::source::PackageSourceConfig> {
    auto patches     = Vec<lito::source::GitSourcePatch>::make();
    auto patch_value = config_member(document, "patch"_str);
    if (patch_value.is_none()) {
        return Ok(lito::source::PackageSourceConfig { .patches = rstd::move(patches) });
    }

    auto patch_table =
        rstd_try(parse_toml::table(**patch_value, NodePath::root("config.patch"_str)));
    auto keys = patch_table->keys();
    for (auto key = keys.next(); key.is_some(); key = keys.next()) {
        const auto& url = **key;
        if (url.is_empty()) {
            return config_failure<lito::source::PackageSourceConfig>(
                "config.patch Git URL must not be empty"_str);
        }
        if (url.as_str().starts_with("-"_str)) {
            return config_failure<lito::source::PackageSourceConfig>(
                rstd::format("config.patch Git URL '{}' must not start with '-'", url.as_str()));
        }
        if (url.as_str().contains("#"_str)) {
            return config_failure<lito::source::PackageSourceConfig>(rstd::format(
                "config.patch Git URL '{}' must not contain a URL fragment", url.as_str()));
        }

        auto specification = patch_table->get(url.as_str());
        auto context       = rstd::format("config.patch.'{}'", url.as_str());
        auto table = rstd_try(parse_toml::table(**specification, NodePath::root(context.as_str())));
        rstd_try(
            parse_toml::reject_unknown(*table, NodePath::root(context.as_str()), patch_config_key));
        auto value = config_member(**specification, "path"_str);
        if (value.is_none()) {
            return config_failure<lito::source::PackageSourceConfig>(
                rstd::format("{} is missing 'path'", context.as_str()));
        }
        auto text = (**value).as_str();
        if (text.is_none()) {
            return config_failure<lito::source::PackageSourceConfig>(
                rstd::format("{}.path must be a string", context.as_str()));
        }
        if (text->is_empty()) {
            return config_failure<lito::source::PackageSourceConfig>(
                rstd::format("{}.path must not be empty", context.as_str()));
        }

        auto requested = PathBuf::from(*text);
        if (requested.as_path().is_relative()) {
            requested = PathBuf::from(project_root).join(requested.as_path());
        }
        auto canonical = rstd::fs::canonicalize(requested.as_path());
        if (canonical.is_err()) {
            return config_io_failure<lito::source::PackageSourceConfig>(
                rstd::format("resolve {}.path", context.as_str()).as_str(),
                requested.as_path(),
                rstd::move(canonical).unwrap_err());
        }
        auto resolved = rstd::move(canonical).unwrap();
        auto metadata = rstd::fs::metadata(resolved.as_path());
        if (metadata.is_err()) {
            return config_io_failure<lito::source::PackageSourceConfig>(
                rstd::format("inspect {}.path", context.as_str()).as_str(),
                resolved.as_path(),
                rstd::move(metadata).unwrap_err());
        }
        if (! metadata->is_dir()) {
            return config_failure<lito::source::PackageSourceConfig>(rstd::format(
                "{}.path '{}' is not a directory", context.as_str(), resolved.as_path()));
        }
        patches.push(lito::source::GitSourcePatch {
            .git  = url.clone(),
            .path = rstd::move(resolved),
        });
    }
    return Ok(lito::source::PackageSourceConfig { .patches = rstd::move(patches) });
}

auto decode_project_config(PathBuf               root,
                           const Toml&           document,
                           EnvironmentFlagPolicy environment_flags = EnvironmentFlagPolicy::Ignore,
                           Option<ProjectConfigDefaults> defaults  = None())
    -> ConfigResult<ProjectConfig> {
    auto root_table = rstd_try(parse_toml::table(document, NodePath::root("config root"_str)));
    rstd_try(parse_toml::reject_unknown(
        *root_table, NodePath::root("config root"_str), root_config_key));

    auto tool_defaults      = lito::tools::ToolSpec {};
    auto toolchain_defaults = default_toolchain();
    if (defaults.is_some()) {
        tool_defaults      = rstd::move(defaults->tools);
        toolchain_defaults = rstd::move(defaults->toolchain);
    }
    auto toolchain = rstd_try(configured_toolchain(document, rstd::move(toolchain_defaults)));
    auto tools =
        rstd_try(configured_host_tools(document, root.as_path(), rstd::move(tool_defaults)));
    auto standard_library         = StandardLibrarySelection::Auto;
    auto standard_library_runtime = StandardLibraryRuntime::Dynamic;
    auto toolchain_value          = config_member(document, "toolchain"_str);
    if (toolchain_value.is_some()) {
        standard_library         = rstd_try(configured_standard_library(**toolchain_value));
        standard_library_runtime = rstd_try(configured_standard_library_runtime(**toolchain_value));
    }

    auto environment = configured_environment(document, root.as_path());
    if (environment.is_err()) return Err(rstd::move(environment).unwrap_err());
    auto lock = configured_lock(document, root.as_path());
    if (lock.is_err()) return Err(rstd::move(lock).unwrap_err());
    auto sources = configured_sources(document, root.as_path());
    if (sources.is_err()) return Err(rstd::move(sources).unwrap_err());
    auto install       = configured_install(document, root.as_path());
    auto doc           = configured_doc(document, root.as_path());
    auto build_options = configured_build_options(document);
    auto build_target  = configured_build_target(document);
    if (install.is_err()) return Err(rstd::move(install).unwrap_err());
    if (doc.is_err()) return Err(rstd::move(doc).unwrap_err());
    if (build_options.is_err()) return Err(rstd::move(build_options).unwrap_err());
    if (build_target.is_err()) return Err(rstd::move(build_target).unwrap_err());
    auto effective_build_options = rstd::move(build_options).unwrap();
    rstd_try(append_environment_build_options(effective_build_options, environment_flags));

    return Ok(ProjectConfig {
        .root                     = rstd::move(root),
        .lock                     = rstd::move(lock).unwrap(),
        .environment              = rstd::move(environment).unwrap(),
        .tools                    = rstd::move(tools.executables),
        .toolchain                = rstd::move(toolchain),
        .standard_library         = standard_library,
        .standard_library_runtime = standard_library_runtime,
        .build_options            = rstd::move(effective_build_options),
        .build_target             = rstd::move(build_target).unwrap(),
        .sources                  = rstd::move(sources).unwrap(),
        .pkg_config               = rstd::move(tools.pkg_config),
        .cmake                    = rstd::move(tools.cmake),
        .cmake_build_overrides    = rstd::move(tools.cmake_build_overrides),
        .install                  = rstd::move(install).unwrap(),
        .doc                      = rstd::move(doc).unwrap(),
    });
}

auto decode_host_tool_command_config(PathBuf                       root,
                                     const Toml&                   document,
                                     Option<ProjectConfigDefaults> defaults = None())
    -> ConfigResult<HostToolCommandConfig> {
    auto root_table = rstd_try(parse_toml::table(document, NodePath::root("config root"_str)));
    rstd_try(parse_toml::reject_unknown(
        *root_table, NodePath::root("config root"_str), root_config_key));

    auto tool_defaults      = lito::tools::ToolSpec {};
    auto toolchain_defaults = default_toolchain();
    if (defaults.is_some()) {
        tool_defaults      = rstd::move(defaults->tools);
        toolchain_defaults = rstd::move(defaults->toolchain);
    }
    auto environment = rstd_try(configured_environment(document, root.as_path()));
    auto tools =
        rstd_try(configured_host_tools(document, root.as_path(), rstd::move(tool_defaults)));
    auto toolchain = rstd_try(configured_toolchain(document, rstd::move(toolchain_defaults)));
    return Ok(HostToolCommandConfig {
        .root        = rstd::move(root),
        .environment = rstd::move(environment),
        .tools       = rstd::move(tools.executables),
        .toolchain   = rstd::move(toolchain),
    });
}
