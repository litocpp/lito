module;
#include <rstd/macro.hpp>

export module lito.config:schema;

import rstd;
import rstd.toml;
import lito.model;
import lito.toolchain;

using namespace rstd::prelude;
using namespace rstd::literals;
using Toml  = rstd::toml::Value;
using Table = rstd::toml::Table;

namespace lito
{

template<typename T>
auto config_failure(String message) -> Result<T> {
    return Err(Error::make(ErrorKind::Config, rstd::move(message)));
}

template<typename T>
auto config_failure(ref<str> message) -> Result<T> {
    return Err(Error::make(ErrorKind::Config, message));
}

auto config_member(const Toml& value, ref<str> key) -> Option<ref<Toml>> {
    return value.get(key);
}

auto config_table(const Toml& value, ref<str> context) -> Result<ref<Table>> {
    auto table = value.as_table();
    if (table.is_none()) {
        return config_failure<ref<Table>>(rstd::format("{} must be a table", context));
    }
    return Ok(*table);
}

auto root_config_key(ref<str> key) -> bool {
    return key == "environment"_str || key == "toolchain"_str || key == "pkg-config"_str ||
           key == "cmake"_str || key == "patch"_str;
}

auto environment_config_key(ref<str> key) -> bool {
    return key == "append-path"_str;
}

auto toolchain_config_key(ref<str> key) -> bool {
    return key == "compiler"_str || key == "c-compiler"_str || key == "archiver"_str ||
           key == "formatter"_str || key == "stripper"_str;
}

auto patch_config_key(ref<str> key) -> bool {
    return key == "path"_str;
}

auto pkg_config_key(ref<str> key) -> bool {
    return key == "executable"_str || key == "search-path"_str || key == "library-path"_str ||
           key == "sysroot"_str;
}

auto cmake_key(ref<str> key) -> bool {
    return key == "executable"_str || key == "generator"_str || key == "search-path"_str;
}

auto reject_config_unknown(const Table& table, ref<str> context, bool (*allowed)(ref<str>))
    -> Result<empty> {
    auto keys = table.keys();
    for (auto key = keys.next(); key.is_some(); key = keys.next()) {
        if (! allowed((**key).as_str())) {
            return config_failure<empty>(
                rstd::format("{} contains unknown field '{}'", context, (**key).as_str()));
        }
    }
    return Ok(empty {});
}

auto configured_tool(const Toml& toolchain_value, ref<str> key, ref<str> fallback, ref<str> context)
    -> Result<PathBuf> {
    auto value = config_member(toolchain_value, key);
    if (value.is_none()) return Ok(PathBuf::from(fallback));
    auto text = (**value).as_str();
    if (text.is_none()) {
        return config_failure<PathBuf>(rstd::format("{}.{} must be a string", context, key));
    }
    if (text->is_empty()) {
        return config_failure<PathBuf>(rstd::format("{}.{} must not be empty", context, key));
    }
    auto path = PathBuf::from(*text);
    if (! path.as_path().is_absolute() &&
        ! toolchain::command::is_searchable_tool_name(path.as_path())) {
        return config_failure<PathBuf>(
            rstd::format("{}.{} must be an executable name or absolute path", context, key));
    }
    return Ok(rstd::move(path));
}

auto configured_directories(const Toml&           table,
                            ref<str>              key,
                            ref<str>              context,
                            ref<rstd::path::Path> project_root) -> Result<Vec<PathBuf>> {
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
            return config_failure<Vec<PathBuf>>(rstd::format("cannot resolve {}.{} path '{}': {}",
                                                             context,
                                                             key,
                                                             path.as_path(),
                                                             rstd::move(canonical).unwrap_err()));
        }
        auto metadata = rstd::fs::metadata(canonical->as_path());
        if (metadata.is_err()) {
            return config_failure<Vec<PathBuf>>(rstd::format("cannot inspect {}.{} path '{}': {}",
                                                             context,
                                                             key,
                                                             canonical->as_path(),
                                                             rstd::move(metadata).unwrap_err()));
        }
        if (! metadata->is_dir()) {
            return config_failure<Vec<PathBuf>>(rstd::format(
                "{}.{} path '{}' is not a directory", context, key, canonical->as_path()));
        }
        result.push(rstd::move(canonical).unwrap());
    }
    return Ok(rstd::move(result));
}

