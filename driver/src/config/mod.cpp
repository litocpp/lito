module;
#include <rstd/macro.hpp>

module lito.driver;

import rstd;
import rstd.toml;
import lito.core;
import lito.tools;
import :config;
import :config.schema;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;
using namespace rstd::literals;
using Toml  = rstd::toml::Value;
using Table = rstd::toml::Table;
using namespace lito::config;

struct ConfigLocation {
    PathBuf root;
    PathBuf directory;
    PathBuf shared_path;
    PathBuf path;
    PathBuf lock;
};

struct ConfigDocument {
    ConfigLocation location;
    Toml           value;
};

struct ConfigMutationSession {
    rstd::fs::FileLock lock;
    ConfigDocument     document;
};

template<typename T>
auto document_failure(String message) -> ConfigResult<T> {
    return Err(ConfigError::Schema(rstd::move(message)));
}

template<typename T>
auto document_failure(ref<str> message) -> ConfigResult<T> {
    return Err(ConfigError::Schema(String::make(message)));
}

template<typename T>
auto document_io_failure(ref<str>               operation,
                         ref<rstd::path::Path>  path,
                         rstd::io::error::Error source) -> ConfigResult<T> {
    return Err(ConfigError::Io(String::make(operation), PathBuf::from(path), rstd::move(source)));
}

auto is_not_found(const rstd::io::error::Error& error) noexcept -> bool {
    return error.kind() == rstd::io::error::ErrorKind { rstd::io::error::ErrorKind::NotFound };
}

auto resolve_config_location(ref<rstd::path::Path> requested_root) -> ConfigResult<ConfigLocation> {
    auto canonical = rstd::fs::canonicalize(requested_root);
    if (canonical.is_err()) {
        return document_io_failure<ConfigLocation>(
            "resolve project root"_str, requested_root, rstd::move(canonical).unwrap_err());
    }
    auto root     = rstd::move(canonical).unwrap();
    auto metadata = rstd::fs::metadata(root.as_path());
    if (metadata.is_err()) {
        return document_io_failure<ConfigLocation>(
            "inspect project root"_str, root.as_path(), rstd::move(metadata).unwrap_err());
    }
    if (! metadata->is_dir()) {
        return document_failure<ConfigLocation>(
            rstd::format("project root '{}' is not a directory", root.as_path()));
    }
    auto directory   = root.join(PathBuf::from(".lito"_str).as_path());
    auto shared_path = root.join(PathBuf::from("lito-config.toml"_str).as_path());
    return Ok(ConfigLocation {
        .root        = rstd::move(root),
        .directory   = directory.clone(),
        .shared_path = rstd::move(shared_path),
        .path        = directory.join(PathBuf::from("config.toml"_str).as_path()),
        .lock        = directory.join(PathBuf::from("config.toml.lock"_str).as_path()),
    });
}

auto empty_document(ConfigLocation location) -> ConfigDocument {
    return ConfigDocument {
        .location = rstd::move(location),
        .value    = Toml::Table(Table::make()),
    };
}

auto inspect_ordinary_file(ref<rstd::path::Path> path, ref<str> description) -> ConfigResult<bool> {
    auto metadata = rstd::fs::symlink_metadata(path);
    if (metadata.is_err()) {
        auto error = rstd::move(metadata).unwrap_err();
        if (is_not_found(error)) return Ok(false);
        return document_io_failure<bool>(
            rstd::format("inspect {}", description).as_str(), path, rstd::move(error));
    }
    if (! metadata->is_file()) {
        return document_failure<bool>(
            rstd::format("{} '{}' must be an ordinary file", description, path));
    }
    return Ok(true);
}

auto read_config_value(ref<rstd::path::Path> path, ref<str> description, bool require_ordinary_file)
    -> ConfigResult<Option<Toml>> {
    bool exists {};
    if (require_ordinary_file) {
        exists = rstd_try(inspect_ordinary_file(path, description));
    } else {
        auto result = rstd::fs::exists(path);
        if (result.is_err()) {
            return document_io_failure<Option<Toml>>(
                rstd::format("inspect {}", description).as_str(),
                path,
                rstd::move(result).unwrap_err());
        }
        exists = *result;
    }
    if (! exists) return Ok(Option<Toml> {});

    auto contents = rstd::fs::read_to_string(path);
    if (contents.is_err()) {
        return document_io_failure<Option<Toml>>(
            rstd::format("read {}", description).as_str(), path, rstd::move(contents).unwrap_err());
    }
    auto parsed = rstd::toml::from_str(contents->as_str());
    if (parsed.is_err()) {
        return Err(ConfigError::Parse(PathBuf::from(path), rstd::move(parsed).unwrap_err()));
    }
    if (! parsed->is_table()) {
        return document_failure<Option<Toml>>(rstd::format("{} root must be a table", description));
    }
    return Ok(Some(rstd::move(parsed).unwrap()));
}

