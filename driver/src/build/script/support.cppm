module;
#include <rstd/macro.hpp>

module lito.driver:build.script.support;

import rstd;
import lito.core;
import lito.cpp;
import lito.system;
import rstd.json;
import :build.script.error;
import :build.generated.error;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito::system;

namespace lito
{

using Json = rstd::json::Value;
auto normal_relative_path(String text, ref<str> context) -> BuildScriptResult<PathBuf> {
    auto path = PathBuf::from(rstd::move(text));
    if (path.is_empty() || path.as_path().is_absolute() || path.as_path().has_root()) {
        return Err(BuildScriptError::Message(
            rstd::format("{} must be a non-empty relative path", context)));
    }
    auto components = path.as_path().components();
    for (auto component : components) {
        if (component.is_normal()) continue;
        return Err(BuildScriptError::Message(
            rstd::format("{} contains a non-normal path component", context)));
    }
    return Ok(rstd::move(path));
}

class BuildOutputRegistry {
public:
    auto claim(ref<str> package, ref<rstd::path::Path> relative, ref<str> owner)
        -> BuildScriptResult<empty> {
        auto key = String::make(package);
        key.push_ascii('\n');
        key.push_str(relative.to_string_lossy().as_str());
        auto existing = owners_.get(key.as_str());
        if (existing.is_some()) {
            return Err(BuildScriptError::Message(
                rstd::format("generated output '{}:{}' is claimed by build scripts '{}' and '{}'",
                             package,
                             relative,
                             **existing,
                             owner)));
        }
        owners_.insert(rstd::move(key), String::make(owner));
        return Ok(empty {});
    }

private:
    rstd::collections::BTreeMap<String, String> owners_ =
        rstd::collections::BTreeMap<String, String>::make();
};

struct ConfigurePackage {
    String  name;
    PathBuf source_root;
    PathBuf generated_root;
};

struct OwnedOutput {
    String  package;
    PathBuf relative;

    auto clone() const -> OwnedOutput {
        return OwnedOutput { .package = package.clone(), .relative = relative.clone() };
    }
};

auto same_output(const OwnedOutput& left, const OwnedOutput& right) noexcept -> bool {
    return left.package == right.package.as_str() && left.relative == right.relative.as_path();
}

auto contains_output(const Vec<OwnedOutput>& values, const OwnedOutput& candidate) noexcept
    -> bool {
    return values.iter().any([&](auto value) {
        return same_output((*value), candidate);
    });
}

auto package_is_selected(const Vec<ConfigurePackage>& packages, ref<str> name) noexcept -> bool {
    return packages.iter().any([&](auto package) {
        return package->name == name;
    });
}

auto package_is_selected(const Vec<String>& packages, ref<str> name) noexcept -> bool {
    return packages.iter().any([&](auto package) {
        return (*package) == name;
    });
}

auto package_component_is_valid(ref<str> package) noexcept -> bool {
    auto path  = PathBuf::from(package);
    auto parts = path.as_path().components();
    auto first = parts.next();
    return ! package.is_empty() && first.is_some() && first->is_normal() && parts.next().is_none();
}

auto load_receipt(ref<rstd::path::Path> path) -> BuildScriptResult<Vec<OwnedOutput>> {
    auto exists = rstd::fs::exists(path);
    if (exists.is_err()) {
        return Err(BuildScriptError::Io(
            "inspect configure receipt"_Str, PathBuf::from(path), rstd::move(exists).unwrap_err()));
    }
    if (! *exists) return Ok(Vec<OwnedOutput>::make());
    auto contents = rstd::fs::read_to_string(path);
    if (contents.is_err()) {
        return Err(BuildScriptError::Io(
            "read configure receipt"_Str, PathBuf::from(path), rstd::move(contents).unwrap_err()));
    }
    auto parsed = rstd::json::from_str(contents->as_str());
    if (parsed.is_err()) {
        return Err(BuildScriptError::Json(PathBuf::from(path), rstd::move(parsed).unwrap_err()));
    }
    auto document = rstd::move(parsed).unwrap();
    auto version  = document.get("version"_str);
    auto outputs  = document.get("outputs"_str);
    if (version.is_none() || (**version).as_i64() != Some(i64(1)) || outputs.is_none() ||
        (**outputs).as_array().is_none()) {
        return Err(BuildScriptError::Message(
            rstd::format("configure receipt '{}' has an unsupported schema", path)));
    }

    auto result = Vec<OwnedOutput>::make();
    auto array  = (**outputs).as_array();
    for (usize index {}; index < (**array).len(); ++index) {
        const auto& item     = (**array)[index];
        auto        package  = item.get("package"_str);
        auto        relative = item.get("path"_str);
        if (package.is_none() || relative.is_none()) {
            return Err(BuildScriptError::Message(
                rstd::format("configure receipt '{}' has an invalid output entry", path)));
        }
        auto package_text  = (**package).as_str();
        auto relative_text = (**relative).as_str();
        if (package_text.is_none() || relative_text.is_none() ||
            ! package_component_is_valid(*package_text)) {
            return Err(BuildScriptError::Message(
                rstd::format("configure receipt '{}' has an invalid output entry", path)));
        }
        auto path_value =
            normal_relative_path(String::make(*relative_text), "configure receipt output path"_str);
        if (path_value.is_err()) return Err(rstd::move(path_value).unwrap_err());
        result.push(OwnedOutput {
            .package  = String::make(*package_text),
            .relative = rstd::move(path_value).unwrap(),
        });
    }
    return Ok(rstd::move(result));
}

auto encode_receipt(const Vec<OwnedOutput>& outputs) -> String {
    auto array = rstd::json::Array::with_capacity(outputs.len());
    for (const auto& output : outputs) {
        auto object = rstd::json::Map::make();
        object.insert("package"_Str, Json::String(output.package.clone()));
        object.insert("path"_Str, Json::String(output.relative.as_path().to_string_lossy()));
        array.push(Json::Object(rstd::move(object)));
    }
    auto document = rstd::json::Map::make();
    document.insert("version"_Str, Json::Number(rstd::json::Number::from_i64(i64(1))));
    document.insert("outputs"_Str, Json::Array(rstd::move(array)));
    auto text =
        rstd::json::to_string(Json::Object(rstd::move(document)),
                              rstd::json::FormatOptions { .pretty = true, .indent = usize(2) });
    text.push_ascii('\n');
    return text;
}

auto find_package_root(const cpp::PackageMetadata& metadata, ref<str> name)
    -> Option<ref<rstd::path::Path>> {
    for (const auto& target : metadata.targets) {
        if (target.id.package == name) return Some(target.root.as_path());
    }
    return None();
}

struct ConfigureOutcome {
    PathBuf                output;
    rstd::fs::WriteOutcome write { rstd::fs::WriteOutcome::Unchanged };
};

} // namespace lito
