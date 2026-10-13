module;
#include <rstd/macro.hpp>
export module lito.core:registry.externals;
import rstd;
import rstd.json;
import :manifest.package;
import :manifest.name;
import :registry.error;
import :registry.identity;
import :source.tree;

using namespace rstd::prelude;
using namespace rstd::literals;
using rstd::json::Value;

export namespace lito::registry
{
class RegistryExternalDependencyProjection {
    Value declaration_;
    explicit RegistryExternalDependencyProjection(Value declaration)
        : declaration_(rstd::move(declaration)) {}
    friend auto parse_registry_external_inventory(const Value&)
        -> RegistryValueResult<Vec<RegistryExternalDependencyProjection>>;

public:
    auto declaration() const -> const Value& { return declaration_; }
    auto clone() const -> RegistryExternalDependencyProjection {
        return RegistryExternalDependencyProjection(declaration_.clone());
    }
};
auto parse_registry_external_inventory(const Value& input)
    -> RegistryValueResult<Vec<RegistryExternalDependencyProjection>>;
auto registry_external_inventory_json(slice<RegistryExternalDependencyProjection> values) -> Value;
auto project_registry_external_dependencies(const lito::manifest::PackageManifest& manifest)
    -> RegistryValueResult<Vec<RegistryExternalDependencyProjection>>;
} // namespace lito::registry

namespace lito::registry
{
auto external_error() -> RegistryValueError {
    return RegistryValueError::Message(
        "external dependency inventory is invalid or exceeds its limits"_Str);
}
auto external_text(const Value& value, usize maximum, bool empty = false) -> bool {
    auto text = value.as_str();
    if (text.is_none() || (! empty && text->is_empty()) || text->len() > maximum) return false;
    for (auto character : text->chars())
        if (character <= u32(31) || (character >= u32(127) && character <= u32(159))) return false;
    return true;
}
auto external_fields(const Value& value, initializer_list<ref<str>> names) -> bool {
    auto object = value.as_object();
    if (object.is_none() || object->get().len() != usize(names.size())) return false;
    for (auto name : names)
        if (value.get(name).is_none()) return false;
    return true;
}
auto external_usage(const Value& value) -> bool {
    auto array = value.as_array();
    if (array.is_none()) return false;
    const auto& fields = array->get();
    return (fields.len() == usize(1) &&
            (fields[usize()] == "compile"_str || fields[usize()] == "link"_str)) ||
           (fields.len() == usize(2) && fields[usize()] == "compile"_str &&
            fields[usize(1)] == "link"_str);
}
auto external_optional_text(const Value& value, usize maximum) -> bool {
    return value.is_Null() || external_text(value, maximum);
}
auto external_condition(const Value& value) -> bool {
    if (value.is_Null()) return true;
    auto text = value.as_str();
    if (text.is_none() || text->is_empty() || text->len() > usize(2048)) return false;
    for (auto byte : text->as_bytes())
        if (byte == u8()) return false;
    return true;
}
auto external_path(const Value& value) -> bool {
    if (value.is_Null()) return true;
    auto text = value.as_str();
    return text.is_some() && external_text(value, usize(1024)) &&
           lito::source::SourcePath::parse(*text).is_ok();
}
template<class F>
auto external_list(const Value& value, F check, ref<str> key = ""_str) -> bool {
    auto array = value.as_array();
    if (array.is_none() || array->get().len() > usize(256)) return false;
    Option<String> previous;
    for (const auto& item : array->get()) {
        if (! check(item)) return false;
        auto name = key.is_empty() ? item.as_str() : item.get(key)->get().as_str();
        if (name.is_none() || (previous.is_some() && *previous >= *name)) return false;
        previous = Some(String::make(*name));
    }
    return true;
}
auto external_declaration(const Value& value) -> bool {
    auto kind = value.get("kind"_str);
    if (kind.is_none()) return false;
    bool pkg = kind->get() == "pkg-config"_str;
    if (pkg ? ! external_fields(value,
                                { "kind"_str,
                                  "alias"_str,
                                  "module"_str,
                                  "condition"_str,
                                  "version"_str,
                                  "static"_str,
                                  "pub"_str,
                                  "usage"_str })
            : kind->get() != "cmake"_str || ! external_fields(value,
                                                              { "kind"_str,
                                                                "alias"_str,
                                                                "package"_str,
                                                                "condition"_str,
                                                                "components"_str,
                                                                "targets"_str,
                                                                "host_tools"_str,
                                                                "source"_str,
                                                                "adapter"_str,
                                                                "config_directory"_str,
                                                                "cache"_str }))
        return false;
    auto alias = value.get("alias"_str)->get().as_str();
    if (alias.is_none() || alias->len() > usize(256) ||
        ! lito::manifest::package_name_is_valid(*alias) ||
        ! external_condition(value["condition"_str]))
        return false;
    if (pkg) {
        const auto& version = value["version"_str];
        if (! version.is_Null()) {
            if (! external_fields(version, { "comparison"_str, "value"_str }) ||
                ! external_text(version["value"_str], usize(256)))
                return false;
            const auto& op = version["comparison"_str];
            if (op != "="_str && op != "<"_str && op != ">"_str && op != "<="_str && op != ">="_str)
                return false;
        }
        return external_text(value["module"_str], usize(256)) && value["static"_str].is_Bool() &&
               value["pub"_str].is_Bool() && external_usage(value["usage"_str]);
    }
    return external_text(value["package"_str], usize(256)) &&
           external_optional_text(value["source"_str], usize(2048)) &&
           external_path(value["adapter"_str]) && external_path(value["config_directory"_str]) &&
           external_list(value["components"_str],
                         [](const Value& item) {
                             return external_text(item, usize(256));
                         }) &&
           external_list(
               value["targets"_str],
               [](const Value& item) {
                   return external_fields(item, { "name"_str, "pub"_str, "usage"_str }) &&
                          external_text(item["name"_str], usize(512)) &&
                          item["pub"_str].is_Bool() && external_usage(item["usage"_str]);
               },
               "name"_str) &&
           external_list(
               value["host_tools"_str],
               [](const Value& item) {
                   return external_fields(item, { "name"_str, "target"_str }) &&
                          external_text(item["name"_str], usize(256)) &&
                          external_text(item["target"_str], usize(512));
               },
               "name"_str) &&
           external_list(
               value["cache"_str],
               [](const Value& item) {
                   return external_fields(item, { "name"_str, "value"_str }) &&
                          external_text(item["name"_str], usize(256)) &&
                          external_text(item["value"_str], usize(8192), true);
               },
               "name"_str);
}
auto external_string(ref<str> value) -> Value {
    return Value::String(String::make(value));
}
auto external_consumption(lito::dependency::DependencyConsumption consumption) -> Value {
    rstd::json::Array usage;
    if (consumption.usage.uses_compile()) usage.push(external_string("compile"_str));
    if (consumption.usage.uses_link()) usage.push(external_string("link"_str));
    return Value::Array(rstd::move(usage));
}
auto external_optional(const Option<String>& value) -> Value {
    return value.is_some() ? external_string(value->as_str()) : Value::Null();
}
auto external_path_projection(const Option<rstd::path::PathBuf>& path) -> Value {
    return path.is_some() && path->as_path().to_str().is_some()
               ? external_string(*path->as_path().to_str())
               : Value::Null();
}
template<class T>
auto external_sorted(const Vec<T>& values) -> Vec<const T*> {
    Vec<const T*> result;
    for (const auto& value : values) result.push(rstd::addressof(value));
    rstd::slice_::sort_unstable_by(result.as_mut_slice().as_mut_ref(), [](auto a, auto b) {
        return a->name < b->name;
    });
    return result;
}
} // namespace lito::registry