auto configured_pkg_config(const Toml& document, ref<rstd::path::Path> project_root)
    -> Result<PkgConfigProviderConfig> {
    auto result = PkgConfigProviderConfig {
        .executable = PathBuf::from("pkg-config"_str),
    };
    auto value = config_member(document, "pkg-config"_str);
    if (value.is_none()) return Ok(rstd::move(result));
    auto table = config_table(**value, "config.pkg-config"_str);
    if (table.is_err()) return Err(rstd::move(table).unwrap_err());
    auto known = reject_config_unknown(**table, "config.pkg-config"_str, pkg_config_key);
    if (known.is_err()) return Err(rstd::move(known).unwrap_err());
    result.executable = rstd_try(
        configured_tool(**value, "executable"_str, "pkg-config"_str, "config.pkg-config"_str));
    result.search_paths = rstd_try(
        configured_directories(**value, "search-path"_str, "config.pkg-config"_str, project_root));
    result.library_paths = rstd_try(
        configured_directories(**value, "library-path"_str, "config.pkg-config"_str, project_root));
    auto sysroot = config_member(**value, "sysroot"_str);
    if (sysroot.is_some()) {
        auto text = (**sysroot).as_str();
        if (text.is_none() || text->is_empty()) {
            return config_failure<PkgConfigProviderConfig>(
                "config.pkg-config.sysroot must be a non-empty string"_str);
        }
        auto path = PathBuf::from(*text);
        if (path.as_path().is_relative()) path = PathBuf::from(project_root).join(path.as_path());
        auto canonical = rstd::fs::canonicalize(path.as_path());
        if (canonical.is_err()) {
            return config_failure<PkgConfigProviderConfig>(
                rstd::format("cannot resolve config.pkg-config.sysroot '{}': {}",
                             path.as_path(),
                             rstd::move(canonical).unwrap_err()));
        }
        result.sysroot = Some(rstd::move(canonical).unwrap());
    }
    result.target_configured = config_member(**value, "executable"_str).is_some() ||
                               ! result.library_paths.is_empty() || result.sysroot.is_some();
    return Ok(rstd::move(result));
}

auto configured_environment(const Toml& document, ref<rstd::path::Path> project_root)
    -> Result<ProcessEnvironmentSpec> {
    auto value = config_member(document, "environment"_str);
    if (value.is_none()) return Ok(ProcessEnvironmentSpec {});
    auto table = config_table(**value, "config.environment"_str);
    if (table.is_err()) return Err(rstd::move(table).unwrap_err());
    auto known = reject_config_unknown(**table, "config.environment"_str, environment_config_key);
    if (known.is_err()) return Err(rstd::move(known).unwrap_err());
    auto append_path =
        configured_directories(**value, "append-path"_str, "config.environment"_str, project_root);
    if (append_path.is_err()) return Err(rstd::move(append_path).unwrap_err());
    return Ok(ProcessEnvironmentSpec {
        .append_path = rstd::move(append_path).unwrap(),
    });
}

auto configured_cmake(const Toml& document, ref<rstd::path::Path> project_root)
    -> Result<CMakeProviderConfig> {
    auto result = CMakeProviderConfig {
        .executable = PathBuf::from("cmake"_str),
        .generator  = String::make("Ninja"_str),
    };
    auto value = config_member(document, "cmake"_str);
    if (value.is_none()) return Ok(rstd::move(result));
    auto table = config_table(**value, "config.cmake"_str);
    if (table.is_err()) return Err(rstd::move(table).unwrap_err());
    auto known = reject_config_unknown(**table, "config.cmake"_str, cmake_key);
    if (known.is_err()) return Err(rstd::move(known).unwrap_err());
    result.executable =
        rstd_try(configured_tool(**value, "executable"_str, "cmake"_str, "config.cmake"_str));
    auto generator = config_member(**value, "generator"_str);
    if (generator.is_some()) {
        auto text = (**generator).as_str();
        if (text.is_none() || text->is_empty()) {
            return config_failure<CMakeProviderConfig>(
                "config.cmake.generator must be a non-empty string"_str);
        }
        result.generator = String::make(*text);
    }
    result.search_paths = rstd_try(
        configured_directories(**value, "search-path"_str, "config.cmake"_str, project_root));
    return Ok(rstd::move(result));
}

auto default_toolchain() -> ToolchainSpec {
    return ToolchainSpec {
        .compiler   = PathBuf::from("clang++"_str),
        .c_compiler = PathBuf::from("clang"_str),
        .linker     = PathBuf::from("ld.lld"_str),
        .archiver   = PathBuf::from("llvm-ar"_str),
        .formatter  = PathBuf::from("clang-format"_str),
        .stripper   = PathBuf::from("llvm-strip"_str),
    };
}

