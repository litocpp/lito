module;
#include <rstd/macro.hpp>

export module lito.dependency.cmake.file_api;

import rstd;
import rstd.json;
import lito.error;
import lito.dependency.contract;
import lito.cpp;
import lito.system.process;
import lito.dependency.cmake.model;

using namespace rstd::prelude;
using namespace rstd::literals;
using Json      = rstd::json::Value;
using JsonArray = rstd::json::Array;

export namespace lito
{

auto read_json(ref<rstd::path::Path> path, ref<str> context) -> Result<Json> {
    auto contents = rstd::fs::read_to_string(path);
    if (contents.is_err()) {
        return cmake_failure<Json>(rstd::format(
            "cannot read {} '{}': {}", context, path, rstd::move(contents).unwrap_err()));
    }
    auto parsed = rstd::json::from_str(contents->as_str());
    if (parsed.is_err()) {
        return cmake_failure<Json>(rstd::format(
            "cannot parse {} '{}': {}", context, path, rstd::move(parsed).unwrap_err()));
    }
    return Ok(rstd::move(parsed).unwrap());
}

auto required_json_member(const Json& value, ref<str> key, ref<str> context) -> Result<ref<Json>> {
    auto member = value.get(key);
    if (member.is_none()) {
        return cmake_failure<ref<Json>>(rstd::format("{} is missing '{}'", context, key));
    }
    return Ok(*member);
}

auto required_json_string(const Json& value, ref<str> key, ref<str> context) -> Result<ref<str>> {
    auto member = required_json_member(value, key, context);
    if (member.is_err()) return Err(rstd::move(member).unwrap_err());
    auto text = (**member).as_str();
    if (text.is_none()) {
        return cmake_failure<ref<str>>(rstd::format("{}.{} must be a string", context, key));
    }
    return Ok(*text);
}

auto required_json_array(const Json& value, ref<str> key, ref<str> context)
    -> Result<ref<JsonArray>> {
    auto member = required_json_member(value, key, context);
    if (member.is_err()) return Err(rstd::move(member).unwrap_err());
    auto array = (**member).as_array();
    if (array.is_none()) {
        return cmake_failure<ref<JsonArray>>(rstd::format("{}.{} must be an array", context, key));
    }
    return Ok(*array);
}

auto current_reply_index(ref<rstd::path::Path> reply) -> Result<PathBuf> {
    auto opened = rstd::fs::read_dir(reply);
    if (opened.is_err()) {
        return cmake_failure<PathBuf>(rstd::format(
            "cannot read CMake File API reply '{}': {}", reply, rstd::move(opened).unwrap_err()));
    }
    auto selected      = Option<PathBuf> {};
    auto selected_name = String::make();
    auto entries       = rstd::move(opened).unwrap();
    for (auto entry = entries.next(); entry.is_some(); entry = entries.next()) {
        if (entry->is_err()) {
            return cmake_failure<PathBuf>(rstd::format("cannot read CMake File API entry: {}",
                                                       rstd::move(*entry).unwrap_err()));
        }
        auto value = rstd::move(*entry).unwrap();
        auto name  = value.file_name().into_string();
        if (name.is_err()) continue;
        auto text = rstd::move(name).unwrap();
        if (! text.as_str().starts_with("index-"_str) || ! text.as_str().ends_with(".json"_str)) {
            continue;
        }
        if (selected.is_none() || selected_name < text) {
            selected_name = rstd::move(text);
            selected      = Some(value.path());
        }
    }
    if (selected.is_none()) {
        return cmake_failure<PathBuf>("CMake File API produced no reply index"_str);
    }
    return Ok(rstd::move(selected).unwrap());
}

auto codemodel_path(const CMakeWorkArea& area) -> Result<PathBuf> {
    auto reply      = area.query_build.join(PathBuf::from(".cmake/api/v1/reply"_str).as_path());
    auto index_path = current_reply_index(reply.as_path());
    if (index_path.is_err()) return Err(rstd::move(index_path).unwrap_err());
    auto index = read_json(index_path->as_path(), "CMake File API index"_str);
    if (index.is_err()) return Err(rstd::move(index).unwrap_err());
    auto reply_member = required_json_member(*index, "reply"_str, "CMake File API index"_str);
    if (reply_member.is_err()) return Err(rstd::move(reply_member).unwrap_err());
    auto client =
        required_json_member(**reply_member, "client-lito"_str, "CMake File API reply"_str);
    if (client.is_err()) return Err(rstd::move(client).unwrap_err());
    auto query =
        required_json_member(**client, "query.json"_str, "CMake File API client reply"_str);
    if (query.is_err()) return Err(rstd::move(query).unwrap_err());
    auto responses =
        required_json_array(**query, "responses"_str, "CMake File API query reply"_str);
    if (responses.is_err()) return Err(rstd::move(responses).unwrap_err());
    if ((**responses).is_empty()) {
        return cmake_failure<PathBuf>("CMake File API query returned no response"_str);
    }
    auto file = required_json_string(
        (**responses)[usize {}], "jsonFile"_str, "CMake File API codemodel response"_str);
    if (file.is_err()) return Err(rstd::move(file).unwrap_err());
    return Ok(reply.join(PathBuf::from(*file).as_path()));
}

struct ProbeTargets {
    PathBuf      baseline;
    Vec<PathBuf> dependencies;
    PathBuf      combined;
};

auto probe_target_paths(const CMakeWorkArea&                      area,
                        const ResolvedCMakeDependencyRequirement& requirement)
    -> Result<ProbeTargets> {
    auto path = codemodel_path(area);
    if (path.is_err()) return Err(rstd::move(path).unwrap_err());
    auto model = read_json(path->as_path(), "CMake File API codemodel"_str);
    if (model.is_err()) return Err(rstd::move(model).unwrap_err());
    auto configurations = required_json_array(*model, "configurations"_str, "CMake codemodel"_str);
    if (configurations.is_err()) return Err(rstd::move(configurations).unwrap_err());
    if ((**configurations).is_empty()) {
        return cmake_failure<ProbeTargets>("CMake codemodel has no configuration"_str);
    }
    auto targets = required_json_array(
        (**configurations)[usize {}], "targets"_str, "CMake codemodel configuration"_str);
    if (targets.is_err()) return Err(rstd::move(targets).unwrap_err());
    auto baseline     = Option<PathBuf> {};
    auto combined     = Option<PathBuf> {};
    auto dependencies = Vec<Option<PathBuf>>::with_capacity(requirement.targets.len());
    for (usize index {}; index < requirement.targets.len(); ++index) {
        dependencies.emplace_back(None());
    }
    auto reply = area.query_build.join(PathBuf::from(".cmake/api/v1/reply"_str).as_path());
    for (const auto& target : **targets) {
        auto name = required_json_string(target, "name"_str, "CMake codemodel target"_str);
        auto file = required_json_string(target, "jsonFile"_str, "CMake codemodel target"_str);
        if (name.is_err()) return Err(rstd::move(name).unwrap_err());
        if (file.is_err()) return Err(rstd::move(file).unwrap_err());
        if (*name == "lito_cmake_baseline"_str)
            baseline = Some(reply.join(PathBuf::from(*file).as_path()));
        else if (*name == "lito_cmake_combined"_str)
            combined = Some(reply.join(PathBuf::from(*file).as_path()));
        else {
            for (usize index {}; index < dependencies.len(); ++index) {
                if (*name == rstd::format("lito_cmake_dependency_{}", index).as_str()) {
                    dependencies[index] = Some(reply.join(PathBuf::from(*file).as_path()));
                    break;
                }
            }
        }
    }
    if (baseline.is_none() || combined.is_none()) {
        return cmake_failure<ProbeTargets>("CMake codemodel is missing probe targets"_str);
    }
    auto resolved_dependencies = Vec<PathBuf>::with_capacity(dependencies.len());
    for (auto& dependency : dependencies) {
        if (dependency.is_none()) {
            return cmake_failure<ProbeTargets>("CMake codemodel is missing probe targets"_str);
        }
        resolved_dependencies.push(rstd::move(dependency).unwrap());
    }
    return Ok(ProbeTargets {
        .baseline     = rstd::move(baseline).unwrap(),
        .dependencies = rstd::move(resolved_dependencies),
        .combined     = rstd::move(combined).unwrap(),
    });
}

auto append_fragment_tokens(Vec<String>& output, ref<str> fragment, ref<str> context)
    -> Result<empty> {
    auto tokens = tokenize_command_fragments(fragment, context);
    if (tokens.is_err()) return Err(rstd::move(tokens).unwrap_err());
    for (auto& token : *tokens) output.push(rstd::move(token));
    return Ok(empty {});
}

auto compile_tokens(const Json& target) -> Result<Vec<String>> {
    auto result = Vec<String>::make();
    auto groups = target.get("compileGroups"_str);
    if (groups.is_none()) return Ok(rstd::move(result));
    auto array = (**groups).as_array();
    if (array.is_none())
        return cmake_failure<Vec<String>>("CMake compileGroups is not an array"_str);
    for (const auto& group : **array) {
        auto language = required_json_string(group, "language"_str, "CMake compile group"_str);
        if (language.is_err()) return Err(rstd::move(language).unwrap_err());
        if (*language != "CXX"_str) continue;
        auto fragments = group.get("compileCommandFragments"_str);
        if (fragments.is_some()) {
            auto values = (**fragments).as_array();
            if (values.is_none()) {
                return cmake_failure<Vec<String>>("CMake compile fragments is not an array"_str);
            }
            for (const auto& fragment : **values) {
                auto text =
                    required_json_string(fragment, "fragment"_str, "CMake compile fragment"_str);
                if (text.is_err()) return Err(rstd::move(text).unwrap_err());
                rstd_try(append_fragment_tokens(result, *text, "CMake compile fragment"_str));
            }
        }
        auto definitions = group.get("defines"_str);
        if (definitions.is_some()) {
            auto values = (**definitions).as_array();
            if (values.is_none())
                return cmake_failure<Vec<String>>("CMake defines is not an array"_str);
            for (const auto& definition : **values) {
                auto text = required_json_string(definition, "define"_str, "CMake definition"_str);
                if (text.is_err()) return Err(rstd::move(text).unwrap_err());
                result.push(rstd::format("-D{}", *text));
            }
        }
        auto includes = group.get("includes"_str);
        if (includes.is_some()) {
            auto values = (**includes).as_array();
            if (values.is_none())
                return cmake_failure<Vec<String>>("CMake includes is not an array"_str);
            for (const auto& include : **values) {
                auto path = required_json_string(include, "path"_str, "CMake include"_str);
                if (path.is_err()) return Err(rstd::move(path).unwrap_err());
                auto system    = include.get("isSystem"_str);
                auto is_system = false;
                if (system.is_some()) {
                    auto value = (**system).as_bool();
                    if (value.is_none())
                        return cmake_failure<Vec<String>>("CMake isSystem is not a boolean"_str);
                    is_system = *value;
                }
                if (is_system) {
                    result.push(String::make("-isystem"_str));
                    result.push(String::make(*path));
                } else {
                    result.push(rstd::format("-I{}", *path));
                }
            }
        }
        break;
    }
    return Ok(rstd::move(result));
}

auto link_tokens(const Json& target) -> Result<Vec<String>> {
    auto result = Vec<String>::make();
    auto link   = target.get("link"_str);
    if (link.is_none()) return Ok(rstd::move(result));
    auto fragments = (**link).get("commandFragments"_str);
    if (fragments.is_none()) return Ok(rstd::move(result));
    auto values = (**fragments).as_array();
    if (values.is_none())
        return cmake_failure<Vec<String>>("CMake link fragments is not an array"_str);
    for (const auto& fragment : **values) {
        auto text = required_json_string(fragment, "fragment"_str, "CMake link fragment"_str);
        if (text.is_err()) return Err(rstd::move(text).unwrap_err());
        rstd_try(append_fragment_tokens(result, *text, "CMake link fragment"_str));
    }
    return Ok(rstd::move(result));
}

auto subtract_baseline(const Vec<String>& baseline, Vec<String> values) -> Vec<String> {
    auto consumed = Vec<bool>::with_capacity(baseline.len());
    for (usize index {}; index < baseline.len(); ++index) consumed.emplace_back(false);
    auto result = Vec<String>::make();
    for (auto& value : values) {
        auto matched = false;
        for (usize index {}; index < baseline.len(); ++index) {
            if (! consumed[index] && baseline[index] == value) {
                consumed[index] = true;
                matched         = true;
                break;
            }
        }
        if (! matched) result.push(rstd::move(value));
    }
    return result;
}

struct CMakeTargetUsageSnapshot {
    Vec<String> compile;
    Vec<String> link;
};

struct CMakeUsageSnapshot {
    String                        version;
    Vec<CMakeTargetUsageSnapshot> targets;
    CMakeTargetUsageSnapshot      combined;
};

auto snapshot_from_targets(const Json& baseline, const Json& dependency)
    -> Result<CMakeTargetUsageSnapshot> {
    auto baseline_compile   = compile_tokens(baseline);
    auto baseline_link      = link_tokens(baseline);
    auto dependency_compile = compile_tokens(dependency);
    auto dependency_link    = link_tokens(dependency);
    if (baseline_compile.is_err()) return Err(rstd::move(baseline_compile).unwrap_err());
    if (baseline_link.is_err()) return Err(rstd::move(baseline_link).unwrap_err());
    if (dependency_compile.is_err()) return Err(rstd::move(dependency_compile).unwrap_err());
    if (dependency_link.is_err()) return Err(rstd::move(dependency_link).unwrap_err());
    return Ok(CMakeTargetUsageSnapshot {
        .compile = subtract_baseline(*baseline_compile, rstd::move(dependency_compile).unwrap()),
        .link    = subtract_baseline(*baseline_link, rstd::move(dependency_link).unwrap()),
    });
}

auto read_probe_snapshots(const CMakeWorkArea&                      area,
                          const ResolvedCMakeDependencyRequirement& requirement)
    -> Result<CMakeUsageSnapshot> {
    auto paths = probe_target_paths(area, requirement);
    if (paths.is_err()) return Err(rstd::move(paths).unwrap_err());
    auto baseline = read_json(paths->baseline.as_path(), "CMake baseline target"_str);
    if (baseline.is_err()) return Err(rstd::move(baseline).unwrap_err());
    auto snapshots = Vec<CMakeTargetUsageSnapshot>::with_capacity(paths->dependencies.len());
    for (const auto& path : paths->dependencies) {
        auto dependency = read_json(path.as_path(), "CMake dependency target"_str);
        if (dependency.is_err()) return Err(rstd::move(dependency).unwrap_err());
        auto snapshot = snapshot_from_targets(*baseline, *dependency);
        if (snapshot.is_err()) return Err(rstd::move(snapshot).unwrap_err());
        snapshots.push(rstd::move(snapshot).unwrap());
    }
    auto combined = read_json(paths->combined.as_path(), "CMake combined target"_str);
    if (combined.is_err()) return Err(rstd::move(combined).unwrap_err());
    auto combined_snapshot = snapshot_from_targets(*baseline, *combined);
    if (combined_snapshot.is_err()) return Err(rstd::move(combined_snapshot).unwrap_err());
    return Ok(CMakeUsageSnapshot {
        .targets  = rstd::move(snapshots),
        .combined = rstd::move(combined_snapshot).unwrap(),
    });
}

} // namespace lito
