module;
#include <rstd/macro.hpp>

export module lito.manifest:primitives;

import rstd;
import rstd.toml;
import lito.error;

using namespace rstd::prelude;
using namespace rstd::literals;
using Toml         = rstd::toml::Value;
using Table        = rstd::toml::Table;
using KeyPredicate = bool (*)(ref<str>);

namespace lito
{

template<typename T>
auto failure(ref<str> message) -> Result<T> {
    return Err(Error::make(ErrorKind::Manifest, message));
}

template<typename T>
auto failure(String message) -> Result<T> {
    return Err(Error::make(ErrorKind::Manifest, rstd::move(message)));
}

auto member(const Toml& value, ref<str> key) -> Option<ref<Toml>> {
    return value.get(key);
}

auto canonical_existing(ref<rstd::path::Path> path, ref<str> context) -> Result<PathBuf> {
    auto canonical = rstd::fs::canonicalize(path);
    if (canonical.is_err()) {
        return failure<PathBuf>(
            rstd::format("{} '{}': {}", context, path, rstd::move(canonical).unwrap_err()));
    }
    return Ok(rstd::move(canonical).unwrap());
}

auto table_value(const Toml& value, ref<str> context) -> Result<ref<Table>> {
    auto table = value.as_table();
    if (table.is_none()) {
        return failure<ref<Table>>(rstd::format("{} must be a table", context));
    }
    return Ok(*table);
}

auto required_table(const Toml& document, ref<str> key, ref<str> context) -> Result<ref<Table>> {
    auto value = member(document, key);
    if (value.is_none()) {
        return failure<ref<Table>>(rstd::format("{} is missing '{}'", context, key));
    }
    return table_value(**value, rstd::format("{}.{}", context, key).as_str());
}

auto string_value(const Toml& value, ref<str> context) -> Result<String> {
    auto text = value.as_str();
    if (text.is_none()) return failure<String>(rstd::format("{} must be a string", context));
    return Ok(String::make(*text));
}

auto required_string(const Toml& table, ref<str> key, ref<str> context) -> Result<String> {
    auto value = member(table, key);
    if (value.is_none()) {
        return failure<String>(rstd::format("{} is missing '{}'", context, key));
    }
    return string_value(**value, rstd::format("{}.{}", context, key).as_str());
}

auto optional_string(const Toml& table, ref<str> key, ref<str> context) -> Result<Option<String>> {
    auto value = member(table, key);
    if (value.is_none()) return Ok(Option<String> {});
    auto parsed = string_value(**value, rstd::format("{}.{}", context, key).as_str());
    if (parsed.is_err()) return Err(rstd::move(parsed).unwrap_err());
    return Ok(Some(rstd::move(parsed).unwrap()));
}

auto string_array(Option<ref<Toml>> value, ref<str> context) -> Result<Vec<String>> {
    auto result = Vec<String>::make();
    if (value.is_none()) return Ok(rstd::move(result));
    auto array = (**value).as_array();
    if (array.is_none()) return failure<Vec<String>>(rstd::format("{} must be an array", context));
    for (const auto& item : **array) {
        auto text = string_value(item, rstd::format("{} item", context).as_str());
        if (text.is_err()) return Err(rstd::move(text).unwrap_err());
        result.push(rstd::move(text).unwrap());
    }
    return Ok(rstd::move(result));
}

auto reject_unknown(const Table& table, ref<str> context, KeyPredicate allowed) -> Result<empty> {
    auto keys = table.keys();
    for (auto key = keys.next(); key.is_some(); key = keys.next()) {
        if (! allowed((**key).as_str())) {
            return failure<empty>(
                rstd::format("{} contains unknown field '{}'", context, (**key).as_str()));
        }
    }
    return Ok(empty {});
}

auto package_root_key(ref<str> key) -> bool {
    return key == "package"_str || key == "lib"_str || key == "bin"_str || key == "test"_str ||
           key == "bench"_str || key == "compile-test"_str || key == "usage"_str ||
           key == "dependencies"_str || key == "dev-dependencies"_str ||
           key == "external-dependencies"_str || key == "profile"_str;
}

auto workspace_root_key(ref<str> key) -> bool {
    return key == "workspace"_str || key == "profile"_str;
}

} // namespace lito
