module;
#include <rstd/macro.hpp>

export module lito.manifest:primitives;

import rstd;
import rstd.toml;
import lito.error;
import lito.manifest.contract;

using namespace rstd::prelude;
using namespace rstd::literals;
using Toml         = rstd::toml::Value;
using Table        = rstd::toml::Table;
using KeyPredicate = bool (*)(ref<str>);

namespace lito
{

template<typename T>
auto failure(ref<str> message) -> ManifestSchemaResult<T> {
    return Err(ManifestSchemaError::InvalidValue(
        ManifestNodePath::make("manifest"_str), String::make(message)));
}

template<typename T>
auto failure(String message) -> ManifestSchemaResult<T> {
    return Err(ManifestSchemaError::InvalidValue(
        ManifestNodePath::make("manifest"_str), rstd::move(message)));
}

template<typename T>
auto manifest_io_failure(ref<str>                node,
                         ref<str>                operation,
                         ref<rstd::path::Path>   path,
                         rstd::io::error::Error error) -> ManifestSchemaResult<T> {
    return Err(ManifestSchemaError::Io(ManifestNodePath::make(node),
                                       String::make(operation),
                                       PathBuf::from(path),
                                       rstd::move(error)));
}

auto member(const Toml& value, ref<str> key) -> Option<ref<Toml>> {
    return value.get(key);
}

auto canonical_existing(ref<rstd::path::Path> path, ref<str> context)
    -> ManifestSchemaResult<PathBuf> {
    auto canonical = rstd::fs::canonicalize(path);
    if (canonical.is_err()) {
        return Err(ManifestSchemaError::Io(ManifestNodePath::make(context),
                                           String::make("resolve"_str),
                                           PathBuf::from(path),
                                           rstd::move(canonical).unwrap_err()));
    }
    return Ok(rstd::move(canonical).unwrap());
}

auto table_value(const Toml& value, ref<str> context) -> ManifestSchemaResult<ref<Table>> {
    auto table = value.as_table();
    if (table.is_none()) {
        return Err(ManifestSchemaError::WrongType(
            ManifestNodePath::make(context), String::make("a table"_str)));
    }
    return Ok(*table);
}

auto required_table(const Toml& document, ref<str> key, ref<str> context)
    -> ManifestSchemaResult<ref<Table>> {
    auto value = member(document, key);
    if (value.is_none()) {
        return Err(ManifestSchemaError::MissingField(
            ManifestNodePath::make(context), String::make(key)));
    }
    return table_value(**value, rstd::format("{}.{}", context, key).as_str());
}

auto string_value(const Toml& value, ref<str> context) -> ManifestSchemaResult<String> {
    auto text = value.as_str();
    if (text.is_none()) {
        return Err(ManifestSchemaError::WrongType(
            ManifestNodePath::make(context), String::make("a string"_str)));
    }
    return Ok(String::make(*text));
}

auto required_string(const Toml& table, ref<str> key, ref<str> context)
    -> ManifestSchemaResult<String> {
    auto value = member(table, key);
    if (value.is_none()) {
        return Err(ManifestSchemaError::MissingField(
            ManifestNodePath::make(context), String::make(key)));
    }
    return string_value(**value, rstd::format("{}.{}", context, key).as_str());
}

auto optional_string(const Toml& table, ref<str> key, ref<str> context)
    -> ManifestSchemaResult<Option<String>> {
    auto value = member(table, key);
    if (value.is_none()) return Ok(Option<String> {});
    auto parsed = string_value(**value, rstd::format("{}.{}", context, key).as_str());
    if (parsed.is_err()) return Err(rstd::move(parsed).unwrap_err());
    return Ok(Some(rstd::move(parsed).unwrap()));
}

auto string_array(Option<ref<Toml>> value, ref<str> context)
    -> ManifestSchemaResult<Vec<String>> {
    auto result = Vec<String>::make();
    if (value.is_none()) return Ok(rstd::move(result));
    auto array = (**value).as_array();
    if (array.is_none()) {
        return Err(ManifestSchemaError::WrongType(
            ManifestNodePath::make(context), String::make("an array"_str)));
    }
    for (const auto& item : **array) {
        auto text = string_value(item, rstd::format("{} item", context).as_str());
        if (text.is_err()) return Err(rstd::move(text).unwrap_err());
        result.push(rstd::move(text).unwrap());
    }
    return Ok(rstd::move(result));
}

auto reject_unknown(const Table& table, ref<str> context, KeyPredicate allowed)
    -> ManifestSchemaResult<empty> {
    auto keys = table.keys();
    for (auto key = keys.next(); key.is_some(); key = keys.next()) {
        if (! allowed((**key).as_str())) {
            return Err(ManifestSchemaError::UnknownField(
                ManifestNodePath::make(context), String::make((**key).as_str())));
        }
    }
    return Ok(empty {});
}

auto package_root_key(ref<str> key) -> bool {
    return key == "package"_str || key == "lib"_str || key == "bin"_str || key == "test"_str ||
           key == "bench"_str || key == "compile-test"_str || key == "usage"_str ||
           key == "dependencies"_str || key == "dev-dependencies"_str ||
           key == "runtime-dependencies"_str || key == "external-dependencies"_str ||
           key == "build-tools"_str || key == "profile"_str;
}

auto archive_url_is_valid(ref<str> value) -> bool {
    return ! value.is_empty() && ! value.starts_with("-"_str) && ! value.contains("#"_str) &&
           ! value.contains("\""_str) && ! value.contains("\\"_str) &&
           ! value.contains(";"_str) && ! value.contains("\n"_str) &&
           ! value.contains("\r"_str);
}

auto sha256_is_valid(ref<str> value) -> bool {
    if (value.len() != usize(64)) return false;
    for (const auto character : value) {
        const auto ascii = character.to_primitive();
        if (! ((ascii >= '0' && ascii <= '9') || (ascii >= 'a' && ascii <= 'f') ||
               (ascii >= 'A' && ascii <= 'F'))) {
            return false;
        }
    }
    return true;
}

auto workspace_root_key(ref<str> key) -> bool {
    return key == "workspace"_str || key == "profile"_str;
}

} // namespace lito