auto lito::registry::parse_registry_external_inventory(const Value& input)
    -> RegistryValueResult<Vec<RegistryExternalDependencyProjection>> {
    auto values = input.as_array();
    if (values.is_none() || values->get().len() > usize(256) ||
        rstd::json::to_string(input).len() > usize(65536))
        return Err(external_error());
    Vec<RegistryExternalDependencyProjection> result;
    Option<String>                            previous;
    for (const auto& value : values->get()) {
        if (! external_declaration(value)) return Err(external_error());
        auto key = rstd::format("{}:{}", *value["kind"_str].as_str(), *value["alias"_str].as_str());
        if (previous.is_some() && *previous >= key.as_str()) return Err(external_error());
        previous = Some(rstd::move(key));
        result.push(RegistryExternalDependencyProjection(value.clone()));
    }
    return Ok(rstd::move(result));
}
auto lito::registry::registry_external_inventory_json(
    slice<RegistryExternalDependencyProjection> values) -> Value {
    rstd::json::Array result;
    for (const auto& value : values) result.push(value.declaration().clone());
    return Value::Array(rstd::move(result));
}
auto lito::registry::project_registry_external_dependencies(
    const lito::manifest::PackageManifest& manifest)
    -> RegistryValueResult<Vec<RegistryExternalDependencyProjection>> {
    if (! manifest.workspace_pkg_config_external_dependencies.is_empty() ||
        ! manifest.workspace_cmake_external_dependencies.is_empty())
        return Err(external_error());
    rstd::json::Array result;
    for (const auto& dependency : manifest.cmake_external_dependencies) {
        rstd::json::Map fields;
        fields.insert("kind"_Str, external_string("cmake"_str));
        fields.insert("alias"_Str, external_string(dependency.alias.as_str()));
        fields.insert("package"_Str, external_string(dependency.package.as_str()));
        fields.insert("condition"_Str,
                      dependency.condition.is_some()
                          ? external_string(dependency.condition->source.as_str())
                          : Value::Null());
        fields.insert("source"_Str, external_optional(dependency.source));
        fields.insert("adapter"_Str, external_path_projection(dependency.adapter));
        fields.insert("config_directory"_Str,
                      external_path_projection(dependency.config_directory));
        auto components = dependency.components.clone();
        rstd::slice_::sort_unstable(components.as_mut_slice().as_mut_ref());
        rstd::json::Array names, targets, tools, cache;
        Option<String>    previous;
        for (const auto& component : components) {
            if (previous.is_some() && *previous == component) continue;
            names.push(external_string(component.as_str()));
            previous = Some(component.clone());
        }
        auto sorted_targets = external_sorted(dependency.targets);
        for (const auto& target : sorted_targets) {
            rstd::json::Map item;
            item.insert("name"_Str, external_string(target->name.as_str()));
            item.insert("pub"_Str, Value::Bool(target->consumption.is_public));
            item.insert("usage"_Str, external_consumption(target->consumption));
            targets.push(Value::Object(rstd::move(item)));
        }
        auto sorted_tools = external_sorted(dependency.host_tools);
        for (const auto& tool : sorted_tools) {
            rstd::json::Map item;
            item.insert("name"_Str, external_string(tool->name.as_str()));
            item.insert("target"_Str, external_string(tool->target.as_str()));
            tools.push(Value::Object(rstd::move(item)));
        }
        auto sorted_cache = external_sorted(dependency.cache);
        for (const auto& entry : sorted_cache) {
            rstd::json::Map item;
            item.insert("name"_Str, external_string(entry->name.as_str()));
            item.insert("value"_Str, external_string(entry->value.as_str()));
            cache.push(Value::Object(rstd::move(item)));
        }
        fields.insert("components"_Str, Value::Array(rstd::move(names)));
        fields.insert("targets"_Str, Value::Array(rstd::move(targets)));
        fields.insert("host_tools"_Str, Value::Array(rstd::move(tools)));
        fields.insert("cache"_Str, Value::Array(rstd::move(cache)));
        result.push(Value::Object(rstd::move(fields)));
    }
    for (const auto& dependency : manifest.pkg_config_external_dependencies) {
        rstd::json::Map fields;
        fields.insert("kind"_Str, external_string("pkg-config"_str));
        fields.insert("alias"_Str, external_string(dependency.alias.as_str()));
        fields.insert("module"_Str, external_string(dependency.requirement.module.as_str()));
        fields.insert("condition"_Str,
                      dependency.condition.is_some()
                          ? external_string(dependency.condition->source.as_str())
                          : Value::Null());
        fields.insert("static"_Str,
                      Value::Bool(dependency.requirement.mode ==
                                  lito::dependency::PkgConfigQueryMode::Static));
        fields.insert("pub"_Str, Value::Bool(dependency.consumption.is_public));
        fields.insert("usage"_Str, external_consumption(dependency.consumption));
        Value version = Value::Null();
        if (dependency.requirement.version.is_some()) {
            const auto& required   = *dependency.requirement.version;
            auto        comparison = "="_str;
            using Operator         = lito::dependency::PkgConfigVersionOperator;
            switch (required.comparison) {
            case Operator::Equal: break;
            case Operator::Less: comparison = "<"_str; break;
            case Operator::Greater: comparison = ">"_str; break;
            case Operator::LessEqual: comparison = "<="_str; break;
            case Operator::GreaterEqual: comparison = ">="_str; break;
            }
            rstd::json::Map item;
            item.insert("comparison"_Str, external_string(comparison));
            item.insert("value"_Str, external_string(required.value.as_str()));
            version = Value::Object(rstd::move(item));
        }
        fields.insert("version"_Str, rstd::move(version));
        result.push(Value::Object(rstd::move(fields)));
    }
    rstd::slice_::sort_unstable_by(
        result.as_mut_slice().as_mut_ref(), [](const Value& a, const Value& b) {
            auto ak = rstd::format("{}:{}", *a["kind"_str].as_str(), *a["alias"_str].as_str());
            auto bk = rstd::format("{}:{}", *b["kind"_str].as_str(), *b["alias"_str].as_str());
            return ak < bk;
        });
    return parse_registry_external_inventory(Value::Array(rstd::move(result)));
}