auto read_config_document(ConfigLocation location, bool require_ordinary_file)
    -> ConfigResult<ConfigDocument> {
    auto value = rstd_try(
        read_config_value(location.path.as_path(), "configuration"_str, require_ordinary_file));
    auto document = empty_document(rstd::move(location));
    if (value.is_some()) document.value = rstd::move(value).unwrap();
    return Ok(rstd::move(document));
}

auto merge_config_value(Toml& destination, const Toml& source) -> void {
    if (! destination.is_table() || ! source.is_table()) {
        destination = source.clone();
        return;
    }
    auto source_table = source.as_table().unwrap();
    auto keys         = source_table->keys();
    for (auto key = keys.next(); key.is_some(); key = keys.next()) {
        auto source_value      = source_table->get((**key).as_str()).unwrap();
        auto destination_value = destination.get_mut((**key).as_str());
        if (destination_value.is_some() && (**destination_value).is_table() &&
            source_value->is_table()) {
            merge_config_value(**destination_value, *source_value);
            continue;
        }
        auto destination_table = destination.as_table_mut().unwrap();
        destination_table->insert((**key).clone(), source_value->clone());
    }
}

auto open_config_document(ref<rstd::path::Path> root, ConfigLoadMode mode)
    -> ConfigResult<ConfigDocument> {
    auto location = rstd_try(resolve_config_location(root));
    auto shared   = rstd_try(
        read_config_value(location.shared_path.as_path(), "project configuration"_str, false));
    if (shared.is_some()) {
        normalize_host_tool_provider_shorthand(*shared);
        auto tools = shared->get("tools"_str);
        if (tools.is_some()) {
            auto tools_table = (**tools).as_table();
            if (tools_table.is_some()) {
                auto cmake = (**tools_table).get("cmake"_str);
                if (cmake.is_some()) {
                    auto cmake_table = (**cmake).as_table();
                    if (cmake_table.is_some() && (**cmake_table).contains_key("overrides"_str)) {
                        return document_failure<ConfigDocument>(
                            rstd::format("project configuration '{}' cannot contain "
                                         "tools.cmake.overrides; use .lito/config.toml or --config",
                                         location.shared_path.as_path()));
                    }
                }
            }
        }
    }
    auto document = empty_document(rstd::move(location));
    if (shared.is_some()) document.value = rstd::move(shared).unwrap();
    if (mode == ConfigLoadMode::LocalDisabled) return Ok(rstd::move(document));
    auto local =
        rstd_try(read_config_value(document.location.path.as_path(), "configuration"_str, false));
    if (local.is_some()) {
        normalize_host_tool_provider_shorthand(*local);
        merge_config_value(document.value, *local);
    }
    return Ok(rstd::move(document));
}

auto get_config_value(const Toml& document, const rstd::toml::KeyPath& key) -> Option<ref<Toml>> {
    auto current = ref<Toml>::from_raw_parts(rstd::addressof(document));
    for (const auto& segment : key) {
        auto next = current->get(segment.as_str());
        if (next.is_none()) return None();
        current = *next;
    }
    return Some(current);
}

auto set_config_value(Toml& document, const rstd::toml::KeyPath& key, Toml value)
    -> ConfigResult<empty> {
    auto normalized = rstd::toml::to_key_string(key);
    auto current    = document.as_table_mut().unwrap();
    for (usize index {}; index + usize(1) < key.len(); ++index) {
        auto next = current->get_mut(key[index].as_str());
        if (next.is_none()) {
            current->insert(key[index].clone(), Toml::Table(Table::make()));
            next = current->get_mut(key[index].as_str());
        }
        if (! (**next).is_table()) {
            return Err(ConfigError::KeyConflict(rstd::move(normalized)));
        }
        current = (**next).as_table_mut().unwrap();
    }
    current->insert(key[key.len() - usize(1)].clone(), rstd::move(value));
    return Ok(empty {});
}

