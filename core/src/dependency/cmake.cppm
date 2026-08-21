module;
#include <rstd/enum.hpp>

export module lito.core:dependency.cmake;

import rstd;
import :dependency.condition;
import :dependency.visibility;
import lito.system;

using namespace rstd::prelude;
using namespace rstd::literals;
using PathBuf = rstd::path::PathBuf;
using namespace lito::system;

export namespace lito::dependency
{

auto cmake_package_name_is_valid(ref<str> value) noexcept -> bool {
    if (value.is_empty() || value.starts_with("-"_str)) return false;
    for (const auto byte : value) {
        const auto character = byte.to_primitive();
        const auto alpha =
            (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z');
        const auto digit = character >= '0' && character <= '9';
        if (! (alpha || digit || character == '_' || character == '-' || character == '.' ||
               character == '+')) {
            return false;
        }
    }
    return true;
}

auto cmake_component_name_is_valid(ref<str> value) noexcept -> bool {
    return cmake_package_name_is_valid(value);
}

struct CMakeCacheEntry {
    String name;
    String value;
};

struct CMakeTargetRequirement {
    String               name;
    DependencyVisibility visibility { DependencyVisibility::Private };
};

struct CMakeHostToolRequirement {
    String name;
    String target;

    auto clone() const -> CMakeHostToolRequirement {
        return CMakeHostToolRequirement { .name = name.clone(), .target = target.clone() };
    }
};

struct CMakeDependencyRequirement {
    String                              alias;
    String                              package;
    Vec<String>                         components;
    Option<ExternalDependencyCondition> condition;
    Option<String>                      source;
    Option<PathBuf>                     adapter;
    Option<PathBuf>                     config_directory;
    Vec<CMakeCacheEntry>                cache;
    Vec<CMakeTargetRequirement>         targets;
    Vec<CMakeHostToolRequirement>       host_tools;
    Option<PathBuf>                     declaration_root;
    Option<PathBuf>                     adapter_root;

    auto clone() const -> CMakeDependencyRequirement {
        auto cache_copy = Vec<CMakeCacheEntry>::with_capacity(cache.len());
        for (const auto& entry : cache) {
            cache_copy.push(CMakeCacheEntry {
                .name  = entry.name.clone(),
                .value = entry.value.clone(),
            });
        }
        auto target_copy = Vec<CMakeTargetRequirement>::with_capacity(targets.len());
        for (const auto& target : targets) {
            target_copy.push(CMakeTargetRequirement {
                .name       = target.name.clone(),
                .visibility = target.visibility,
            });
        }
        auto result = CMakeDependencyRequirement {
            .alias      = alias.clone(),
            .package    = package.clone(),
            .components = as<Clone>(components).clone(),
            .cache      = rstd::move(cache_copy),
            .targets    = rstd::move(target_copy),
        };
        for (const auto& tool : host_tools) result.host_tools.push(tool.clone());
        if (condition.is_some()) result.condition = Some(condition->clone());
        if (source.is_some()) result.source = Some(source->clone());
        if (adapter.is_some()) result.adapter = Some(adapter->clone());
        if (config_directory.is_some()) result.config_directory = Some(config_directory->clone());
        if (declaration_root.is_some()) result.declaration_root = Some(declaration_root->clone());
        if (adapter_root.is_some()) result.adapter_root = Some(adapter_root->clone());
        return result;
    }
};

enum class CMakeBuildSourceOverride
{
    Installed,
};

struct CMakeBuildOverride {
    String                   package;
    CMakeBuildSourceOverride source { CMakeBuildSourceOverride::Installed };

    auto clone() const -> CMakeBuildOverride {
        return CMakeBuildOverride {
            .package = package.clone(),
            .source  = source,
        };
    }
};

struct CMakeBuildOverrideSet {
    Vec<CMakeBuildOverride> entries;

    auto clone() const -> CMakeBuildOverrideSet {
        auto copied = Vec<CMakeBuildOverride>::with_capacity(entries.len());
        for (const auto& entry : entries) copied.push(entry.clone());
        return CMakeBuildOverrideSet { .entries = rstd::move(copied) };
    }

    auto contains(ref<str> package) const noexcept -> bool {
        for (const auto& entry : entries) {
            if (entry.package.as_str() == package) return true;
        }
        return false;
    }
};

struct CMakeProviderConfig {
    PathBuf      executable;
    String       generator;
    String       identity;
    Vec<PathBuf> search_paths;

    auto clone() const -> CMakeProviderConfig {
        return CMakeProviderConfig {
            .executable   = executable.clone(),
            .generator    = generator.clone(),
            .identity     = identity.clone(),
            .search_paths = as<Clone>(search_paths).clone(),
        };
    }
};

} // namespace lito::dependency