auto configured_sources(const Toml& document, ref<rstd::path::Path> project_root)
    -> Result<PackageSourceConfig> {
    auto patches     = Vec<GitSourcePatch>::make();
    auto patch_value = config_member(document, "patch"_str);
    if (patch_value.is_none()) {
        return Ok(PackageSourceConfig { .patches = rstd::move(patches) });
    }

    auto patch_table = config_table(**patch_value, "config.patch"_str);
    if (patch_table.is_err()) return Err(rstd::move(patch_table).unwrap_err());
    auto keys = (**patch_table).keys();
    for (auto key = keys.next(); key.is_some(); key = keys.next()) {
        const auto& url = **key;
        if (url.is_empty()) {
            return config_failure<PackageSourceConfig>(
                "config.patch Git URL must not be empty"_str);
        }
        if (url.as_str().starts_with("-"_str)) {
            return config_failure<PackageSourceConfig>(
                rstd::format("config.patch Git URL '{}' must not start with '-'", url.as_str()));
        }
        if (url.as_str().contains("#"_str)) {
            return config_failure<PackageSourceConfig>(rstd::format(
                "config.patch Git URL '{}' must not contain a URL fragment", url.as_str()));
        }

        auto specification = (**patch_table).get(url.as_str());
        auto context       = rstd::format("config.patch.'{}'", url.as_str());
        auto table         = config_table(**specification, context.as_str());
        if (table.is_err()) return Err(rstd::move(table).unwrap_err());
        auto known = reject_config_unknown(**table, context.as_str(), patch_config_key);
        if (known.is_err()) return Err(rstd::move(known).unwrap_err());
        auto value = config_member(**specification, "path"_str);
        if (value.is_none()) {
            return config_failure<PackageSourceConfig>(
                rstd::format("{} is missing 'path'", context.as_str()));
        }
        auto text = (**value).as_str();
        if (text.is_none()) {
            return config_failure<PackageSourceConfig>(
                rstd::format("{}.path must be a string", context.as_str()));
        }
        if (text->is_empty()) {
            return config_failure<PackageSourceConfig>(
                rstd::format("{}.path must not be empty", context.as_str()));
        }

        auto requested = PathBuf::from(*text);
        if (requested.as_path().is_relative()) {
            requested = PathBuf::from(project_root).join(requested.as_path());
        }
        auto canonical = rstd::fs::canonicalize(requested.as_path());
        if (canonical.is_err()) {
            return config_failure<PackageSourceConfig>(
                rstd::format("cannot resolve {}.path '{}': {}",
                             context.as_str(),
                             requested.as_path(),
                             rstd::move(canonical).unwrap_err()));
        }
        auto resolved = rstd::move(canonical).unwrap();
        auto metadata = rstd::fs::metadata(resolved.as_path());
        if (metadata.is_err()) {
            return config_failure<PackageSourceConfig>(
                rstd::format("cannot inspect {}.path '{}': {}",
                             context.as_str(),
                             resolved.as_path(),
                             rstd::move(metadata).unwrap_err()));
        }
        if (! metadata->is_dir()) {
            return config_failure<PackageSourceConfig>(rstd::format(
                "{}.path '{}' is not a directory", context.as_str(), resolved.as_path()));
        }
        patches.push(GitSourcePatch {
            .git  = url.clone(),
            .path = rstd::move(resolved),
        });
    }
    return Ok(PackageSourceConfig { .patches = rstd::move(patches) });
}

} // namespace lito