auto merge_config_value_at(Toml& document, const rstd::toml::KeyPath& key, Toml value)
    -> ConfigResult<empty> {
    auto normalized = rstd::toml::to_key_string(key);
    auto current    = document.as_table_mut().unwrap();
    for (usize index {}; index + usize(1) < key.len(); ++index) {
        auto next = current->get_mut(key[index].as_str());
        if (next.is_none()) {
            current->insert(key[index].clone(), Toml::Table(Table::make()));
            next = current->get_mut(key[index].as_str());
        }
        if (! (**next).is_table()) {
            return Err(ConfigError::KeyConflict(rstd::move(normalized)));
        }
        current = (**next).as_table_mut().unwrap();
    }
    auto existing = current->get_mut(key[key.len() - usize(1)].as_str());
    if (existing.is_none()) {
        current->insert(key[key.len() - usize(1)].clone(), rstd::move(value));
    } else {
        merge_config_value(**existing, value);
    }
    return Ok(empty {});
}

auto is_host_tool_provider_key(const rstd::toml::KeyPath& key) -> bool {
    if (key.len() != usize(2) || key[usize {}] != "tools"_str) return false;
    return key[usize(1)] == "cmake"_str || key[usize(1)] == "pkg-config"_str;
}

auto set_config_assignment_value(Toml& document, const rstd::toml::KeyPath& key, Toml value)
    -> ConfigResult<empty> {
    if (! is_host_tool_provider_key(key)) {
        return set_config_value(document, key, rstd::move(value));
    }
    if (value.as_str().is_some()) {
        auto executable = key.clone();
        executable.push(String::make("executable"_str));
        return set_config_value(document, executable, rstd::move(value));
    }
    if (value.is_table()) {
        return merge_config_value_at(document, key, rstd::move(value));
    }
    return set_config_value(document, key, rstd::move(value));
}

auto unset_config_value_at(Table&                     table,
                           const rstd::toml::KeyPath& key,
                           usize                      index,
                           ref<str>                   normalized) -> ConfigResult<empty> {
    const auto& segment = key[index];
    if (index + usize(1) == key.len()) {
        if (table.remove(segment.as_str()).is_none()) {
            return Err(ConfigError::MissingKey(String::make(normalized)));
        }
        return Ok(empty {});
    }
    auto child = table.get_mut(segment.as_str());
    if (child.is_none()) return Err(ConfigError::MissingKey(String::make(normalized)));
    if (! (**child).is_table()) {
        return Err(ConfigError::KeyConflict(String::make(normalized)));
    }
    auto child_table = (**child).as_table_mut().unwrap();
    rstd_try(unset_config_value_at(*child_table, key, index + usize(1), normalized));
    if (child_table->is_empty()) (void)table.remove(segment.as_str());
    return Ok(empty {});
}

auto unset_config_value(Toml& document, const rstd::toml::KeyPath& key) -> ConfigResult<empty> {
    auto normalized = rstd::toml::to_key_string(key);
    return unset_config_value_at(**document.as_table_mut(), key, usize {}, normalized.as_str());
}

auto parse_config_key(ref<str> text, ref<str> context) -> ConfigResult<rstd::toml::KeyPath> {
    auto key = rstd::toml::parse_key_path(text);
    if (key.is_err()) {
        return Err(ConfigError::Input(String::make(context), rstd::move(key).unwrap_err()));
    }
    return Ok(rstd::move(key).unwrap());
}

auto parse_config_value(ref<str> text) -> ConfigResult<Toml> {
    if (text.is_empty()) return document_failure<Toml>("configuration value must not be empty"_str);
    auto value = rstd::toml::parse_value(text);
    if (value.is_ok()) return Ok(rstd::move(value).unwrap());
    return Ok(Toml::String(String::make(text)));
}

