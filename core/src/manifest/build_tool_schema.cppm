module;
#include <rstd/macro.hpp>

module lito.core:manifest.build_tool_schema;

import rstd;
import rstd.serde;
import rstd.toml;
import :manifest.build_tool;
import :manifest.convention;
import :manifest.error;
import :manifest.primitives;
import :manifest.wire;
import :package.identity;
import :parse.value;
import lito.system;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito::manifest;
using namespace lito::system;
using PathBuf = rstd::path::PathBuf;
using Toml    = rstd::toml::Value;

auto exact_build_tool_version(ref<str> value) noexcept -> bool {
    if (value.is_empty() || value.trim_ascii() != value || value == "latest"_str) return false;
    for (const auto character : value) {
        switch (character.to_primitive()) {
        case '<':
        case '>':
        case '=':
        case '~':
        case '^':
        case '*':
        case '|':
        case ',': return false;
        default: break;
        }
    }
    return true;
}

auto parse_host_key(ref<str> value, rstd::serde::DataPath path) -> ManifestSchemaResult<HostInfo> {
    auto separator = Option<usize> {};
    for (usize index {}; index < value.len(); ++index) {
        if (value[index] == u8('-')) {
            separator = Some(index);
            break;
        }
    }
    if (separator.is_none() || *separator == usize {} || *separator + usize(1) >= value.len()) {
        return manifest_data_failure<HostInfo>(rstd::move(path),
                                               "host must use '<os>-<architecture>'"_str);
    }
    auto os   = value.get(usize {}, *separator).unwrap_unchecked();
    auto arch = value.get(*separator + usize(1), value.len()).unwrap_unchecked();
    for (auto character : os) {
        const auto ascii = character.to_primitive();
        if (! ((ascii >= 'a' && ascii <= 'z') || (ascii >= '0' && ascii <= '9'))) {
            return manifest_data_failure<HostInfo>(rstd::move(path),
                                                   "host contains an invalid OS"_str);
        }
    }
    auto architecture = require_architecture(arch);
    if (architecture.is_err()) {
        return manifest_data_failure<HostInfo>(rstd::move(path),
                                               "host architecture is unsupported"_str);
    }
    return Ok(HostInfo {
        .architecture = rstd::move(architecture).unwrap_unchecked(),
        .os           = String::make(os),
    });
}

auto parse_build_tools(Option<ref<Toml>> value) -> ManifestSchemaResult<Vec<BuildToolRequirement>> {
    auto result = Vec<BuildToolRequirement>::make();
    if (value.is_none()) return Ok(rstd::move(result));
    auto root = rstd::serde::DataPath().with_field("build-tools"_str);
    auto tools =
        rstd_try(decode_manifest_value<lito::manifest::wire::BuildTools>(**value, root.clone()));
    for (auto alias_ref : tools.keys()) {
        const auto& alias      = *alias_ref;
        auto        alias_path = root.with_map_key(alias.as_str());
        if (! package_name_is_valid(alias.as_str())) {
            return manifest_data_failure<Vec<BuildToolRequirement>>(rstd::move(alias_path),
                                                                    "invalid tool alias"_str);
        }
        const auto tool = tools.get(alias.as_str()).unwrap_unchecked();
        if (! exact_build_tool_version(tool->version.as_str())) {
            return manifest_data_failure<Vec<BuildToolRequirement>>(
                alias_path.with_field("version"_str), "must be an exact non-empty version"_str);
        }
        auto executable = PathBuf::from(tool->executable.as_str());
        if (executable.is_empty() || ! executable.as_path().is_safe_relative()) {
            return manifest_data_failure<Vec<BuildToolRequirement>>(
                alias_path.with_field("executable"_str),
                "must be a safe non-empty relative path"_str);
        }
        if (tool->archives.is_empty()) {
            return manifest_data_failure<Vec<BuildToolRequirement>>(
                alias_path.with_field("archives"_str), "must not be empty"_str);
        }
        auto archives = Vec<BuildToolArchiveManifest>::with_capacity(tool->archives.len());
        for (auto host_ref : tool->archives.keys()) {
            const auto& host   = *host_ref;
            auto archive_path  = alias_path.with_field("archives"_str).with_map_key(host.as_str());
            const auto archive = tool->archives.get(host.as_str()).unwrap_unchecked();
            auto       url     = lito::parse::HttpsUrl::parse(archive->url.as_str());
            if (url.is_err()) {
                return manifest_data_failure<Vec<BuildToolRequirement>>(
                    archive_path.with_field("url"_str),
                    "invalid HTTPS URL"_str,
                    rstd::move(url).unwrap_err_unchecked());
            }
            auto digest = lito::parse::parse_sha256(archive->sha256.as_str(),
                                                    lito::parse::Sha256TextMode::Flexible);
            if (digest.is_err()) {
                return manifest_data_failure<Vec<BuildToolRequirement>>(
                    archive_path.with_field("sha256"_str),
                    "invalid SHA-256 digest"_str,
                    rstd::move(digest).unwrap_err_unchecked());
            }
            auto parsed_host = rstd_try(parse_host_key(host.as_str(), archive_path.clone()));
            for (const auto& existing : archives) {
                if (existing.host.os == parsed_host.os.as_str() &&
                    existing.host.architecture == parsed_host.architecture) {
                    return manifest_data_failure<Vec<BuildToolRequirement>>(
                        rstd::move(archive_path), "canonical host is repeated"_str);
                }
            }
            archives.push(BuildToolArchiveManifest {
                .host   = rstd::move(parsed_host),
                .url    = rstd::move(url).unwrap_unchecked(),
                .sha256 = rstd::move(digest).unwrap_unchecked(),
            });
        }
        result.push(BuildToolRequirement {
            .alias      = alias.clone(),
            .version    = tool->version.clone(),
            .executable = rstd::move(executable),
            .archives   = rstd::move(archives),
        });
    }
    return Ok(rstd::move(result));
}
