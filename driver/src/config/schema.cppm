module;
#include <rstd/macro.hpp>

module lito.driver:config.schema;

import rstd;
import rstd.serde;
import rstd.toml;
import lito.core;
import lito.tools;
import lito.tools.cargo;
import :config.project;
import :config.wire;
import lito.system;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito::config;
using namespace lito::system;
using namespace lito::tools;
using PathBuf = rstd::path::PathBuf;
using Toml    = rstd::toml::Value;

template<typename T>
auto config_failure(String message) -> ConfigResult<T> {
    return Err(ConfigError::Schema(rstd::move(message)));
}

template<typename T>
auto config_failure(ref<str> message) -> ConfigResult<T> {
    return Err(ConfigError::Schema(String::make(message)));
}

template<typename T>
auto config_data_failure(rstd::serde::DataPath path, ref<str> message) -> ConfigResult<T> {
    return Err(ConfigError::Data(rstd::serde::Error::invalid_value(rstd::move(path), message)));
}

template<typename T>
auto config_io_failure(ref<str>               operation,
                       ref<rstd::path::Path>  path,
                       rstd::io::error::Error source) -> ConfigResult<T> {
    return Err(ConfigError::Io(String::make(operation), PathBuf::from(path), rstd::move(source)));
}

auto normalize_host_tool_provider_shorthand(Toml& document) -> void {
    auto tools = document.get_mut("tools"_str);
    if (tools.is_none()) return;
    auto table = (**tools).as_table_mut();
    if (table.is_none()) return;
    constexpr ref<str> providers[] = {
        "cargo"_str,
        "cmake"_str,
        "pkg-config"_str,
    };
    for (const auto provider : providers) {
        auto value = (**table).remove(provider);
        if (value.is_none()) continue;
        auto entry = rstd::move(value).unwrap_unchecked();
        if (entry.as_str().is_some()) {
            auto normalized = rstd::toml::Table::make();
            normalized.insert(String::make("executable"_str), rstd::move(entry));
            entry = Toml::Table(rstd::move(normalized));
        }
        (**table).insert(String::make(provider), rstd::move(entry));
    }
}

auto decode_config_wire(const Toml& document) -> ConfigResult<lito::config::wire::Document> {
    auto decoded = rstd::toml::decode_value<lito::config::wire::Document>(document);
    if (decoded.is_err()) {
        return Err(ConfigError::Data(rstd::move(decoded).unwrap_err_unchecked()));
    }
    return Ok(rstd::move(decoded).unwrap_unchecked());
}

auto decode_host_config_wire(const Toml& document)
    -> ConfigResult<lito::config::wire::HostDocument> {
    auto decoded = rstd::toml::decode_value<lito::config::wire::HostDocument>(document);
    if (decoded.is_err()) {
        return Err(ConfigError::Data(rstd::move(decoded).unwrap_err_unchecked()));
    }
    return Ok(rstd::move(decoded).unwrap_unchecked());
}

auto configured_executable(ref<str> value, rstd::serde::DataPath path) -> ConfigResult<PathBuf> {
    if (value.is_empty()) {
        return config_data_failure<PathBuf>(rstd::move(path), "executable must not be empty"_str);
    }
    auto executable = PathBuf::from(value);
    if (! executable.as_path().is_absolute() &&
        ! is_searchable_executable_name(executable.as_path())) {
        return config_data_failure<PathBuf>(rstd::move(path),
                                            "must be an executable name or absolute path"_str);
    }
    return Ok(rstd::move(executable));
}

auto configured_tool_override(const Option<String>& value, rstd::serde::DataPath path)
    -> ConfigResult<Option<PathBuf>> {
    if (value.is_none()) return Ok(Option<PathBuf> {});
    return Ok(Some(rstd_try(configured_executable(value->as_str(), rstd::move(path)))));
}

auto configured_linker_override(const Option<String>& value, rstd::serde::DataPath path)
    -> ConfigResult<Option<PathBuf>> {
    if (value.is_none()) return Ok(Option<PathBuf> {});
    auto linker = rstd_try(configured_executable(value->as_str(), path.clone()));
    if (linker.as_path().is_absolute() || linker.as_path().to_str() == Some("lld"_str)) {
        return Ok(Some(rstd::move(linker)));
    }
    return config_data_failure<Option<PathBuf>>(rstd::move(path),
                                                "must be 'lld' or an absolute path to LLD"_str);
}