auto apply_config_override(ConfigDocument& document, ref<str> text, usize index)
    -> ConfigResult<empty> {
    auto context = rstd::format("command-line config override #{}", index + usize(1));
    auto strict  = rstd::toml::parse_assignment(text);
    if (strict.is_ok()) {
        auto assignment = rstd::move(strict).unwrap();
        return set_config_assignment_value(
            document.value, assignment.key, rstd::move(assignment.value));
    }
    auto raw = rstd::toml::parse_assignment_text(text);
    if (raw.is_err()) {
        return Err(ConfigError::Input(rstd::move(context), rstd::move(raw).unwrap_err()));
    }
    if (raw->value.as_str().contains("\n"_str) || raw->value.as_str().contains("\r"_str)) {
        return Err(ConfigError::Input(rstd::move(context), rstd::move(strict).unwrap_err()));
    }
    auto assignment = rstd::move(raw).unwrap();
    return set_config_assignment_value(
        document.value, assignment.key, Toml::String(rstd::move(assignment.value)));
}

auto ensure_config_directory(const ConfigLocation& location) -> ConfigResult<empty> {
    auto metadata = rstd::fs::symlink_metadata(location.directory.as_path());
    if (metadata.is_ok()) {
        if (! metadata->is_dir()) {
            return document_failure<empty>(
                rstd::format("configuration directory '{}' must be an ordinary directory",
                             location.directory.as_path()));
        }
        return Ok(empty {});
    }
    auto error = rstd::move(metadata).unwrap_err();
    if (! is_not_found(error)) {
        return document_io_failure<empty>(
            "inspect configuration directory"_str, location.directory.as_path(), rstd::move(error));
    }
    auto created = rstd::fs::create_dir_all(location.directory.as_path());
    if (created.is_err()) {
        return document_io_failure<empty>("create configuration directory"_str,
                                          location.directory.as_path(),
                                          rstd::move(created).unwrap_err());
    }
    return Ok(empty {});
}

auto open_mutation_session(ref<rstd::path::Path> root) -> ConfigResult<ConfigMutationSession> {
    auto location = rstd_try(resolve_config_location(root));
    rstd_try(ensure_config_directory(location));
    (void)rstd_try(inspect_ordinary_file(location.lock.as_path(), "configuration lock"_str));
    auto opened = rstd::fs::OpenOptions::make().read(true).write(true).create(true).open(
        location.lock.as_path());
    if (opened.is_err()) {
        return document_io_failure<ConfigMutationSession>("open configuration lock"_str,
                                                          location.lock.as_path(),
                                                          rstd::move(opened).unwrap_err());
    }
    auto opened_metadata = opened->metadata();
    if (opened_metadata.is_err()) {
        return document_io_failure<ConfigMutationSession>("inspect opened configuration lock"_str,
                                                          location.lock.as_path(),
                                                          rstd::move(opened_metadata).unwrap_err());
    }
    if (! opened_metadata->is_file()) {
        return document_failure<ConfigMutationSession>(rstd::format(
            "configuration lock '{}' must be an ordinary file", location.lock.as_path()));
    }
    auto locked =
        rstd::fs::FileLock::acquire(rstd::move(opened).unwrap(), rstd::fs::FileLockMode::Exclusive);
    if (locked.is_err()) {
        return document_io_failure<ConfigMutationSession>(
            "lock configuration"_str, location.lock.as_path(), rstd::move(locked).unwrap_err());
    }
    auto document = read_config_document(rstd::move(location), true);
    if (document.is_err()) return Err(rstd::move(document).unwrap_err());
    auto normalized = rstd::move(document).unwrap();
    normalize_host_tool_provider_shorthand(normalized.value);
    return Ok(ConfigMutationSession {
        .lock     = rstd::move(locked).unwrap(),
        .document = rstd::move(normalized),
    });
}

auto write_config_document(const ConfigDocument& document) -> ConfigResult<empty> {
    auto serialized = rstd::toml::to_string(document.value);
    if (serialized.is_err()) {
        return Err(ConfigError::Serialize(document.location.path.clone(),
                                          rstd::move(serialized).unwrap_err()));
    }
    auto written =
        rstd::fs::write_atomic(document.location.path.as_path(), serialized->as_str().as_bytes());
    if (written.is_err()) {
        return document_io_failure<empty>("write configuration"_str,
                                          document.location.path.as_path(),
                                          rstd::move(written).unwrap_err());
    }
    return Ok(empty {});
}

