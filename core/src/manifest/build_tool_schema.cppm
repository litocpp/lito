module;
#include <rstd/macro.hpp>

export module lito.manifest:build_tool_schema;

import rstd;
import rstd.toml;
import lito.error;
import lito.manifest.contract;
import lito.package.identity;
import lito.platform;
import :primitives;
import :key_schema;
import :convention;

using namespace rstd::prelude;
using namespace rstd::literals;
using Toml = rstd::toml::Value;

namespace lito
{

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

auto parse_host_key(ref<str> value, ref<str> context) -> ManifestSchemaResult<HostInfo> {
    auto separator = Option<usize> {};
    for (usize index {}; index < value.len(); ++index) {
        if (value[index] == u8('-')) {
            separator = Some(index);
            break;
        }
    }
    if (separator.is_none() || *separator == usize {} || *separator + usize(1) >= value.len()) {
        return failure<HostInfo>(
            rstd::format("{} must use '<os>-<architecture>'", context));
    }
    auto os   = value.get(usize {}, *separator).unwrap();
    auto arch = value.get(*separator + usize(1), value.len()).unwrap();
    for (auto character : os) {
        const auto ascii = character.to_primitive();
        if (! ((ascii >= 'a' && ascii <= 'z') || (ascii >= '0' && ascii <= '9'))) {
            return failure<HostInfo>(rstd::format("{} contains an invalid OS", context));
        }
    }
    auto architecture = canonical_architecture(arch);
    if (architecture.is_err()) {
        return failure<HostInfo>(rstd::format("{} contains unsupported architecture '{}'",
                                              context,
                                              arch));
    }
    return Ok(HostInfo {
        .architecture = rstd::move(architecture).unwrap(),
        .os           = String::make(os),
    });
}

auto parse_build_tools(Option<ref<Toml>> value)
    -> ManifestSchemaResult<Vec<BuildToolRequirement>> {
    auto result = Vec<BuildToolRequirement>::make();
    if (value.is_none()) return Ok(rstd::move(result));
    auto tools = rstd_try(table_value(**value, "manifest.build-tools"_str));
    auto aliases = tools->keys();
    for (auto alias = aliases.next(); alias.is_some(); alias = aliases.next()) {
        const auto context = rstd::format("manifest.build-tools.{}", **alias);
        if (! package_name_is_valid((**alias).as_str())) {
            return failure<Vec<BuildToolRequirement>>(
                rstd::format("{} is not a valid tool alias", context));
        }
        auto value = tools->get((**alias).as_str());
        auto table = rstd_try(table_value(**value, context.as_str()));
        rstd_try(reject_unknown(*table, context.as_str(), build_tool_key));
        auto version = rstd_try(required_string(**value, "version"_str, context.as_str()));
        auto executable =
            rstd_try(required_string(**value, "executable"_str, context.as_str()));
        if (! exact_build_tool_version(version.as_str())) {
            return failure<Vec<BuildToolRequirement>>(
                rstd::format("{}.version must be an exact non-empty version", context));
        }
        auto executable_path = PathBuf::from(rstd::move(executable));
        if (executable_path.is_empty() || ! executable_path.as_path().is_safe_relative()) {
            return failure<Vec<BuildToolRequirement>>(
                rstd::format("{}.executable must be a safe non-empty relative path", context));
        }
        auto archives_value = member(**value, "archives"_str);
        if (archives_value.is_none()) {
            return failure<Vec<BuildToolRequirement>>(
                rstd::format("{} is missing 'archives'", context));
        }
        auto archives_table = rstd_try(
            table_value(**archives_value, rstd::format("{}.archives", context).as_str()));
        auto archives = Vec<BuildToolArchiveManifest>::make();
        auto hosts    = archives_table->keys();
        for (auto host = hosts.next(); host.is_some(); host = hosts.next()) {
            const auto archive_context = rstd::format("{}.archives.{}", context, **host);
            auto       archive_value   = archives_table->get((**host).as_str());
            auto archive_table =
                rstd_try(table_value(**archive_value, archive_context.as_str()));
            rstd_try(reject_unknown(
                *archive_table, archive_context.as_str(), build_tool_archive_key));
            auto url =
                rstd_try(required_string(**archive_value, "url"_str, archive_context.as_str()));
            auto sha256 = rstd_try(
                required_string(**archive_value, "sha256"_str, archive_context.as_str()));
            if (! url.as_str().starts_with("https://"_str) ||
                ! archive_url_is_valid(url.as_str())) {
                return failure<Vec<BuildToolRequirement>>(
                    rstd::format("{}.url must be an HTTPS archive URL", archive_context));
            }
            if (! sha256_is_valid(sha256.as_str())) {
                return failure<Vec<BuildToolRequirement>>(
                    rstd::format("{}.sha256 must be a 64-character hexadecimal digest",
                                 archive_context));
            }
            auto parsed_host = rstd_try(parse_host_key((**host).as_str(), archive_context.as_str()));
            for (const auto& existing : archives) {
                if (existing.host.os == parsed_host.os.as_str() &&
                    existing.host.architecture == parsed_host.architecture) {
                    return failure<Vec<BuildToolRequirement>>(rstd::format(
                        "{}.archives repeats canonical host '{}-{}'",
                        context,
                        parsed_host.os.as_str(),
                        parsed_host.architecture.as_str()));
                }
            }
            archives.push(BuildToolArchiveManifest {
                .host   = rstd::move(parsed_host),
                .url    = rstd::move(url),
                .sha256 = rstd::move(sha256),
            });
        }
        if (archives.is_empty()) {
            return failure<Vec<BuildToolRequirement>>(
                rstd::format("{}.archives must not be empty", context));
        }
        result.push(BuildToolRequirement {
            .alias      = (**alias).clone(),
            .version    = rstd::move(version),
            .executable = rstd::move(executable_path),
            .archives   = rstd::move(archives),
        });
    }
    return Ok(rstd::move(result));
}

} // namespace lito
