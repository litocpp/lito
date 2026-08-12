module;
#include <rstd/macro.hpp>

export module lito.dependency.cmake.snapshot;

import rstd;
import rstd.json;
import lito.error;
import lito.dependency.contract;
import lito.cpp;
import lito.system.storage;
import lito.dependency.cmake.model;
import lito.dependency.cmake.file_api;

using namespace rstd::prelude;
using namespace rstd::literals;
using Json      = rstd::json::Value;
using JsonArray = rstd::json::Array;
using JsonMap   = rstd::json::Map;

export namespace lito
{

auto usage_snapshot_path(const CMakeWorkArea& area) -> PathBuf {
    return area.query_root.join(PathBuf::from("usage-snapshot-v1.json"_str).as_path());
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
    -> Result<empty> {
    auto targets = JsonArray::with_capacity(snapshot.targets.len());
    for (const auto& target : snapshot.targets) targets.push(snapshot_json(target));
    auto document = JsonMap::make();
    document.insert(String::make("schema"_str),
                    Json::String(String::make("lito-cmake-usage-v1"_str)));
    document.insert(String::make("version"_str), Json::String(snapshot.version.clone()));
    document.insert(String::make("targets"_str), Json::Array(rstd::move(targets)));
    document.insert(String::make("combined"_str), snapshot_json(snapshot.combined));
    auto text =
        rstd::json::to_string(Json::Object(rstd::move(document)),
                              rstd::json::FormatOptions { .pretty = true, .indent = usize(2) });
    text.push('\n');
    auto path    = usage_snapshot_path(area);
    auto written = rstd::fs::write_atomic(path.as_path(), text.as_str().as_bytes());
    if (written.is_err()) {
        return cmake_failure<empty>(rstd::format("cannot write CMake usage snapshot '{}': {}",
                                                 path.as_path(),
                                                 rstd::move(written).unwrap_err()));
    }
    return Ok(empty {});
}

auto parse_snapshot_strings(const Json& value, ref<str> key, ref<str> context)
    -> Result<Vec<String>> {
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

auto parse_usage_target(const Json& value, ref<str> context) -> Result<CMakeTargetUsageSnapshot> {
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
    -> Result<Vec<String>> {
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
            return cmake_failure<Vec<String>>(
                rstd::format("cannot inspect CMake link input '{}': {}",
                             candidate.as_path(),
                             rstd::move(exists).unwrap_err()));
        }
        if (! *exists) {
            result.push(token.clone());
            continue;
        }
        auto canonical = rstd::fs::canonicalize(candidate.as_path());
        if (canonical.is_err()) {
            return cmake_failure<Vec<String>>(
                rstd::format("cannot resolve CMake link input '{}': {}",
                             candidate.as_path(),
                             rstd::move(canonical).unwrap_err()));
        }
        auto text = path_text(canonical->as_path(), "CMake link input"_str);
        if (text.is_err()) return Err(rstd::move(text).unwrap_err());
        result.push(rstd::move(text).unwrap());
    }
    return Ok(rstd::move(result));
}

auto read_usage_snapshot(const CMakeWorkArea&                      area,
                         const ResolvedCMakeDependencyRequirement& requirement)
    -> Result<Option<CMakeUsageSnapshot>> {
    auto path   = usage_snapshot_path(area);
    auto exists = rstd::fs::exists(path.as_path());
    if (exists.is_err()) {
        return cmake_failure<Option<CMakeUsageSnapshot>>(
            rstd::format("cannot inspect CMake usage snapshot '{}': {}",
                         path.as_path(),
                         rstd::move(exists).unwrap_err()));
    }
    if (! *exists) return Ok(None());
    auto value = read_json(path.as_path(), "CMake usage snapshot"_str);
    if (value.is_err()) return Err(rstd::move(value).unwrap_err());
    auto schema = required_json_string(*value, "schema"_str, "CMake usage snapshot"_str);
    if (schema.is_err()) return Err(rstd::move(schema).unwrap_err());
    if (*schema != "lito-cmake-usage-v1"_str) {
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
    return Ok(Some(CMakeUsageSnapshot {
        .version  = String::make(*version),
        .targets  = rstd::move(parsed_targets),
        .combined = rstd::move(parsed_combined).unwrap(),
    }));
}

auto target_snapshot_identity(const CMakeProviderConfig&                provider,
                              const ResolvedCMakeDependencyRequirement& requirement,
                              ref<str>                                  target,
                              ref<str>                                  version,
                              const CMakeTargetUsageSnapshot&           snapshot,
                              ref<str> effective_target) -> Result<String> {
    auto executable = path_text(provider.executable.as_path(), "CMake executable"_str);
    if (executable.is_err()) return Err(rstd::move(executable).unwrap_err());
    auto result = String::make("lito-cmake-dependency-v1\n"_str);
    append_identity(result, executable->as_str());
    append_identity(result, provider.identity.as_str());
    append_identity(result, provider.generator.as_str());
    rstd_try(append_search_path_identity(result, provider));
    append_identity(result, requirement.package.as_str());
    append_identity(result, target);
    append_identity(result, version);
    append_identity(result, effective_target);
    append_identity(result, source_identity(requirement).as_str());
    for (const auto& token : snapshot.compile) append_identity(result, token.as_str());
    return Ok(rstd::move(result));
}

auto dependency_identity(const CMakeProviderConfig&                provider,
                         const ResolvedCMakeDependencyRequirement& requirement,
                         ref<str>                                  version,
                         const CMakeUsageSnapshot&                 snapshots,
                         ref<str> effective_target) -> Result<String> {
    auto executable = path_text(provider.executable.as_path(), "CMake executable"_str);
    if (executable.is_err()) return Err(rstd::move(executable).unwrap_err());
    auto result = String::make("lito-cmake-declaration-v1\n"_str);
    append_identity(result, executable->as_str());
    append_identity(result, provider.identity.as_str());
    append_identity(result, provider.generator.as_str());
    rstd_try(append_search_path_identity(result, provider));
    append_identity(result, requirement.package.as_str());
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
    return Ok(rstd::move(result));
}

} // namespace lito