auto lito::config::project_config_path(ref<rstd::path::Path> root) -> ConfigResult<PathBuf> {
    auto location = rstd_try(resolve_config_location(root));
    return Ok(rstd::move(location.path));
}

auto lito::config::load_project_config(ref<rstd::path::Path> root, ProjectConfigRequest request)
    -> ConfigResult<ProjectConfig> {
    auto document = rstd_try(open_config_document(root, request.mode));
    for (usize index {}; index < request.overrides.len(); ++index) {
        rstd_try(apply_config_override(document, request.overrides[index].as_str(), index));
    }
    return decode_project_config(document.location.root.clone(),
                                 document.value,
                                 request.environment_flags,
                                 rstd::move(request.defaults));
}

auto lito::config::load_project_config(ref<rstd::path::Path> root, ConfigLoadMode mode)
    -> ConfigResult<ProjectConfig> {
    return load_project_config(root,
                               ProjectConfigRequest {
                                   .mode = mode,
                               });
}

auto lito::config::load_host_tool_command_config(ref<rstd::path::Path> root,
                                                 ProjectConfigRequest  request)
    -> ConfigResult<HostToolCommandConfig> {
    auto document = rstd_try(open_config_document(root, request.mode));
    for (usize index {}; index < request.overrides.len(); ++index) {
        rstd_try(apply_config_override(document, request.overrides[index].as_str(), index));
    }
    return decode_host_tool_command_config(
        document.location.root.clone(), document.value, rstd::move(request.defaults));
}

auto lito::config::get_persisted_config(ref<rstd::path::Path> root, Option<String> key)
    -> ConfigResult<ConfigQuery> {
    auto location = rstd_try(resolve_config_location(root));
    auto document = rstd_try(read_config_document(rstd::move(location), true));
    auto output   = String::make();
    if (key.is_none()) {
        auto serialized = rstd::toml::to_string(document.value);
        if (serialized.is_err()) {
            return Err(ConfigError::Serialize(document.location.path.clone(),
                                              rstd::move(serialized).unwrap_err()));
        }
        output = rstd::move(serialized).unwrap();
    } else {
        auto parsed = rstd_try(parse_config_key(key->as_str(), "configuration key"_str));
        auto value  = get_config_value(document.value, parsed);
        if (value.is_none()) {
            return Err(ConfigError::MissingKey(rstd::toml::to_key_string(parsed)));
        }
        auto serialized = rstd::toml::to_value_string(**value);
        if (serialized.is_err()) {
            return Err(ConfigError::Serialize(document.location.path.clone(),
                                              rstd::move(serialized).unwrap_err()));
        }
        output = rstd::move(serialized).unwrap();
        output.push_ascii(u8('\n'));
    }
    return Ok(ConfigQuery {
        .path   = document.location.path.clone(),
        .output = rstd::move(output),
    });
}

auto lito::config::set_persisted_config(ref<rstd::path::Path> root, ref<str> key, ref<str> value)
    -> ConfigResult<ConfigMutation> {
    auto parsed_key   = rstd_try(parse_config_key(key, "configuration key"_str));
    auto parsed_value = rstd_try(parse_config_value(value));
    auto normalized   = rstd::toml::to_key_string(parsed_key);
    auto session      = rstd_try(open_mutation_session(root));
    rstd_try(
        set_config_assignment_value(session.document.value, parsed_key, rstd::move(parsed_value)));
    (void)rstd_try(
        decode_project_config(session.document.location.root.clone(), session.document.value));
    rstd_try(write_config_document(session.document));
    return Ok(ConfigMutation {
        .path = session.document.location.path.clone(),
        .key  = rstd::move(normalized),
    });
}

auto lito::config::unset_persisted_config(ref<rstd::path::Path> root, ref<str> key)
    -> ConfigResult<ConfigMutation> {
    auto parsed_key = rstd_try(parse_config_key(key, "configuration key"_str));
    auto normalized = rstd::toml::to_key_string(parsed_key);
    auto session    = rstd_try(open_mutation_session(root));
    rstd_try(unset_config_value(session.document.value, parsed_key));
    (void)rstd_try(
        decode_project_config(session.document.location.root.clone(), session.document.value));
    rstd_try(write_config_document(session.document));
    return Ok(ConfigMutation {
        .path = session.document.location.path.clone(),
        .key  = rstd::move(normalized),
    });
}