export namespace lito
{

auto load_project_config(ref<rstd::path::Path> requested_root) -> Result<ProjectConfig> {
    auto canonical = rstd::fs::canonicalize(requested_root);
    if (canonical.is_err()) {
        return config_failure<ProjectConfig>(rstd::format("cannot resolve project root '{}': {}",
                                                          requested_root,
                                                          rstd::move(canonical).unwrap_err()));
    }
    auto root     = rstd::move(canonical).unwrap();
    auto metadata = rstd::fs::metadata(root.as_path());
    if (metadata.is_err()) {
        return config_failure<ProjectConfig>(rstd::format("cannot inspect project root '{}': {}",
                                                          root.as_path(),
                                                          rstd::move(metadata).unwrap_err()));
    }
    if (! metadata->is_dir()) {
        return config_failure<ProjectConfig>(
            rstd::format("project root '{}' is not a directory", root.as_path()));
    }

    auto config_path = root.join(PathBuf::from(".lito/config.toml"_str).as_path());
    auto exists      = rstd::fs::exists(config_path.as_path());
    if (exists.is_err()) {
        return config_failure<ProjectConfig>(rstd::format("cannot inspect config '{}': {}",
                                                          config_path.as_path(),
                                                          rstd::move(exists).unwrap_err()));
    }
    if (! *exists) {
        return Ok(ProjectConfig {
            .root        = rstd::move(root),
            .environment = ProcessEnvironmentSpec {},
            .toolchain   = default_toolchain(),
            .pkg_config =
                PkgConfigProviderConfig {
                    .executable = PathBuf::from("pkg-config"_str),
                },
            .cmake =
                CMakeProviderConfig {
                    .executable = PathBuf::from("cmake"_str),
                    .generator  = String::make("Ninja"_str),
                },
        });
    }

    auto contents = rstd::fs::read_to_string(config_path.as_path());
    if (contents.is_err()) {
        return config_failure<ProjectConfig>(rstd::format("cannot read config '{}': {}",
                                                          config_path.as_path(),
                                                          rstd::move(contents).unwrap_err()));
    }
    auto parsed = rstd::toml::from_str(contents->as_str());
    if (parsed.is_err()) {
        return config_failure<ProjectConfig>(rstd::format("cannot parse config '{}': {}",
                                                          config_path.as_path(),
                                                          rstd::move(parsed).unwrap_err()));
    }
    auto document   = rstd::move(parsed).unwrap();
    auto root_table = config_table(document, "config root"_str);
    if (root_table.is_err()) return Err(rstd::move(root_table).unwrap_err());
    auto root_known = reject_config_unknown(**root_table, "config root"_str, root_config_key);
    if (root_known.is_err()) return Err(rstd::move(root_known).unwrap_err());

    auto toolchain       = default_toolchain();
    auto toolchain_value = config_member(document, "toolchain"_str);
    if (toolchain_value.is_some()) {
        auto table = config_table(**toolchain_value, "config.toolchain"_str);
        if (table.is_err()) return Err(rstd::move(table).unwrap_err());
        auto known = reject_config_unknown(**table, "config.toolchain"_str, toolchain_config_key);
        if (known.is_err()) return Err(rstd::move(known).unwrap_err());
        auto compiler = configured_tool(
            **toolchain_value, "compiler"_str, "clang++"_str, "config.toolchain"_str);
        auto c_compiler = configured_tool(
            **toolchain_value, "c-compiler"_str, "clang"_str, "config.toolchain"_str);
        auto archiver = configured_tool(
            **toolchain_value, "archiver"_str, "llvm-ar"_str, "config.toolchain"_str);
        auto formatter = configured_tool(
            **toolchain_value, "formatter"_str, "clang-format"_str, "config.toolchain"_str);
        auto stripper = configured_tool(
            **toolchain_value, "stripper"_str, "llvm-strip"_str, "config.toolchain"_str);
        if (compiler.is_err()) return Err(rstd::move(compiler).unwrap_err());
        if (c_compiler.is_err()) return Err(rstd::move(c_compiler).unwrap_err());
        if (archiver.is_err()) return Err(rstd::move(archiver).unwrap_err());
        if (formatter.is_err()) return Err(rstd::move(formatter).unwrap_err());
        if (stripper.is_err()) return Err(rstd::move(stripper).unwrap_err());
        toolchain = ToolchainSpec {
            .compiler   = rstd::move(compiler).unwrap(),
            .c_compiler = rstd::move(c_compiler).unwrap(),
            .linker     = PathBuf::from("ld.lld"_str),
            .archiver   = rstd::move(archiver).unwrap(),
            .formatter  = rstd::move(formatter).unwrap(),
            .stripper   = rstd::move(stripper).unwrap(),
        };
    }

    auto environment = configured_environment(document, root.as_path());
    if (environment.is_err()) return Err(rstd::move(environment).unwrap_err());
    auto sources = configured_sources(document, root.as_path());
    if (sources.is_err()) return Err(rstd::move(sources).unwrap_err());
    auto pkg_config = configured_pkg_config(document, root.as_path());
    if (pkg_config.is_err()) return Err(rstd::move(pkg_config).unwrap_err());
    auto cmake = configured_cmake(document, root.as_path());
    if (cmake.is_err()) return Err(rstd::move(cmake).unwrap_err());

    return Ok(ProjectConfig {
        .root        = rstd::move(root),
        .environment = rstd::move(environment).unwrap(),
        .toolchain   = rstd::move(toolchain),
        .sources     = rstd::move(sources).unwrap(),
        .pkg_config  = rstd::move(pkg_config).unwrap(),
        .cmake       = rstd::move(cmake).unwrap(),
    });
}

} // namespace lito