auto configured_toolchain_sdk(const Option<lito::config::wire::Sdk>& value)
    -> ConfigResult<Option<ToolchainSdkSelection>> {
    if (value.is_none()) return Ok(Option<ToolchainSdkSelection> {});
    auto path = rstd::serde::DataPath().with_field("toolchain"_str).with_field("sdk"_str);
    auto kind = parse_sdk_kind(value->kind.as_str());
    if (kind.is_none()) {
        return config_data_failure<Option<ToolchainSdkSelection>>(
            path.with_field("kind"_str), "must be 'llvm' or 'android-ndk'"_str);
    }
    if (value->version.is_some() == value->path.is_some()) {
        return config_data_failure<Option<ToolchainSdkSelection>>(
            rstd::move(path), "must contain exactly one of 'version' or 'path'"_str);
    }
    if (value->version.is_some()) {
        if (value->version->is_empty()) {
            return config_data_failure<Option<ToolchainSdkSelection>>(
                path.with_field("version"_str), "must not be empty"_str);
        }
        return Ok(
            Some(ToolchainSdkSelection::Managed(*kind, String::make(value->version->as_str()))));
    }
    if (value->path->is_empty()) {
        return config_data_failure<Option<ToolchainSdkSelection>>(path.with_field("path"_str),
                                                                  "must not be empty"_str);
    }
    auto directory = PathBuf::from(value->path->as_str());
    if (! directory.as_path().is_absolute()) {
        return config_data_failure<Option<ToolchainSdkSelection>>(path.with_field("path"_str),
                                                                  "must be absolute"_str);
    }
    return Ok(Some(ToolchainSdkSelection::Directory(*kind, rstd::move(directory))));
}

auto configured_standard_library(const Option<lito::config::wire::Toolchain>& toolchain)
    -> ConfigResult<StandardLibrarySelection> {
    if (toolchain.is_none() || toolchain->standard_library.is_none()) {
        return Ok(StandardLibrarySelection::Auto);
    }
    auto parsed = parse_standard_library_selection(toolchain->standard_library->as_str());
    if (parsed.is_some()) return Ok(*parsed);
    return config_data_failure<StandardLibrarySelection>(
        rstd::serde::DataPath().with_field("toolchain"_str).with_field("stdlib"_str),
        "must be 'auto', 'libc++', 'libstdc++', or 'msvc'"_str);
}

auto configured_standard_library_runtime(const Option<lito::config::wire::Toolchain>& toolchain)
    -> ConfigResult<StandardLibraryRuntime> {
    if (toolchain.is_none() || toolchain->standard_library_runtime.is_none()) {
        return Ok(StandardLibraryRuntime::Dynamic);
    }
    auto parsed = parse_standard_library_runtime(toolchain->standard_library_runtime->as_str());
    if (parsed.is_some()) return Ok(*parsed);
    return config_data_failure<StandardLibraryRuntime>(
        rstd::serde::DataPath().with_field("toolchain"_str).with_field("stdlib-runtime"_str),
        "must be 'dynamic' or 'static'"_str);
}

auto configured_build_target(const Option<lito::config::wire::Build>& build)
    -> ConfigResult<BuildTargetRequest> {
    if (build.is_none() || build->target.is_none()) return Ok(BuildTargetRequest::Default());
    const auto& value = *build->target;
    auto        path  = rstd::serde::DataPath().with_field("build"_str).with_field("target"_str);
    if (value.kind.as_str() != "android"_str) {
        return config_data_failure<BuildTargetRequest>(path.with_field("kind"_str),
                                                       "must be 'android'"_str);
    }
    if (value.abi.is_empty()) {
        return config_data_failure<BuildTargetRequest>(path.with_field("abi"_str),
                                                       "must not be empty"_str);
    }
    if (value.minimum_api <= i64 {} || value.minimum_api > i64(u32::MAX.to_primitive())) {
        return config_data_failure<BuildTargetRequest>(path.with_field("min-api"_str),
                                                       "must be a positive 32-bit integer"_str);
    }
    return Ok(BuildTargetRequest::Android(AndroidTargetRequest {
        .abi         = value.abi.clone(),
        .minimum_api = u32(value.minimum_api.to_primitive()),
    }));
}

auto configured_build_option_input(const Option<Vec<String>>& values,
                                   rstd::serde::DataPath      path,
                                   ref<str> source) -> ConfigResult<Option<BuildOptionInput>> {
    if (values.is_none() || values->is_empty()) return Ok(Option<BuildOptionInput> {});
    auto arguments = Vec<String>::with_capacity(values->len());
    for (usize index {}; index < values->len(); ++index) {
        if ((*values)[index].is_empty()) {
            return config_data_failure<Option<BuildOptionInput>>(
                path.with_index(index), "compiler option must not be empty"_str);
        }
        arguments.push((*values)[index].clone());
    }
    return Ok(Some(BuildOptionInput {
        .arguments = rstd::move(arguments),
        .source    = String::make(source),
    }));
}

auto configured_build_options(const Option<lito::config::wire::Build>& build)
    -> ConfigResult<ProjectBuildOptions> {
    auto result = ProjectBuildOptions {};
    if (build.is_none()) return Ok(rstd::move(result));
    auto root = rstd::serde::DataPath().with_field("build"_str);
    auto cpp  = rstd_try(configured_build_option_input(
        build->options, root.with_field("options"_str), "config.build.options"_str));
    if (cpp.is_some()) result.cpp.push(rstd::move(cpp).unwrap_unchecked());
    auto linker = rstd_try(configured_build_option_input(build->linker_options,
                                                         root.with_field("linker-options"_str),
                                                         "config.build.linker-options"_str));
    if (linker.is_some()) result.linker.push(rstd::move(linker).unwrap_unchecked());
    if (build->c.is_some()) {
        auto c = rstd_try(
            configured_build_option_input(build->c->options,
                                          root.with_field("c"_str).with_field("options"_str),
                                          "config.build.c.options"_str));
        if (c.is_some()) result.c.push(rstd::move(c).unwrap_unchecked());
    }
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
        .arguments = rstd::move(arguments).unwrap_unchecked(),
        .source    = String::make(name),
    }));
}

