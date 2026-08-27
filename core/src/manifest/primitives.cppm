module;
#include <rstd/macro.hpp>

module lito.core:manifest.primitives;

import rstd;
import licrypto;
import rstd.serde;
import rstd.toml;
import :manifest.error;
import :parse;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;
using namespace rstd::literals;
using Toml         = rstd::toml::Value;
using Table        = rstd::toml::Table;
using KeyPredicate = bool (*)(ref<str>);
using namespace lito::manifest;

template<typename T>
auto manifest_schema_failure(ref<str> message) -> ManifestSchemaResult<T> {
    return Err(ManifestSchemaError::Domain(String::make(message)));
}

template<typename T>
auto manifest_schema_failure(String message) -> ManifestSchemaResult<T> {
    return Err(ManifestSchemaError::Domain(rstd::move(message)));
}

template<typename T>
auto manifest_data_failure(rstd::serde::DataPath path, ref<str> message)
    -> ManifestSchemaResult<T> {
    return Err(
        ManifestSchemaError::Data(rstd::serde::Error::invalid_value(rstd::move(path), message)));
}

template<typename T, typename Source>
    requires Impled<rstd::mtp::rm_cvf<Source>, rstd::error::Error>
auto manifest_data_failure(rstd::serde::DataPath path, ref<str> message, Source source)
    -> ManifestSchemaResult<T> {
    return Err(ManifestSchemaError::Data(rstd::serde::Error::invalid_value_with_source(
        rstd::move(path), message, rstd::move(source))));
}

template<typename T>
auto decode_manifest_value(const Toml& value, rstd::serde::DataPath path)
    -> ManifestSchemaResult<T> {
    auto decoded = rstd::toml::decode_value<T>(value, rstd::move(path));
    if (decoded.is_err()) {
        return Err(ManifestSchemaError::Data(rstd::move(decoded).unwrap_err_unchecked()));
    }
    return Ok(rstd::move(decoded).unwrap_unchecked());
}

template<typename T>
auto manifest_io_failure(ref<str>               node,
                         ref<str>               operation,
                         ref<rstd::path::Path>  path,
                         rstd::io::error::Error error) -> ManifestSchemaResult<T> {
    return Err(ManifestSchemaError::Io(
        String::make(node), String::make(operation), PathBuf::from(path), rstd::move(error)));
}

auto member(const Toml& value, ref<str> key) -> Option<ref<Toml>> {
    return value.get(key);
}

auto canonical_existing(ref<rstd::path::Path> path, ref<str> context)
    -> ManifestSchemaResult<PathBuf> {
    auto canonical = rstd::fs::canonicalize(path);
    if (canonical.is_err()) {
        return Err(ManifestSchemaError::Io(String::make(context),
                                           String::make("resolve"_str),
                                           PathBuf::from(path),
                                           rstd::move(canonical).unwrap_err()));
    }
    return Ok(rstd::move(canonical).unwrap());
}

auto table_value(const Toml& value, ref<str> context) -> ManifestSchemaResult<ref<Table>> {
    return Ok(rstd_try(lito::parse::toml::table(value, lito::parse::NodePath::root(context))));
}

auto required_table(const Toml& document, ref<str> key, ref<str> context)
    -> ManifestSchemaResult<ref<Table>> {
    auto path  = lito::parse::NodePath::root(context);
    auto value = rstd_try(lito::parse::toml::required_member(document, key, path));
    return Ok(rstd_try(lito::parse::toml::table(*value, path.field(key))));
}

auto string_value(const Toml& value, ref<str> context) -> ManifestSchemaResult<String> {
    return Ok(String::make(
        rstd_try(lito::parse::toml::string(value, lito::parse::NodePath::root(context)))));
}

auto required_string(const Toml& table, ref<str> key, ref<str> context)
    -> ManifestSchemaResult<String> {
    auto path  = lito::parse::NodePath::root(context);
    auto value = rstd_try(lito::parse::toml::required_member(table, key, path));
    return Ok(String::make(rstd_try(lito::parse::toml::string(*value, path.field(key)))));
}

auto optional_string(const Toml& table, ref<str> key, ref<str> context)
    -> ManifestSchemaResult<Option<String>> {
    auto value = member(table, key);
    if (value.is_none()) return Ok(Option<String> {});
    auto parsed = string_value(**value, rstd::format("{}.{}", context, key).as_str());
    if (parsed.is_err()) return Err(rstd::move(parsed).unwrap_err());
    return Ok(Some(rstd::move(parsed).unwrap()));
}

auto string_array(Option<ref<Toml>> value, ref<str> context) -> ManifestSchemaResult<Vec<String>> {
    auto result = Vec<String>::make();
    if (value.is_none()) return Ok(rstd::move(result));
    auto path  = lito::parse::NodePath::root(context);
    auto array = rstd_try(lito::parse::toml::array(**value, path));
    for (auto index = usize {}; index < array->len(); ++index) {
        auto text = rstd_try(lito::parse::toml::string((*array)[index], path.index(index)));
        result.push(String::make(text));
    }
    return Ok(rstd::move(result));
}

auto reject_unknown(const Table& table, ref<str> context, KeyPredicate allowed)
    -> ManifestSchemaResult<empty> {
    rstd_try(
        lito::parse::toml::reject_unknown(table, lito::parse::NodePath::root(context), allowed));
    return Ok(empty {});
}

auto package_root_key(ref<str> key) -> bool {
    return key == "package"_str || key == "lib"_str || key == "bin"_str || key == "test"_str ||
           key == "bench"_str || key == "compile-test"_str || key == "usage"_str ||
           key == "dependencies"_str || key == "dev-dependencies"_str ||
           key == "runtime-dependencies"_str || key == "external-dependencies"_str ||
           key == "external-sources"_str || key == "source-groups"_str ||
           key == "build-tools"_str || key == "script"_str || key == "profile"_str ||
           key == "when"_str || key == "features"_str;
}

auto parse_archive_url(ref<str> value, ref<str> context)
    -> ManifestSchemaResult<lito::parse::FetchUrl> {
    auto parsed = lito::parse::FetchUrl::parse(value);
    if (parsed.is_err()) {
        return manifest_schema_failure<lito::parse::FetchUrl>(
            rstd::format("{}.archive is invalid: {}", context, rstd::move(parsed).unwrap_err()));
    }
    return Ok(rstd::move(parsed).unwrap());
}

auto parse_manifest_sha256(ref<str> value, ref<str> context)
    -> ManifestSchemaResult<licrypto::Sha256Digest> {
    auto parsed = lito::parse::parse_sha256(value, lito::parse::Sha256TextMode::Flexible);
    if (parsed.is_err()) {
        return manifest_schema_failure<licrypto::Sha256Digest>(
            rstd::format("{}.sha256 is invalid: {}", context, rstd::move(parsed).unwrap_err()));
    }
    return Ok(rstd::move(parsed).unwrap());
}

auto workspace_root_key(ref<str> key) -> bool {
    return key == "workspace"_str || key == "profile"_str;
}
