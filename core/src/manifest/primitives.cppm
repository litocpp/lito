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
using Toml = rstd::toml::Value;
using namespace lito::manifest;

template<typename T>
auto decode_manifest_value(const Toml& value, rstd::serde::DataPath path)
    -> ManifestSchemaResult<T> {
    auto decoded = rstd::toml::decode_value<T>(value, rstd::move(path));
    if (decoded.is_err()) {
        return Err(ManifestSchemaError::Data(rstd::move(decoded).unwrap_err_unchecked()));
    }
    return Ok(rstd::move(decoded).unwrap_unchecked());
}

auto canonical_existing(ref<rstd::path::Path> path, ref<str> context)
    -> ManifestSchemaResult<PathBuf> {
    auto canonical = rstd::fs::canonicalize(path);
    if (canonical.is_err()) {
        return Err(ManifestSchemaError::Io(context.into(),
                                           "resolve"_Str,
                                           PathBuf::from(path),
                                           rstd::move(canonical).unwrap_err()));
    }
    return Ok(rstd::move(canonical).unwrap());
}

auto parse_archive_url(ref<str> value, ref<str> context)
    -> ManifestSchemaResult<lito::parse::FetchUrl> {
    auto parsed = lito::parse::FetchUrl::parse(value);
    if (parsed.is_err()) {
        return Err(ManifestSchemaError::Domain(
            rstd::format("{}.archive is invalid: {}", context, rstd::move(parsed).unwrap_err())));
    }
    return Ok(rstd::move(parsed).unwrap());
}

auto parse_manifest_sha256(ref<str> value, ref<str> context)
    -> ManifestSchemaResult<licrypto::Sha256Digest> {
    auto parsed = lito::parse::parse_sha256(value, lito::parse::Sha256TextMode::Flexible);
    if (parsed.is_err()) {
        return Err(ManifestSchemaError::Domain(
            rstd::format("{}.sha256 is invalid: {}", context, rstd::move(parsed).unwrap_err())));
    }
    return Ok(rstd::move(parsed).unwrap());
}