auto append_environment_build_options(ProjectBuildOptions& options, EnvironmentFlagPolicy policy)
    -> ConfigResult<empty> {
    if (policy == EnvironmentFlagPolicy::Ignore) return Ok(empty {});
    auto cpp = rstd_try(environment_build_option(ToolchainEnvironmentVariable::CxxFlags));
    if (cpp.is_some()) options.cpp.push(rstd::move(cpp).unwrap_unchecked());
    auto c = rstd_try(environment_build_option(ToolchainEnvironmentVariable::CFlags));
    if (c.is_some()) options.c.push(rstd::move(c).unwrap_unchecked());
    auto linker = rstd_try(environment_build_option(ToolchainEnvironmentVariable::LdFlags));
    if (linker.is_some()) options.linker.push(rstd::move(linker).unwrap_unchecked());
    return Ok(empty {});
}

auto configured_directories(const Option<Vec<String>>& values,
                            rstd::serde::DataPath      path,
                            ref<str>                   context,
                            ref<rstd::path::Path>      project_root) -> ConfigResult<Vec<PathBuf>> {
    auto result = Vec<PathBuf>::make();
    if (values.is_none()) return Ok(rstd::move(result));
    result.reserve(values->len());
    for (usize index {}; index < values->len(); ++index) {
        const auto& value = (*values)[index];
        if (value.is_empty()) {
            return config_data_failure<Vec<PathBuf>>(path.with_index(index),
                                                     "path must not be empty"_str);
        }
        auto requested = PathBuf::from(value.as_str());
        if (requested.as_path().is_relative()) {
            requested = PathBuf::from(project_root).join(requested.as_path());
        }
        auto canonical = rstd::fs::canonicalize(requested.as_path());
        if (canonical.is_err()) {
            return config_io_failure<Vec<PathBuf>>(
                rstd::format("resolve {} path", context).as_str(),
                requested.as_path(),
                rstd::move(canonical).unwrap_err());
        }
        auto metadata = rstd::fs::metadata(canonical->as_path());
        if (metadata.is_err()) {
            return config_io_failure<Vec<PathBuf>>(
                rstd::format("inspect {} path", context).as_str(),
                canonical->as_path(),
                rstd::move(metadata).unwrap_err());
        }
        if (! metadata->is_dir()) {
            return config_data_failure<Vec<PathBuf>>(path.with_index(index),
                                                     "path is not a directory"_str);
        }
        result.push(rstd::move(canonical).unwrap_unchecked());
    }
    return Ok(rstd::move(result));
}

auto configured_pkg_config(const lito::config::wire::PkgConfig& value,
                           ref<rstd::path::Path>                project_root)
    -> ConfigResult<lito::dependency::PkgConfigProviderConfig> {
    auto root   = rstd::serde::DataPath().with_field("tools"_str).with_field("pkg-config"_str);
    auto result = lito::dependency::PkgConfigProviderConfig {};
    result.search_paths = rstd_try(configured_directories(value.search_path,
                                                          root.with_field("search-path"_str),
                                                          "config.tools.pkg-config.search-path"_str,
                                                          project_root));
    result.library_paths =
        rstd_try(configured_directories(value.library_path,
                                        root.with_field("library-path"_str),
                                        "config.tools.pkg-config.library-path"_str,
                                        project_root));
    if (value.sysroot.is_some()) {
        if (value.sysroot->is_empty()) {
            return config_data_failure<lito::dependency::PkgConfigProviderConfig>(
                root.with_field("sysroot"_str), "must not be empty"_str);
        }
        auto requested = PathBuf::from(value.sysroot->as_str());
        if (requested.as_path().is_relative()) {
            requested = PathBuf::from(project_root).join(requested.as_path());
        }
        auto canonical = rstd::fs::canonicalize(requested.as_path());
        if (canonical.is_err()) {
            return config_io_failure<lito::dependency::PkgConfigProviderConfig>(
                "resolve config.tools.pkg-config.sysroot"_str,
                requested.as_path(),
                rstd::move(canonical).unwrap_err());
        }
        result.sysroot = Some(rstd::move(canonical).unwrap_unchecked());
    }
    result.target_configured = ! result.library_paths.is_empty() || result.sysroot.is_some();
    return Ok(rstd::move(result));
}

auto configured_environment(const Option<lito::config::wire::Environment>& value,
                            ref<rstd::path::Path>                          project_root)
    -> ConfigResult<ProcessEnvironmentSpec> {
    if (value.is_none()) return Ok(ProcessEnvironmentSpec {});
    return Ok(ProcessEnvironmentSpec {
        .append_path = rstd_try(configured_directories(
            value->append_path,
            rstd::serde::DataPath().with_field("environment"_str).with_field("append-path"_str),
            "config.environment.append-path"_str,
            project_root)),
    });
}

