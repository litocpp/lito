module;
#include <rstd/macro.hpp>

export module lito.tools.cmake:snapshot;

import rstd;
import licrypto;
import rstd.json;
import lito.tools;
import :model;
import :file_api;

using namespace rstd::prelude;
using namespace rstd::literals;
using Json      = rstd::json::Value;
using JsonArray = rstd::json::Array;
using JsonMap   = rstd::json::Map;

export namespace lito::tools::cmake
{

auto usage_snapshot_path(const CMakeWorkArea& area) -> PathBuf {
    return area.query_root.join(PathBuf::from("usage-snapshot-v4.json"_str).as_path());
}

auto json_strings(const Vec<String>& values) -> Json {
    auto result = JsonArray::with_capacity(values.len());
    for (const auto& value : values) result.push(Json::String(value.clone()));
    return Json::Array(rstd::move(result));
}

auto snapshot_json(const CMakeTargetUsageSnapshot& snapshot) -> Json {
    auto result = JsonMap::make();
    result.insert(String::make("compile"_str), json_strings(snapshot.compile));
    result.insert(String::make("link"_str), json_strings(snapshot.link));
    return Json::Object(rstd::move(result));
}

auto write_usage_snapshot(const CMakeWorkArea& area, const CMakeUsageSnapshot& snapshot)
    -> lito::tools::ToolResult<empty> {
    auto targets = JsonArray::with_capacity(snapshot.targets.len());
    for (const auto& target : snapshot.targets) targets.push(snapshot_json(target));
    auto document = JsonMap::make();
    document.insert(String::make("schema"_str),
                    Json::String(String::make("lito-cmake-usage-v4"_str)));
    document.insert(String::make("version"_str), Json::String(snapshot.version.clone()));
    document.insert(String::make("targets"_str), Json::Array(rstd::move(targets)));
    document.insert(String::make("combined"_str), snapshot_json(snapshot.combined));
    auto host_tools = JsonArray::with_capacity(snapshot.host_tools.len());
    for (const auto& tool : snapshot.host_tools) {
        auto item = JsonMap::make();
        item.insert(String::make("digest"_str), Json::String(tool.digest.clone()));
        item.insert(String::make("name"_str), Json::String(tool.name.clone()));
        item.insert(String::make("path"_str),
                    Json::String(tool.executable.as_path().to_string_lossy()));
        item.insert(String::make("target"_str), Json::String(tool.target.clone()));
        host_tools.push(Json::Object(rstd::move(item)));
    }
    document.insert(String::make("host-tools"_str), Json::Array(rstd::move(host_tools)));
    auto assets = JsonArray::with_capacity(snapshot.assets.len());
    for (const auto& set : snapshot.assets) {
        auto entries = JsonArray::with_capacity(set.entries.len());
        for (const auto& entry : set.entries) {
            auto item = JsonMap::make();
            item.insert(String::make("path"_str),
                        Json::String(entry.logical_path.as_path().to_string_lossy()));
            item.insert(String::make("source"_str),
                        Json::String(entry.source.as_path().to_string_lossy()));
            entries.push(Json::Object(rstd::move(item)));
        }
        auto item = JsonMap::make();
        item.insert(String::make("name"_str), Json::String(set.name.clone()));
        item.insert(String::make("disposition"_str),
                    Json::String(String::make(set.disposition == ExternalAssetDisposition::Provided
                                                  ? "provided"_str
                                                  : "materialized"_str)));
        item.insert(String::make("entries"_str), Json::Array(rstd::move(entries)));
        assets.push(Json::Object(rstd::move(item)));
    }
    document.insert(String::make("assets"_str), Json::Array(rstd::move(assets)));
    auto text =
        rstd::json::to_string(Json::Object(rstd::move(document)),
                              rstd::json::FormatOptions { .pretty = true, .indent = usize(2) });
    text.push('\n');
    auto path    = usage_snapshot_path(area);
    auto written = rstd::fs::write_atomic(path.as_path(), text.as_str().as_bytes());
    if (written.is_err()) {
        return cmake_io_failure<empty>(
            "write CMake usage snapshot"_str, path.as_path(), rstd::move(written).unwrap_err());
    }
    return Ok(empty {});
}

auto parse_snapshot_strings(const Json& value, ref<str> key, ref<str> context)
    -> lito::tools::ToolResult<Vec<String>> {
    auto array = required_json_array(value, key, context);
    if (array.is_err()) return Err(rstd::move(array).unwrap_err());
    auto result = Vec<String>::with_capacity((**array).len());
    for (const auto& item : **array) {
        auto text = item.as_str();
        if (text.is_none()) {
            return cmake_failure<Vec<String>>(
                rstd::format("{}.{} contains a non-string value", context, key));
        }
        result.push(String::make(*text));
    }
    return Ok(rstd::move(result));
}

auto parse_usage_target(const Json& value, ref<str> context)
    -> lito::tools::ToolResult<CMakeTargetUsageSnapshot> {
    auto compile = parse_snapshot_strings(value, "compile"_str, context);
    if (compile.is_err()) return Err(rstd::move(compile).unwrap_err());
    auto link = parse_snapshot_strings(value, "link"_str, context);
    if (link.is_err()) return Err(rstd::move(link).unwrap_err());
    return Ok(CMakeTargetUsageSnapshot {
        .compile = rstd::move(compile).unwrap(),
        .link    = rstd::move(link).unwrap(),
    });
}

auto materialize_link_tokens(const Vec<String>& tokens, ref<rstd::path::Path> query_build)
    -> lito::tools::ToolResult<Vec<String>> {
    auto result = Vec<String>::with_capacity(tokens.len());
    auto root   = PathBuf::from(query_build);
    for (const auto& token : tokens) {
        auto path = PathBuf::from(token.as_str());
        if (token.as_str().starts_with("-"_str) || path.as_path().is_absolute()) {
            result.push(token.clone());
            continue;
        }
        auto candidate = root.join(path.as_path());
        auto exists    = rstd::fs::exists(candidate.as_path());
        if (exists.is_err()) {
            return cmake_io_failure<Vec<String>>("inspect CMake link input"_str,
                                                 candidate.as_path(),
                                                 rstd::move(exists).unwrap_err());
        }
        if (! *exists) {
            result.push(token.clone());
            continue;
        }
        auto canonical = rstd::fs::canonicalize(candidate.as_path());
        if (canonical.is_err()) {
            return cmake_io_failure<Vec<String>>("resolve CMake link input"_str,
                                                 candidate.as_path(),
                                                 rstd::move(canonical).unwrap_err());
        }
        auto text = path_text(canonical->as_path(), "CMake link input"_str);
        if (text.is_err()) return Err(rstd::move(text).unwrap_err());
        result.push(rstd::move(text).unwrap());
    }
    return Ok(rstd::move(result));
}

auto read_usage_snapshot(const CMakeWorkArea& area, const Request& requirement)
    -> lito::tools::ToolResult<Option<CMakeUsageSnapshot>> {
    auto path   = usage_snapshot_path(area);
    auto exists = rstd::fs::exists(path.as_path());
    if (exists.is_err()) {
        return cmake_io_failure<Option<CMakeUsageSnapshot>>(
            "inspect CMake usage snapshot"_str, path.as_path(), rstd::move(exists).unwrap_err());
    }
    if (! *exists) return Ok(None());
    auto value = read_json(path.as_path(), "CMake usage snapshot"_str);
    if (value.is_err()) return Err(rstd::move(value).unwrap_err());
    auto schema = required_json_string(*value, "schema"_str, "CMake usage snapshot"_str);
    if (schema.is_err()) return Err(rstd::move(schema).unwrap_err());
    if (*schema != "lito-cmake-usage-v4"_str) {
        return cmake_failure<Option<CMakeUsageSnapshot>>(rstd::format(
            "CMake usage snapshot '{}' has unsupported schema '{}'", path.as_path(), *schema));
    }
    auto version = required_json_string(*value, "version"_str, "CMake usage snapshot"_str);
    if (version.is_err()) return Err(rstd::move(version).unwrap_err());
    auto targets = required_json_array(*value, "targets"_str, "CMake usage snapshot"_str);
    if (targets.is_err()) return Err(rstd::move(targets).unwrap_err());
    if ((**targets).len() != requirement.targets.len()) {
        return cmake_failure<Option<CMakeUsageSnapshot>>(
            rstd::format("CMake usage snapshot '{}' has {} targets, expected {}",
                         path.as_path(),
                         (**targets).len(),
                         requirement.targets.len()));
    }
    auto parsed_targets = Vec<CMakeTargetUsageSnapshot>::with_capacity((**targets).len());
    for (usize index {}; index < (**targets).len(); ++index) {
        auto parsed = parse_usage_target((**targets)[index], "CMake usage target"_str);
        if (parsed.is_err()) return Err(rstd::move(parsed).unwrap_err());
        parsed_targets.push(rstd::move(parsed).unwrap());
    }
    auto combined = required_json_member(*value, "combined"_str, "CMake usage snapshot"_str);
    if (combined.is_err()) return Err(rstd::move(combined).unwrap_err());
    auto parsed_combined = parse_usage_target(**combined, "CMake combined usage"_str);
    if (parsed_combined.is_err()) return Err(rstd::move(parsed_combined).unwrap_err());
    auto tool_values = required_json_array(*value, "host-tools"_str, "CMake usage snapshot"_str);
    if (tool_values.is_err()) return Err(rstd::move(tool_values).unwrap_err());
    if ((**tool_values).len() != requirement.host_tools.len()) return Ok(None());
    auto host_tools = Vec<CMakeHostToolSnapshot>::make();
    for (usize index {}; index < (**tool_values).len(); ++index) {
        const auto& item = (**tool_values)[index];
        auto        name = required_json_string(item, "name"_str, "CMake host tool snapshot"_str);
        auto target      = required_json_string(item, "target"_str, "CMake host tool snapshot"_str);
        auto path        = required_json_string(item, "path"_str, "CMake host tool snapshot"_str);
        auto digest      = required_json_string(item, "digest"_str, "CMake host tool snapshot"_str);
        if (name.is_err()) return Err(rstd::move(name).unwrap_err());
        if (target.is_err()) return Err(rstd::move(target).unwrap_err());
        if (path.is_err()) return Err(rstd::move(path).unwrap_err());
        if (digest.is_err()) return Err(rstd::move(digest).unwrap_err());
        const auto& expected = requirement.host_tools[index];
        if (*name != expected.name.as_str() || *target != expected.target.as_str())
            return Ok(None());
        auto executable = PathBuf::from(*path);
        auto bytes      = rstd::fs::read(executable.as_path());
        if (bytes.is_err() || licrypto::sha256_hex(bytes->as_slice()) != *digest) {
            return Ok(None());
        }
        host_tools.push(CMakeHostToolSnapshot {
            .name       = String::make(*name),
            .target     = String::make(*target),
            .executable = rstd::move(executable),
            .digest     = String::make(*digest),
        });
    }
    auto asset_values = required_json_array(*value, "assets"_str, "CMake usage snapshot"_str);
    if (asset_values.is_err()) return Err(rstd::move(asset_values).unwrap_err());
    auto assets = Vec<ExternalAssetSet>::make();
    for (const auto& item : **asset_values) {
        auto name        = required_json_string(item, "name"_str, "CMake asset set"_str);
        auto disposition = required_json_string(item, "disposition"_str, "CMake asset set"_str);
        if (name.is_err()) return Err(rstd::move(name).unwrap_err());
        if (disposition.is_err()) return Err(rstd::move(disposition).unwrap_err());
        auto parsed_disposition = ExternalAssetDisposition::Materialized;
        if (*disposition == "provided"_str) {
            parsed_disposition = ExternalAssetDisposition::Provided;
        } else if (*disposition != "materialized"_str) {
            return cmake_failure<Option<CMakeUsageSnapshot>>(rstd::format(
                "CMake asset set '{}' has unknown disposition '{}'", *name, *disposition));
        }
        auto entries = required_json_array(item, "entries"_str, "CMake asset set"_str);
        if (entries.is_err()) return Err(rstd::move(entries).unwrap_err());
        auto parsed_entries = Vec<ExternalAssetEntry>::make();
        for (const auto& entry : **entries) {
            auto logical = required_json_string(entry, "path"_str, "CMake asset entry"_str);
            auto source  = required_json_string(entry, "source"_str, "CMake asset entry"_str);
            if (logical.is_err()) return Err(rstd::move(logical).unwrap_err());
            if (source.is_err()) return Err(rstd::move(source).unwrap_err());
            parsed_entries.push(ExternalAssetEntry {
                .logical_path = PathBuf::from(*logical),
                .source       = PathBuf::from(*source),
            });
        }
        assets.push(ExternalAssetSet {
            .name        = String::make(*name),
            .disposition = parsed_disposition,
            .entries     = rstd::move(parsed_entries),
        });
    }
    auto validated_assets = validate_asset_snapshot(area, requirement, rstd::move(assets));
    if (validated_assets.is_err()) return Err(rstd::move(validated_assets).unwrap_err());
    return Ok(Some(CMakeUsageSnapshot {
        .version    = String::make(*version),
        .targets    = rstd::move(parsed_targets),
        .host_tools = rstd::move(host_tools),
        .combined   = rstd::move(parsed_combined).unwrap(),
        .assets     = rstd::move(validated_assets).unwrap(),
    }));
}

auto target_snapshot_identity(const Provider&                 provider,
                              const Request&                  requirement,
                              ref<str>                        target,
                              ref<str>                        version,
                              const CMakeTargetUsageSnapshot& snapshot,
                              ref<str> effective_target) -> lito::tools::ToolResult<String> {
    auto executable = path_text(provider.executable.as_path(), "CMake executable"_str);
    if (executable.is_err()) return Err(rstd::move(executable).unwrap_err());
    auto result = String::make("lito-cmake-dependency-v1\n"_str);
    append_identity(result, executable->as_str());
    append_identity(result, provider.identity.as_str());
    append_identity(result, provider.generator.as_str());
    rstd_try(append_search_path_identity(result, provider));
    append_identity(result, requirement.package.as_str());
    for (const auto& component : requirement.components) {
        append_identity(result, component.as_str());
    }
    append_identity(result, target);
    append_identity(result, version);
    append_identity(result, effective_target);
    append_identity(result, source_identity(requirement).as_str());
    for (const auto& token : snapshot.compile) append_identity(result, token.as_str());
    return Ok(rstd::move(result));
}

auto dependency_identity(const Provider&           provider,
                         const Request&            requirement,
                         ref<str>                  version,
                         const CMakeUsageSnapshot& snapshots,
                         ref<str> effective_target) -> lito::tools::ToolResult<String> {
    auto executable = path_text(provider.executable.as_path(), "CMake executable"_str);
    if (executable.is_err()) return Err(rstd::move(executable).unwrap_err());
    auto result = String::make("lito-cmake-declaration-v1\n"_str);
    append_identity(result, executable->as_str());
    append_identity(result, provider.identity.as_str());
    append_identity(result, provider.generator.as_str());
    rstd_try(append_search_path_identity(result, provider));
    append_identity(result, requirement.package.as_str());
    for (const auto& component : requirement.components) {
        append_identity(result, component.as_str());
    }
    append_identity(result, version);
    append_identity(result, effective_target);
    append_identity(result, source_identity(requirement).as_str());
    for (usize index {}; index < requirement.targets.len(); ++index) {
        append_identity(result, requirement.targets[index].name.as_str());
        for (const auto& token : snapshots.targets[index].compile) {
            append_identity(result, token.as_str());
        }
    }
    for (const auto& token : snapshots.combined.link) append_identity(result, token.as_str());
    for (const auto& tool : snapshots.host_tools) {
        append_identity(result, tool.name.as_str());
        append_identity(result, tool.target.as_str());
        append_identity(result, tool.executable.as_path().to_string_lossy().as_str());
        append_identity(result, tool.digest.as_str());
    }
    return Ok(rstd::move(result));
}

} // namespace lito::tools::cmake