auto configured_cmake(const lito::config::wire::CMake& value, ref<rstd::path::Path> project_root)
    -> ConfigResult<lito::dependency::CMakeProviderConfig> {
    auto result = lito::dependency::CMakeProviderConfig {
        .generator = String::make("Ninja"_str),
    };
    auto root = rstd::serde::DataPath().with_field("tools"_str).with_field("cmake"_str);
    if (value.generator.is_some()) {
        if (value.generator->is_empty()) {
            return config_data_failure<lito::dependency::CMakeProviderConfig>(
                root.with_field("generator"_str), "must not be empty"_str);
        }
        result.generator = value.generator->clone();
    }
    result.search_paths = rstd_try(configured_directories(value.search_path,
                                                          root.with_field("search-path"_str),
                                                          "config.tools.cmake.search-path"_str,
                                                          project_root));
    return Ok(rstd::move(result));
}

auto configured_cmake_build_overrides(const lito::config::wire::CMake& cmake)
    -> ConfigResult<lito::dependency::CMakeBuildOverrideSet> {
    auto result = lito::dependency::CMakeBuildOverrideSet {};
    if (cmake.overrides.is_none()) return Ok(rstd::move(result));
    const auto& overrides = *cmake.overrides;
    for (auto key : overrides.keys()) {
        const auto& package = *key;
        auto        path    = rstd::serde::DataPath()
                                  .with_field("tools"_str)
                                  .with_field("cmake"_str)
                                  .with_field("overrides"_str)
                                  .with_map_key(package.as_str());
        if (! lito::dependency::cmake_package_name_is_valid(package.as_str())) {
            return config_data_failure<lito::dependency::CMakeBuildOverrideSet>(
                rstd::move(path), "package name is unsafe"_str);
        }
        const auto specification = overrides.get(package.as_str()).unwrap_unchecked();
        if (specification->source.as_str() != "installed"_str) {
            return config_data_failure<lito::dependency::CMakeBuildOverrideSet>(
                path.with_field("source"_str), "must be 'installed'"_str);
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
        .ld  = PathBuf::from("lld"_str),
        .ar  = PathBuf::from("llvm-ar"_str),
    };
}

auto configured_toolchain_target(const lito::config::wire::Toolchain& value,
                                 const rstd::serde::DataPath&         root)
    -> ConfigResult<Option<ToolchainTargetSelection>> {
    if (value.os.is_none() && value.arch.is_none()) return Ok(None());
    if (value.os.is_none()) {
        return config_data_failure<Option<ToolchainTargetSelection>>(
            root.with_field("os"_str), "must be configured together with toolchain.arch"_str);
    }
    if (value.arch.is_none()) {
        return config_data_failure<Option<ToolchainTargetSelection>>(
            root.with_field("arch"_str), "must be configured together with toolchain.os"_str);
    }
    auto os = parse_operating_system(value.os->as_str());
    if (os.is_none()) {
        return config_data_failure<Option<ToolchainTargetSelection>>(
            root.with_field("os"_str),
            rstd::format("must be one of {}", operating_system_choices().as_str()));
    }
    auto architecture = require_architecture(value.arch->as_str());
    if (architecture.is_err()) {
        auto error = rstd::move(architecture).unwrap_err();
        return config_data_failure<Option<ToolchainTargetSelection>>(root.with_field("arch"_str),
                                                                     error.message());
    }
    return Ok(Some(ToolchainTargetSelection::Config(*os, rstd::move(architecture).unwrap())));
}

auto configured_toolchain(const Option<lito::config::wire::Toolchain>& value,
                          ToolchainSpec toolchain) -> ConfigResult<ToolchainSpec> {
    if (value.is_none()) return Ok(rstd::move(toolchain));
    auto root = rstd::serde::DataPath().with_field("toolchain"_str);
    return Ok(apply_toolchain_override(
        rstd::move(toolchain),
        ToolchainOverride {
            .cc     = rstd_try(configured_tool_override(value->cc, root.with_field("cc"_str))),
            .cxx    = rstd_try(configured_tool_override(value->cxx, root.with_field("cxx"_str))),
            .ld     = rstd_try(configured_linker_override(value->ld, root.with_field("ld"_str))),
            .ar     = rstd_try(configured_tool_override(value->ar, root.with_field("ar"_str))),
            .sdk    = rstd_try(configured_toolchain_sdk(value->sdk)),
            .target = rstd_try(configured_toolchain_target(*value, root)),
        }));
}

struct DecodedHostTools {
    lito::tools::ToolSpec                     executables;
    lito::tools::cargo::Configuration         cargo;
    lito::dependency::PkgConfigProviderConfig pkg_config;
    lito::dependency::CMakeProviderConfig     cmake;
    lito::dependency::CMakeBuildOverrideSet   cmake_build_overrides;
};

auto configured_host_tools(const Option<lito::config::wire::Tools>& value,
                           ref<rstd::path::Path>                    project_root,
                           lito::tools::ToolSpec executables) -> ConfigResult<DecodedHostTools> {
    auto result = DecodedHostTools {
        .executables = rstd::move(executables),
        .cmake =
            lito::dependency::CMakeProviderConfig {
                .generator = String::make("Ninja"_str),
            },
    };
    if (value.is_none()) return Ok(rstd::move(result));
    auto root  = rstd::serde::DataPath().with_field("tools"_str);
    auto apply = [&](const Option<String>& configured,
                     lito::tools::Tool     tool) -> ConfigResult<empty> {
        if (configured.is_none()) return Ok(empty {});
        auto executable =
            rstd_try(configured_executable(configured->as_str(), root.with_field(tool_name(tool))));
        switch (tool) {
        case lito::tools::Tool::Tar: result.executables.tar = rstd::move(executable); break;
        case lito::tools::Tool::BsdTar: result.executables.bsdtar = rstd::move(executable); break;
        case lito::tools::Tool::ClangFormat:
            result.executables.clang_format = rstd::move(executable);
            break;
        case lito::tools::Tool::Curl: result.executables.curl = rstd::move(executable); break;
        case lito::tools::Tool::Git: result.executables.git = rstd::move(executable); break;
        case lito::tools::Tool::Strip: result.executables.strip = rstd::move(executable); break;
        case lito::tools::Tool::Cargo:
        case lito::tools::Tool::CMake:
        case lito::tools::Tool::PkgConfig:
            return config_failure<empty>("provider tools require table configuration"_str);
        }
        result.executables.mark_configured(tool);
        return Ok(empty {});
    };
    rstd_try(apply(value->tar, lito::tools::Tool::Tar));
    rstd_try(apply(value->bsdtar, lito::tools::Tool::BsdTar));
    rstd_try(apply(value->clang_format, lito::tools::Tool::ClangFormat));
    rstd_try(apply(value->curl, lito::tools::Tool::Curl));
    rstd_try(apply(value->git, lito::tools::Tool::Git));
    rstd_try(apply(value->strip, lito::tools::Tool::Strip));

    if (value->cargo.is_some()) {
        result.cargo.offline = value->cargo->offline.is_some() && *value->cargo->offline;
        if (value->cargo->executable.is_some()) {
            result.executables.cargo = rstd_try(
                configured_executable(value->cargo->executable->as_str(),
                                      root.with_field("cargo"_str).with_field("executable"_str)));
            result.executables.mark_configured(lito::tools::Tool::Cargo);
        }
    }

    if (value->cmake.is_some()) {
        result.cmake = rstd_try(configured_cmake(*value->cmake, project_root));
        if (value->cmake->executable.is_some()) {
            result.executables.cmake = rstd_try(
                configured_executable(value->cmake->executable->as_str(),
                                      root.with_field("cmake"_str).with_field("executable"_str)));
            result.executables.mark_configured(lito::tools::Tool::CMake);
        }
        result.cmake_build_overrides = rstd_try(configured_cmake_build_overrides(*value->cmake));
    }
    if (value->pkg_config.is_some()) {
        result.pkg_config = rstd_try(configured_pkg_config(*value->pkg_config, project_root));
        if (value->pkg_config->executable.is_some()) {
            result.executables.pkg_config = rstd_try(configured_executable(
                value->pkg_config->executable->as_str(),
                root.with_field("pkg-config"_str).with_field("executable"_str)));
            result.executables.mark_configured(lito::tools::Tool::PkgConfig);
            result.pkg_config.target_configured = true;
        }
    }
    return Ok(rstd::move(result));
}

auto default_lock_config(ref<rstd::path::Path> project_root) -> lito::lock::LockConfig {
    return lito::lock::LockConfig {
        .path = PathBuf::from(project_root).join(PathBuf::from("lito.lock"_str).as_path()),
    };
}

auto configured_lock(const Option<lito::config::wire::Lock>& value,
                     ref<rstd::path::Path> project_root) -> ConfigResult<lito::lock::LockConfig> {
    if (value.is_none()) return Ok(default_lock_config(project_root));
    auto path = rstd::serde::DataPath().with_field("lock"_str).with_field("path"_str);
    if (value->path.is_empty()) {
        return config_data_failure<lito::lock::LockConfig>(rstd::move(path),
                                                           "must not be empty"_str);
    }
    auto requested = PathBuf::from(value->path.as_str());
    if (requested.as_path().is_relative()) {
        requested = PathBuf::from(project_root).join(requested.as_path());
    }
    auto parent = requested.as_path().parent();
    auto name   = requested.as_path().file_name();
    if (parent.is_none() || name.is_none()) {
        return config_data_failure<lito::lock::LockConfig>(rstd::move(path),
                                                           "must name a file"_str);
    }
    auto canonical_parent = rstd::fs::canonicalize(*parent);
    if (canonical_parent.is_err()) {
        return config_io_failure<lito::lock::LockConfig>("resolve config.lock.path parent"_str,
                                                         *parent,
                                                         rstd::move(canonical_parent).unwrap_err());
    }
    auto resolved = canonical_parent->join(PathBuf::from(*name).as_path());
    auto exists   = rstd::fs::exists(resolved.as_path());
    if (exists.is_err()) {
        return config_io_failure<lito::lock::LockConfig>(
            "inspect config.lock.path"_str, resolved.as_path(), rstd::move(exists).unwrap_err());
    }
    if (*exists) {
        auto metadata = rstd::fs::metadata(resolved.as_path());
        if (metadata.is_err()) {
            return config_io_failure<lito::lock::LockConfig>("inspect config.lock.path"_str,
                                                             resolved.as_path(),
                                                             rstd::move(metadata).unwrap_err());
        }
        if (! metadata->is_file()) {
            return config_data_failure<lito::lock::LockConfig>(rstd::move(path),
                                                               "must identify a file"_str);
        }
    }
    return Ok(lito::lock::LockConfig { .path = rstd::move(resolved) });
}

auto configured_install(const Option<lito::config::wire::Install>& value,
                        ref<rstd::path::Path> project_root) -> ConfigResult<InstallConfig> {
    if (value.is_none()) return Ok(InstallConfig {});
    if (value->root.is_empty()) {
        return config_data_failure<InstallConfig>(
            rstd::serde::DataPath().with_field("install"_str).with_field("root"_str),
            "must not be empty"_str);
    }
    auto root = PathBuf::from(value->root.as_str());
    if (root.as_path().is_relative()) root = PathBuf::from(project_root).join(root.as_path());
    return Ok(InstallConfig { .root = Some(rstd::move(root)) });
}

auto configured_doc(const Option<lito::config::wire::Doc>& value,
                    ref<rstd::path::Path> project_root) -> ConfigResult<DocConfig> {
    if (value.is_none() || value->litodoc_path.is_none()) return Ok(DocConfig {});
    auto path = rstd::serde::DataPath().with_field("doc"_str).with_field("litodoc-path"_str);
    if (value->litodoc_path->is_empty()) {
        return config_data_failure<DocConfig>(rstd::move(path), "must not be empty"_str);
    }
    auto requested = PathBuf::from(value->litodoc_path->as_str());
    if (requested.as_path().is_relative()) {
        requested = PathBuf::from(project_root).join(requested.as_path());
    }
    auto canonical = rstd::fs::canonicalize(requested.as_path());
    if (canonical.is_err()) {
        return config_io_failure<DocConfig>("resolve config.doc.litodoc-path"_str,
                                            requested.as_path(),
                                            rstd::move(canonical).unwrap_err());
    }
    auto metadata = rstd::fs::metadata(canonical->as_path());
    if (metadata.is_err()) {
        return config_io_failure<DocConfig>("inspect config.doc.litodoc-path"_str,
                                            canonical->as_path(),
                                            rstd::move(metadata).unwrap_err());
    }
    if (! metadata->is_dir()) {
        return config_data_failure<DocConfig>(rstd::move(path), "must be a directory"_str);
    }
    return Ok(DocConfig { .litodoc_path = Some(rstd::move(canonical).unwrap_unchecked()) });
}

auto configured_builtin_sources(const Option<lito::config::wire::Builtin>& value,
                                ref<rstd::path::Path>                      project_root)
    -> ConfigResult<Vec<lito::source::BuiltinPackageSourceEntry>> {
    auto result = Vec<lito::source::BuiltinPackageSourceEntry>::make();
    if (value.is_none() || value->packages.is_none()) return Ok(rstd::move(result));
    for (auto key : value->packages->keys()) {
        auto id        = (*key).as_str();
        auto data_path = rstd::serde::DataPath()
                             .with_field("builtin"_str)
                             .with_field("packages"_str)
                             .with_map_key(id);
        if (lito::registry::RegistryPackageName::parse(id).is_err()) {
            return config_data_failure<Vec<lito::source::BuiltinPackageSourceEntry>>(
                rstd::move(data_path), "key must be a valid builtin package id"_str);
        }
        const auto& source       = **value->packages->get(id);
        const auto  source_count = usize(source.version.is_some()) + usize(source.git.is_some()) +
                                   usize(source.path.is_some());
        if (source_count != usize(1)) {
            return config_data_failure<Vec<lito::source::BuiltinPackageSourceEntry>>(
                rstd::move(data_path),
                "must contain exactly one of 'version', 'git', or 'path'"_str);
        }
        if (source.version.is_some()) {
            if (source.branch.is_some() || source.tag.is_some() || source.rev.is_some() ||
                source.commit.is_some()) {
                return config_data_failure<Vec<lito::source::BuiltinPackageSourceEntry>>(
                    rstd::move(data_path), "Registry source cannot contain Git selectors"_str);
            }
            auto requirement = lito::registry::VersionRequirement::parse(source.version->as_str());
            if (requirement.is_err()) {
                return config_data_failure<Vec<lito::source::BuiltinPackageSourceEntry>>(
                    data_path.with_field("version"_str), "must be a valid version requirement"_str);
            }
            result.push(lito::source::BuiltinPackageSourceEntry {
                .id     = (*key).clone(),
                .source = lito::source::BuiltinPackageSource::Registry(
                    source.registry.clone(), rstd::move(requirement).unwrap()),
            });
            continue;
        }
        if (source.path.is_some()) {
            if (source.registry.is_some() || source.branch.is_some() || source.tag.is_some() ||
                source.rev.is_some() || source.commit.is_some()) {
                return config_data_failure<Vec<lito::source::BuiltinPackageSourceEntry>>(
                    rstd::move(data_path), "Path source cannot contain Registry or Git fields"_str);
            }
            auto requested = PathBuf::from(source.path->as_str());
            if (requested.as_path().is_relative()) {
                requested = PathBuf::from(project_root).join(requested.as_path());
            }
            auto canonical = rstd::fs::canonicalize(requested.as_path());
            if (canonical.is_err()) {
                return config_io_failure<Vec<lito::source::BuiltinPackageSourceEntry>>(
                    "resolve builtin package path"_str,
                    requested.as_path(),
                    rstd::move(canonical).unwrap_err());
            }
            auto metadata = rstd::fs::metadata(canonical->as_path());
            if (metadata.is_err()) {
                return config_io_failure<Vec<lito::source::BuiltinPackageSourceEntry>>(
                    "inspect builtin package path"_str,
                    canonical->as_path(),
                    rstd::move(metadata).unwrap_err());
            }
            if (! metadata->is_dir()) {
                return config_data_failure<Vec<lito::source::BuiltinPackageSourceEntry>>(
                    data_path.with_field("path"_str), "must be a directory"_str);
            }
            result.push(lito::source::BuiltinPackageSourceEntry {
                .id     = (*key).clone(),
                .source = lito::source::BuiltinPackageSource::Path(rstd::move(canonical).unwrap()),
            });
            continue;
        }
        if (source.registry.is_some()) {
            return config_data_failure<Vec<lito::source::BuiltinPackageSourceEntry>>(
                data_path.with_field("registry"_str), "is only valid with 'version'"_str);
        }
        if (source.git->is_empty() || source.git->as_str().starts_with("-"_str) ||
            source.git->as_str().contains("#"_str)) {
            return config_data_failure<Vec<lito::source::BuiltinPackageSourceEntry>>(
                data_path.with_field("git"_str), "must be a valid Git URL"_str);
        }
        const auto selector_count = usize(source.branch.is_some()) + usize(source.tag.is_some()) +
                                    usize(source.rev.is_some()) + usize(source.commit.is_some());
        if (selector_count > usize(1)) {
            return config_data_failure<Vec<lito::source::BuiltinPackageSourceEntry>>(
                rstd::move(data_path), "Git source accepts at most one selector"_str);
        }
        auto reference = lito::source::GitReference {};
        if (source.branch.is_some()) {
            reference.kind  = lito::source::GitReferenceKind::Branch;
            reference.value = source.branch->clone();
        } else if (source.tag.is_some()) {
            reference.kind  = lito::source::GitReferenceKind::Tag;
            reference.value = source.tag->clone();
        } else if (source.rev.is_some()) {
            reference.kind  = lito::source::GitReferenceKind::Rev;
            reference.value = source.rev->clone();
        } else if (source.commit.is_some()) {
            if (! lito::source::git_commit_is_valid(source.commit->as_str())) {
                return config_data_failure<Vec<lito::source::BuiltinPackageSourceEntry>>(
                    data_path.with_field("commit"_str),
                    "must be a 40-digit hexadecimal commit"_str);
            }
            reference.kind  = lito::source::GitReferenceKind::Commit;
            reference.value = source.commit->clone();
        }
        result.push(lito::source::BuiltinPackageSourceEntry {
            .id = (*key).clone(),
            .source =
                lito::source::BuiltinPackageSource::Git(source.git->clone(), rstd::move(reference)),
        });
    }
    return Ok(rstd::move(result));
}

auto configured_sources(
    const Option<rstd::collections::BTreeMap<String, lito::config::wire::Patch>>& value,
    const Option<lito::config::wire::Builtin>&                                    builtin,
    ref<rstd::path::Path> project_root) -> ConfigResult<lito::source::PackageSourceConfig> {
    auto patches = Vec<lito::source::GitSourcePatch>::make();
    if (value.is_none()) {
        return Ok(lito::source::PackageSourceConfig {
            .patches          = rstd::move(patches),
            .builtin_packages = rstd_try(configured_builtin_sources(builtin, project_root)),
        });
    }
    for (auto key : value->keys()) {
        const auto& url = *key;
        auto path = rstd::serde::DataPath().with_field("patch"_str).with_map_key(url.as_str());
        if (url.is_empty()) {
            return config_data_failure<lito::source::PackageSourceConfig>(
                rstd::move(path), "URL must not be empty"_str);
        }
        if (url.as_str().starts_with("-"_str)) {
            return config_data_failure<lito::source::PackageSourceConfig>(
                rstd::move(path), "URL must not start with '-'"_str);
        }
        if (url.as_str().contains("#"_str)) {
            return config_data_failure<lito::source::PackageSourceConfig>(
                rstd::move(path), "URL must not contain a fragment"_str);
        }
        const auto specification = value->get(url.as_str()).unwrap_unchecked();
        if (specification->path.is_empty()) {
            return config_data_failure<lito::source::PackageSourceConfig>(
                path.with_field("path"_str), "must not be empty"_str);
        }
        auto requested = PathBuf::from(specification->path.as_str());
        if (requested.as_path().is_relative()) {
            requested = PathBuf::from(project_root).join(requested.as_path());
        }
        auto canonical = rstd::fs::canonicalize(requested.as_path());
        if (canonical.is_err()) {
            return config_io_failure<lito::source::PackageSourceConfig>(
                "resolve config.patch path"_str,
                requested.as_path(),
                rstd::move(canonical).unwrap_err());
        }
        auto metadata = rstd::fs::metadata(canonical->as_path());
        if (metadata.is_err()) {
            return config_io_failure<lito::source::PackageSourceConfig>(
                "inspect config.patch path"_str,
                canonical->as_path(),
                rstd::move(metadata).unwrap_err());
        }
        if (! metadata->is_dir()) {
            return config_data_failure<lito::source::PackageSourceConfig>(
                path.with_field("path"_str), "must be a directory"_str);
        }
        patches.push(lito::source::GitSourcePatch {
            .git  = url.clone(),
            .path = rstd::move(canonical).unwrap_unchecked(),
        });
    }
    return Ok(lito::source::PackageSourceConfig {
        .patches          = rstd::move(patches),
        .builtin_packages = rstd_try(configured_builtin_sources(builtin, project_root)),
    });
}

auto decode_project_config(PathBuf               root,
                           const Toml&           document,
                           EnvironmentFlagPolicy environment_flags = EnvironmentFlagPolicy::Ignore,
                           Option<ProjectConfigDefaults> defaults  = None())
    -> ConfigResult<ProjectConfig> {
    auto wire               = rstd_try(decode_config_wire(document));
    auto tool_defaults      = lito::tools::ToolSpec {};
    auto toolchain_defaults = default_toolchain();
    if (defaults.is_some()) {
        tool_defaults      = rstd::move(defaults->tools);
        toolchain_defaults = rstd::move(defaults->toolchain);
    }
    auto toolchain = rstd_try(configured_toolchain(wire.toolchain, rstd::move(toolchain_defaults)));
    auto tools =
        rstd_try(configured_host_tools(wire.tools, root.as_path(), rstd::move(tool_defaults)));
    auto standard_library         = rstd_try(configured_standard_library(wire.toolchain));
    auto standard_library_runtime = rstd_try(configured_standard_library_runtime(wire.toolchain));
    auto environment   = rstd_try(configured_environment(wire.environment, root.as_path()));
    auto lock          = rstd_try(configured_lock(wire.lock, root.as_path()));
    auto sources       = rstd_try(configured_sources(wire.patch, wire.builtin, root.as_path()));
    auto install       = rstd_try(configured_install(wire.install, root.as_path()));
    auto doc           = rstd_try(configured_doc(wire.doc, root.as_path()));
    auto build_options = rstd_try(configured_build_options(wire.build));
    auto build_target  = rstd_try(configured_build_target(wire.build));
    rstd_try(append_environment_build_options(build_options, environment_flags));

    return Ok(ProjectConfig {
        .root                     = rstd::move(root),
        .lock                     = rstd::move(lock),
        .environment              = rstd::move(environment),
        .tools                    = rstd::move(tools.executables),
        .toolchain                = rstd::move(toolchain),
        .standard_library         = standard_library,
        .standard_library_runtime = standard_library_runtime,
        .build_options            = rstd::move(build_options),
        .build_target             = rstd::move(build_target),
        .sources                  = rstd::move(sources),
        .cargo                    = tools.cargo,
        .pkg_config               = rstd::move(tools.pkg_config),
        .cmake                    = rstd::move(tools.cmake),
        .cmake_build_overrides    = rstd::move(tools.cmake_build_overrides),
        .install                  = rstd::move(install),
        .doc                      = rstd::move(doc),
    });
}

auto decode_host_tool_command_config(PathBuf                       root,
                                     const Toml&                   document,
                                     Option<ProjectConfigDefaults> defaults = None())
    -> ConfigResult<HostToolCommandConfig> {
    auto wire               = rstd_try(decode_host_config_wire(document));
    auto tool_defaults      = lito::tools::ToolSpec {};
    auto toolchain_defaults = default_toolchain();
    if (defaults.is_some()) {
        tool_defaults      = rstd::move(defaults->tools);
        toolchain_defaults = rstd::move(defaults->toolchain);
    }
    auto environment = rstd_try(configured_environment(wire.environment, root.as_path()));
    auto tools =
        rstd_try(configured_host_tools(wire.tools, root.as_path(), rstd::move(tool_defaults)));
    auto toolchain = rstd_try(configured_toolchain(wire.toolchain, rstd::move(toolchain_defaults)));
    return Ok(HostToolCommandConfig {
        .root        = rstd::move(root),
        .environment = rstd::move(environment),
        .tools       = rstd::move(tools.executables),
        .toolchain   = rstd::move(toolchain),
    });
}
