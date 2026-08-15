module;
#include <rstd/macro.hpp>

export module lito.manifest:dependency_schema;

import rstd;
import rstd.toml;
import lito.error;
import lito.manifest.contract;
import lito.package.identity;
import lito.dependency.contract;
import lito.source.contract;
import lito.platform;
import :primitives;
import :key_schema;
import :target_schema;

using namespace rstd::prelude;
using namespace rstd::literals;
using Toml  = rstd::toml::Value;
using Table = rstd::toml::Table;

namespace lito
{

auto parse_visibility(ref<str> value, ref<str> context) -> ManifestSchemaResult<DependencyVisibility> {
    if (value == "public"_str) return Ok(DependencyVisibility::Public);
    if (value == "private"_str) return Ok(DependencyVisibility::Private);
    if (value == "link"_str) return Ok(DependencyVisibility::LinkOnly);
    return failure<DependencyVisibility>(
        rstd::format("{} must be public, private, or link", context));
}

auto parse_pkg_config_version(ref<str> value, ref<str> context)
    -> ManifestSchemaResult<PkgConfigVersionRequirement> {
    auto text       = value.trim_ascii();
    auto comparison = PkgConfigVersionOperator::Equal;
    auto prefix     = usize {};
    if (text.starts_with(">="_str)) {
        comparison = PkgConfigVersionOperator::GreaterEqual;
        prefix     = usize(2);
    } else if (text.starts_with("<="_str)) {
        comparison = PkgConfigVersionOperator::LessEqual;
        prefix     = usize(2);
    } else if (text.starts_with("="_str)) {
        comparison = PkgConfigVersionOperator::Equal;
        prefix     = usize(1);
    } else if (text.starts_with(">"_str)) {
        comparison = PkgConfigVersionOperator::Greater;
        prefix     = usize(1);
    } else if (text.starts_with("<"_str)) {
        comparison = PkgConfigVersionOperator::Less;
        prefix     = usize(1);
    } else {
        return failure<PkgConfigVersionRequirement>(
            rstd::format("{} must begin with one of '=', '<', '>', '<=', or '>='", context));
    }
    auto version = text.get(prefix, text.len());
    if (version.is_none()) {
        return failure<PkgConfigVersionRequirement>(
            rstd::format("{} must contain a version value", context));
    }
    auto normalized = version->trim_ascii();
    if (normalized.is_empty() || normalized.contains(" "_str) || normalized.contains("\t"_str) ||
        normalized.contains("<"_str) || normalized.contains(">"_str) ||
        normalized.contains("="_str)) {
        return failure<PkgConfigVersionRequirement>(
            rstd::format("{} contains an invalid version value", context));
    }
    return Ok(PkgConfigVersionRequirement {
        .comparison = comparison,
        .value      = String::make(normalized),
    });
}

auto cmake_name_character_is_valid(u8 value) -> bool {
    const auto character = value.to_primitive();
    const auto alpha =
        (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z');
    const auto digit = character >= '0' && character <= '9';
    return alpha || digit || character == '_' || character == '-' || character == '.' ||
           character == '+';
}

auto cmake_name_is_valid(ref<str> value) -> bool {
    if (value.is_empty() || value.starts_with("-"_str)) return false;
    for (auto character : value) {
        if (! cmake_name_character_is_valid(character)) return false;
    }
    return true;
}

auto cmake_target_is_valid(ref<str> value) -> bool {
    if (value.is_empty() || value.starts_with("-"_str)) return false;
    auto segment = usize {};
    for (usize index {}; index < value.len(); ++index) {
        if (value[index] != u8(':')) {
            if (! cmake_name_character_is_valid(value[index])) return false;
            ++segment;
            continue;
        }
        if (segment == usize {} || index + usize(1) >= value.len() ||
            value[index + usize(1)] != u8(':')) {
            return false;
        }
        segment = usize {};
        ++index;
    }
    return segment != usize {};
}

auto cmake_cache_key_is_valid(ref<str> value) -> bool {
    if (value.is_empty()) return false;
    for (auto byte : value) {
        const auto character = byte.to_primitive();
        const auto alpha =
            (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z');
        const auto digit = character >= '0' && character <= '9';
        if (! (alpha || digit || character == '_')) return false;
    }
    return true;
}

auto parse_cmake_cache(Option<ref<Toml>> value, ref<str> context) -> ManifestSchemaResult<Vec<CMakeCacheEntry>> {
    auto result = Vec<CMakeCacheEntry>::make();
    if (value.is_none()) return Ok(rstd::move(result));
    auto table = table_value(**value, context);
    if (table.is_err()) return Err(rstd::move(table).unwrap_err());
    auto keys = (**table).keys();
    for (auto key = keys.next(); key.is_some(); key = keys.next()) {
        if (! cmake_cache_key_is_valid((**key).as_str())) {
            return failure<Vec<CMakeCacheEntry>>(rstd::format(
                "{} key '{}' must contain only ASCII letters, digits, or '_'", context, **key));
        }
        auto item       = (**table).get((**key).as_str());
        auto text       = (**item).as_str();
        auto boolean    = (**item).as_bool();
        auto integer    = (**item).as_integer();
        auto cache_text = String::make();
        if (text.is_some())
            cache_text = String::make(*text);
        else if (boolean.is_some())
            cache_text = String::make(*boolean ? "ON"_str : "OFF"_str);
        else if (integer.is_some())
            cache_text = rstd::format("{}", *integer);
        else
            return failure<Vec<CMakeCacheEntry>>(rstd::format(
                "{} value '{}' must be a string, boolean, or integer", context, **key));
        result.push(CMakeCacheEntry {
            .name  = (**key).clone(),
            .value = rstd::move(cache_text),
        });
    }
    return Ok(rstd::move(result));
}

auto parse_git_reference(const Toml& specification, ref<str> context) -> ManifestSchemaResult<GitReference> {
    auto branch = optional_string(specification, "branch"_str, context);
    auto tag    = optional_string(specification, "tag"_str, context);
    auto rev    = optional_string(specification, "rev"_str, context);
    auto commit = optional_string(specification, "commit"_str, context);
    if (branch.is_err()) return Err(rstd::move(branch).unwrap_err());
    if (tag.is_err()) return Err(rstd::move(tag).unwrap_err());
    if (rev.is_err()) return Err(rstd::move(rev).unwrap_err());
    if (commit.is_err()) return Err(rstd::move(commit).unwrap_err());
    auto branch_value = rstd::move(branch).unwrap();
    auto tag_value    = rstd::move(tag).unwrap();
    auto rev_value    = rstd::move(rev).unwrap();
    auto commit_value = rstd::move(commit).unwrap();
    auto count        = usize {};
    if (branch_value.is_some()) ++count;
    if (tag_value.is_some()) ++count;
    if (rev_value.is_some()) ++count;
    if (commit_value.is_some()) ++count;
    if (count > usize(1)) {
        return failure<GitReference>(rstd::format(
            "{} may contain only one of 'branch', 'tag', 'rev', or 'commit'", context));
    }
    auto reference = GitReference {};
    if (branch_value.is_some()) {
        reference.kind  = GitReferenceKind::Branch;
        reference.value = rstd::move(branch_value).unwrap();
    } else if (tag_value.is_some()) {
        reference.kind  = GitReferenceKind::Tag;
        reference.value = rstd::move(tag_value).unwrap();
    } else if (rev_value.is_some()) {
        reference.kind  = GitReferenceKind::Rev;
        reference.value = rstd::move(rev_value).unwrap();
    } else if (commit_value.is_some()) {
        reference.kind  = GitReferenceKind::Commit;
        reference.value = rstd::move(commit_value).unwrap();
    }
    if (reference.kind != GitReferenceKind::DefaultBranch &&
        (reference.value.is_empty() || reference.value.as_str().starts_with("-"_str))) {
        return failure<GitReference>(rstd::format("{} Git selector is invalid", context));
    }
    if (reference.kind == GitReferenceKind::Commit &&
        ! git_commit_is_valid(reference.value.as_str())) {
        return failure<GitReference>(
            rstd::format("{} Git commit must be a full hexadecimal object id", context));
    }
    return Ok(rstd::move(reference));
}

auto validate_git_url(ref<str> value, ref<str> context) -> ManifestSchemaResult<empty> {
    if (value.is_empty() || value.starts_with("-"_str) || value.contains("#"_str)) {
        return failure<empty>(rstd::format("{}.git is not a valid Git source URL", context));
    }
    return Ok(empty {});
}

auto validate_archive_url(ref<str> value, ref<str> context) -> ManifestSchemaResult<empty> {
    if (! archive_url_is_valid(value)) {
        return failure<empty>(rstd::format("{}.archive is not a valid archive URL", context));
    }
    return Ok(empty {});
}

auto parse_cmake_archive_variants(Option<ref<Toml>> value, ref<str> context)
    -> ManifestSchemaResult<Option<Vec<CMakeArchiveVariant>>> {
    if (value.is_none()) return Ok(Option<Vec<CMakeArchiveVariant>> {});
    auto variant_context = rstd::format("{}.archives", context);
    auto table           = table_value(**value, variant_context.as_str());
    if (table.is_err()) return Err(rstd::move(table).unwrap_err());
    if ((**table).is_empty()) {
        return failure<Option<Vec<CMakeArchiveVariant>>>(
            rstd::format("{}.archives must not be empty", context));
    }
    auto variants = Vec<CMakeArchiveVariant>::with_capacity((**table).len());
    auto keys     = (**table).keys();
    for (auto key = keys.next(); key.is_some(); key = keys.next()) {
        const auto& name          = **key;
        auto        entry_context = rstd::format("{}.archives.{}", context, name.as_str());
        auto        entry         = (**table).get(name.as_str());
        auto        fields        = table_value(**entry, entry_context.as_str());
        if (fields.is_err()) return Err(rstd::move(fields).unwrap_err());
        rstd_try(reject_unknown(**fields, entry_context.as_str(), cmake_archive_variant_key));
        auto archive = required_string(**entry, "archive"_str, entry_context.as_str());
        auto sha256  = required_string(**entry, "sha256"_str, entry_context.as_str());
        if (archive.is_err()) return Err(rstd::move(archive).unwrap_err());
        if (sha256.is_err()) return Err(rstd::move(sha256).unwrap_err());
        rstd_try(validate_archive_url(archive->as_str(), entry_context.as_str()));
        if (! sha256_is_valid(sha256->as_str())) {
            return failure<Option<Vec<CMakeArchiveVariant>>>(rstd::format(
                "{}.sha256 must be a full hexadecimal SHA-256 digest", entry_context.as_str()));
        }
        auto architecture = canonical_architecture(name.as_str());
        if (architecture.is_err()) {
            return failure<Option<Vec<CMakeArchiveVariant>>>(
                rstd::format("{}.archives architecture '{}' is invalid", context, name.as_str()));
        }
        if (architecture->as_str() != name.as_str()) {
            return failure<Option<Vec<CMakeArchiveVariant>>>(
                rstd::format("{}.archives architecture '{}' is not canonical; use '{}'",
                             context,
                             name.as_str(),
                             architecture->as_str()));
        }
        variants.push(CMakeArchiveVariant {
            .architecture = rstd::move(architecture).unwrap(),
            .url          = rstd::move(archive).unwrap(),
            .sha256       = rstd::move(sha256).unwrap(),
        });
    }
    rstd::slice_::sort_unstable_by(
        variants.as_mut_slice().as_mut_ref(),
        [](const CMakeArchiveVariant& left, const CMakeArchiveVariant& right) {
            return left.architecture.name < right.architecture.name;
        });
    return Ok(Some(rstd::move(variants)));
}

auto workspace_reference_enabled(const Toml& specification, ref<str> context) -> ManifestSchemaResult<bool> {
    auto value = member(specification, "workspace"_str);
    if (value.is_none()) return Ok(false);
    auto enabled = (**value).as_bool();
    if (enabled.is_none() || ! *enabled) {
        return failure<bool>(rstd::format("{}.workspace must be true", context));
    }
    return Ok(true);
}

auto parse_package_dependency_source(const Toml& specification, ref<str> context)
    -> ManifestSchemaResult<PackageSourceRequirement> {
    auto path = optional_string(specification, "path"_str, context);
    auto git  = optional_string(specification, "git"_str, context);
    if (path.is_err()) return Err(rstd::move(path).unwrap_err());
    if (git.is_err()) return Err(rstd::move(git).unwrap_err());
    auto path_value = rstd::move(path).unwrap();
    auto git_value  = rstd::move(git).unwrap();
    if (path_value.is_some() == git_value.is_some()) {
        return failure<PackageSourceRequirement>(
            rstd::format("{} must contain exactly one of 'path' or 'git'", context));
    }
    auto reference = parse_git_reference(specification, context);
    if (reference.is_err()) return Err(rstd::move(reference).unwrap_err());
    if (git_value.is_none() && reference->kind != GitReferenceKind::DefaultBranch) {
        return failure<PackageSourceRequirement>(
            rstd::format("{} Git selector requires 'git'", context));
    }
    if (path_value.is_some()) {
        auto parsed = relative_path(rstd::move(path_value).unwrap(), "dependency.path"_str);
        if (parsed.is_err()) return Err(rstd::move(parsed).unwrap_err());
        return Ok(PackageSourceRequirement::Path(rstd::move(parsed).unwrap()));
    }
    auto url = rstd::move(git_value).unwrap();
    rstd_try(validate_git_url(url.as_str(), context));
    return Ok(PackageSourceRequirement::Git(rstd::move(url), rstd::move(reference).unwrap()));
}

struct ParsedDependencies {
    Vec<DeclaredDependency>           explicit_dependencies;
    Vec<WorkspaceDependencyReference> workspace_dependencies;
};

auto parse_dependencies(Option<ref<Toml>> value, bool development = false)
    -> ManifestSchemaResult<ParsedDependencies> {
    auto result = ParsedDependencies {};
    if (value.is_none()) return Ok(rstd::move(result));
    const auto table_context =
        development ? "manifest.dev-dependencies"_str : "manifest.dependencies"_str;
    auto table = table_value(**value, table_context);
    if (table.is_err()) return Err(rstd::move(table).unwrap_err());
    auto keys = (**table).keys();
    for (auto key = keys.next(); key.is_some(); key = keys.next()) {
        const auto& name    = **key;
        auto        context = rstd::format(
            "{} dependency '{}'", development ? "development"_str : "normal"_str, name.as_str());
        if (! package_name_is_valid(name.as_str())) {
            return failure<ParsedDependencies>(rstd::format(
                "dependency name '{}' must contain only ASCII letters, digits, '-' or '_'",
                name.as_str()));
        }
        auto specification = (**table).get(name.as_str());
        auto fields        = table_value(**specification, context.as_str());
        if (fields.is_err()) return Err(rstd::move(fields).unwrap_err());
        rstd_try(reject_unknown(
            **fields, context.as_str(), development ? dev_dependency_key : dependency_key));
        auto inherited = workspace_reference_enabled(**specification, context.as_str());
        if (inherited.is_err()) return Err(rstd::move(inherited).unwrap_err());
        if (*inherited) {
            rstd_try(reject_unknown(**fields,
                                    context.as_str(),
                                    development ? workspace_dev_dependency_reference_key
                                                : workspace_dependency_reference_key));
        }
        auto parsed_visibility = DependencyVisibility::Private;
        if (! development) {
            auto visibility = required_string(**specification, "visibility"_str, context.as_str());
            if (visibility.is_err()) return Err(rstd::move(visibility).unwrap_err());
            auto parsed = parse_visibility(visibility->as_str(), "dependency.visibility"_str);
            if (parsed.is_err()) return Err(rstd::move(parsed).unwrap_err());
            parsed_visibility = rstd::move(parsed).unwrap();
        }
        if (*inherited) {
            result.workspace_dependencies.push(WorkspaceDependencyReference {
                .name       = name.clone(),
                .visibility = parsed_visibility,
            });
            continue;
        }
        auto source = parse_package_dependency_source(**specification, context.as_str());
        if (source.is_err()) return Err(rstd::move(source).unwrap_err());
        result.explicit_dependencies.push(DeclaredDependency {
            .name       = name.clone(),
            .source     = rstd::move(source).unwrap(),
            .visibility = parsed_visibility,
        });
    }
    return Ok(rstd::move(result));
}

auto contains_dependency(const ParsedDependencies& dependencies, ref<str> name) -> bool {
    for (const auto& dependency : dependencies.explicit_dependencies) {
        if (dependency.name.as_str() == name) return true;
    }
    for (const auto& dependency : dependencies.workspace_dependencies) {
        if (dependency.name.as_str() == name) return true;
    }
    return false;
}

struct ParsedRuntimeDependencies {
    Vec<DeclaredRuntimeDependency>           explicit_dependencies;
    Vec<WorkspaceRuntimeDependencyReference> workspace_dependencies;
};

auto parse_runtime_dependencies(Option<ref<Toml>> value)
    -> ManifestSchemaResult<ParsedRuntimeDependencies> {
    auto result = ParsedRuntimeDependencies {};
    if (value.is_none()) return Ok(rstd::move(result));
    auto table = table_value(**value, "manifest.runtime-dependencies"_str);
    if (table.is_err()) return Err(rstd::move(table).unwrap_err());
    auto keys = (**table).keys();
    for (auto key = keys.next(); key.is_some(); key = keys.next()) {
        const auto& name = **key;
        auto context = rstd::format("runtime dependency '{}'", name.as_str());
        if (! package_name_is_valid(name.as_str())) {
            return failure<ParsedRuntimeDependencies>(rstd::format(
                "runtime dependency name '{}' must contain only ASCII letters, digits, '-' or '_'",
                name.as_str()));
        }
        auto specification = (**table).get(name.as_str());
        auto fields = table_value(**specification, context.as_str());
        if (fields.is_err()) return Err(rstd::move(fields).unwrap_err());
        rstd_try(reject_unknown(**fields, context.as_str(), runtime_dependency_key));
        auto inherited = rstd_try(workspace_reference_enabled(**specification, context.as_str()));
        if (inherited) {
            rstd_try(reject_unknown(
                **fields, context.as_str(), workspace_runtime_dependency_reference_key));
            result.workspace_dependencies.push(
                WorkspaceRuntimeDependencyReference { .name = name.clone() });
            continue;
        }
        auto source = rstd_try(parse_package_dependency_source(**specification, context.as_str()));
        result.explicit_dependencies.push(DeclaredRuntimeDependency {
            .name   = name.clone(),
            .source = rstd::move(source),
        });
    }
    return Ok(rstd::move(result));
}

auto parse_workspace_dependencies(Option<ref<Toml>> value)
    -> ManifestSchemaResult<Vec<WorkspaceDependencyDefinition>> {
    auto result = Vec<WorkspaceDependencyDefinition>::make();
    if (value.is_none()) return Ok(rstd::move(result));
    auto table = table_value(**value, "workspace.dependencies"_str);
    if (table.is_err()) return Err(rstd::move(table).unwrap_err());
    auto keys = (**table).keys();
    for (auto key = keys.next(); key.is_some(); key = keys.next()) {
        const auto& name    = **key;
        auto        context = rstd::format("workspace dependency '{}'", name.as_str());
        if (! package_name_is_valid(name.as_str())) {
            return failure<Vec<WorkspaceDependencyDefinition>>(
                rstd::format("workspace dependency alias '{}' is invalid", name.as_str()));
        }
        auto specification = (**table).get(name.as_str());
        auto fields        = table_value(**specification, context.as_str());
        if (fields.is_err()) return Err(rstd::move(fields).unwrap_err());
        rstd_try(reject_unknown(**fields, context.as_str(), workspace_dependency_key));
        auto source = parse_package_dependency_source(**specification, context.as_str());
        if (source.is_err()) return Err(rstd::move(source).unwrap_err());
        result.push(WorkspaceDependencyDefinition {
            .name   = name.clone(),
            .source = rstd::move(source).unwrap(),
        });
    }
    return Ok(rstd::move(result));
}

struct ParsedExternalDependencies {
    Vec<PkgConfigExternalDependency>                   pkg_config;
    Vec<WorkspacePkgConfigExternalDependencyReference> workspace_pkg_config;
    Vec<CMakeDependencyRequirement>                    cmake;
    Vec<WorkspaceCMakeExternalDependencyReference>     workspace_cmake;
};

auto parse_pkg_config_requirement(const Toml& specification, ref<str> context)
    -> ManifestSchemaResult<PkgConfigDependencyRequirement> {
    auto module  = required_string(specification, "module"_str, context);
    auto version = optional_string(specification, "version"_str, context);
    if (module.is_err()) return Err(rstd::move(module).unwrap_err());
    if (version.is_err()) return Err(rstd::move(version).unwrap_err());
    if (module->is_empty() || module->as_str().starts_with("-"_str)) {
        return failure<PkgConfigDependencyRequirement>(
            rstd::format("{}.module must be non-empty and must not start with '-'", context));
    }
    auto version_requirement = Option<PkgConfigVersionRequirement> {};
    if (version->is_some()) {
        auto parsed = parse_pkg_config_version(version->as_ref()->as_str(),
                                               "external pkg-config version"_str);
        if (parsed.is_err()) return Err(rstd::move(parsed).unwrap_err());
        version_requirement = Some(rstd::move(parsed).unwrap());
    }
    auto static_mode  = false;
    auto static_value = member(specification, "static"_str);
    if (static_value.is_some()) {
        auto parsed = (**static_value).as_bool();
        if (parsed.is_none()) {
            return failure<PkgConfigDependencyRequirement>(
                rstd::format("{}.static must be a boolean", context));
        }
        static_mode = *parsed;
    }
    return Ok(PkgConfigDependencyRequirement {
        .module  = rstd::move(module).unwrap(),
        .version = rstd::move(version_requirement),
        .mode    = static_mode ? PkgConfigQueryMode::Static : PkgConfigQueryMode::Shared,
    });
}

struct ParsedPkgConfigExternalDependencies {
    Vec<PkgConfigExternalDependency>                   explicit_dependencies;
    Vec<WorkspacePkgConfigExternalDependencyReference> workspace_dependencies;
};

auto parse_pkg_config_external_dependencies(Option<ref<Toml>> value)
    -> ManifestSchemaResult<ParsedPkgConfigExternalDependencies> {
    auto result = ParsedPkgConfigExternalDependencies {};
    if (value.is_none()) return Ok(rstd::move(result));
    auto table = table_value(**value, "external-dependencies.pkg-config"_str);
    if (table.is_err()) return Err(rstd::move(table).unwrap_err());
    auto keys = (**table).keys();
    for (auto key = keys.next(); key.is_some(); key = keys.next()) {
        const auto& alias   = **key;
        auto        context = rstd::format("pkg-config external dependency '{}'", alias.as_str());
        if (! package_name_is_valid(alias.as_str())) {
            return failure<ParsedPkgConfigExternalDependencies>(
                rstd::format("external dependency alias '{}' is invalid", alias.as_str()));
        }
        auto specification = (**table).get(alias.as_str());
        auto fields        = table_value(**specification, context.as_str());
        if (fields.is_err()) return Err(rstd::move(fields).unwrap_err());
        rstd_try(reject_unknown(**fields, context.as_str(), pkg_config_external_key));
        auto inherited = workspace_reference_enabled(**specification, context.as_str());
        if (inherited.is_err()) return Err(rstd::move(inherited).unwrap_err());
        if (*inherited) {
            rstd_try(reject_unknown(
                **fields, context.as_str(), workspace_pkg_config_external_reference_key));
        }
        auto visibility = required_string(**specification, "visibility"_str, context.as_str());
        if (visibility.is_err()) return Err(rstd::move(visibility).unwrap_err());
        auto parsed_visibility =
            parse_visibility(visibility->as_str(), "external pkg-config visibility"_str);
        if (parsed_visibility.is_err()) return Err(rstd::move(parsed_visibility).unwrap_err());
        if (*inherited) {
            result.workspace_dependencies.push(WorkspacePkgConfigExternalDependencyReference {
                .alias      = alias.clone(),
                .visibility = rstd::move(parsed_visibility).unwrap(),
            });
            continue;
        }
        auto requirement = parse_pkg_config_requirement(**specification, context.as_str());
        if (requirement.is_err()) return Err(rstd::move(requirement).unwrap_err());
        result.explicit_dependencies.push(PkgConfigExternalDependency {
            .alias       = alias.clone(),
            .requirement = rstd::move(requirement).unwrap(),
            .visibility  = rstd::move(parsed_visibility).unwrap(),
        });
    }
    return Ok(rstd::move(result));
}

auto parse_workspace_pkg_config_external_dependencies(Option<ref<Toml>> value)
    -> ManifestSchemaResult<Vec<WorkspacePkgConfigExternalDependencyDefinition>> {
    auto result = Vec<WorkspacePkgConfigExternalDependencyDefinition>::make();
    if (value.is_none()) return Ok(rstd::move(result));
    auto table = table_value(**value, "workspace.external-dependencies.pkg-config"_str);
    if (table.is_err()) return Err(rstd::move(table).unwrap_err());
    auto keys = (**table).keys();
    for (auto key = keys.next(); key.is_some(); key = keys.next()) {
        const auto& alias = **key;
        auto        context =
            rstd::format("workspace pkg-config external dependency '{}'", alias.as_str());
        if (! package_name_is_valid(alias.as_str())) {
            return failure<Vec<WorkspacePkgConfigExternalDependencyDefinition>>(
                rstd::format("external dependency alias '{}' is invalid", alias.as_str()));
        }
        auto specification = (**table).get(alias.as_str());
        auto fields        = table_value(**specification, context.as_str());
        if (fields.is_err()) return Err(rstd::move(fields).unwrap_err());
        rstd_try(reject_unknown(**fields, context.as_str(), workspace_pkg_config_external_key));
        auto requirement = parse_pkg_config_requirement(**specification, context.as_str());
        if (requirement.is_err()) return Err(rstd::move(requirement).unwrap_err());
        result.push(WorkspacePkgConfigExternalDependencyDefinition {
            .alias       = alias.clone(),
            .requirement = rstd::move(requirement).unwrap(),
        });
    }
    return Ok(rstd::move(result));
}

auto parse_cmake_targets(const Toml& specification, ref<str> context)
    -> ManifestSchemaResult<Vec<CMakeTargetRequirement>> {
    auto value = member(specification, "targets"_str);
    if (value.is_none()) {
        return failure<Vec<CMakeTargetRequirement>>(
            rstd::format("{} is missing 'targets'", context));
    }
    auto array = (**value).as_array();
    if (array.is_none() || (**array).is_empty()) {
        return failure<Vec<CMakeTargetRequirement>>(
            rstd::format("{}.targets must be a non-empty array", context));
    }
    auto result = Vec<CMakeTargetRequirement>::with_capacity((**array).len());
    auto names  = rstd::collections::BTreeMap<String, empty>::make();
    for (const auto& item : **array) {
        auto target = table_value(item, rstd::format("{}.targets item", context).as_str());
        if (target.is_err()) return Err(rstd::move(target).unwrap_err());
        rstd_try(reject_unknown(**target, "CMake target"_str, cmake_target_key));
        auto name       = required_string(item, "name"_str, "CMake target"_str);
        auto visibility = required_string(item, "visibility"_str, "CMake target"_str);
        if (name.is_err()) return Err(rstd::move(name).unwrap_err());
        if (visibility.is_err()) return Err(rstd::move(visibility).unwrap_err());
        if (! cmake_target_is_valid(name->as_str())) {
            return failure<Vec<CMakeTargetRequirement>>(
                rstd::format("CMake target '{}' is invalid", name->as_str()));
        }
        if (names.contains_key(name->as_str())) {
            return failure<Vec<CMakeTargetRequirement>>(
                rstd::format("{} repeats CMake target '{}'", context, name->as_str()));
        }
        names.insert(name->clone(), empty {});
        auto parsed_visibility =
            parse_visibility(visibility->as_str(), "CMake target visibility"_str);
        if (parsed_visibility.is_err()) return Err(rstd::move(parsed_visibility).unwrap_err());
        result.push(CMakeTargetRequirement {
            .name       = rstd::move(name).unwrap(),
            .visibility = rstd::move(parsed_visibility).unwrap(),
        });
    }
    return Ok(rstd::move(result));
}

auto parse_cmake_external_dependency_definition(const Toml& specification,
                                                String      alias,
                                                ref<str>    context)
    -> ManifestSchemaResult<WorkspaceCMakeExternalDependencyDefinition> {
    auto package     = required_string(specification, "find-package"_str, context);
    auto path        = optional_string(specification, "path"_str, context);
    auto git         = optional_string(specification, "git"_str, context);
    auto archive     = optional_string(specification, "archive"_str, context);
    auto archives    = parse_cmake_archive_variants(member(specification, "archives"_str), context);
    auto sha256      = optional_string(specification, "sha256"_str, context);
    auto integration = optional_string(specification, "integration"_str, context);
    auto adapter     = optional_string(specification, "adapter"_str, context);
    auto config_directory = optional_string(specification, "config-directory"_str, context);
    if (package.is_err()) return Err(rstd::move(package).unwrap_err());
    if (path.is_err()) return Err(rstd::move(path).unwrap_err());
    if (git.is_err()) return Err(rstd::move(git).unwrap_err());
    if (archive.is_err()) return Err(rstd::move(archive).unwrap_err());
    if (archives.is_err()) return Err(rstd::move(archives).unwrap_err());
    if (sha256.is_err()) return Err(rstd::move(sha256).unwrap_err());
    if (integration.is_err()) return Err(rstd::move(integration).unwrap_err());
    if (adapter.is_err()) return Err(rstd::move(adapter).unwrap_err());
    if (config_directory.is_err()) return Err(rstd::move(config_directory).unwrap_err());
    if (! cmake_name_is_valid(package->as_str())) {
        return failure<WorkspaceCMakeExternalDependencyDefinition>(
            rstd::format("{}.find-package is unsafe", context));
    }
    auto path_value     = rstd::move(path).unwrap();
    auto git_value      = rstd::move(git).unwrap();
    auto archive_value  = rstd::move(archive).unwrap();
    auto archives_value = rstd::move(archives).unwrap();
    auto source_count   = usize(path_value.is_some()) + usize(git_value.is_some()) +
                          usize(archive_value.is_some()) + usize(archives_value.is_some());
    if (source_count > usize(1)) {
        return failure<WorkspaceCMakeExternalDependencyDefinition>(
            rstd::format("{} cannot combine 'path', 'git', 'archive', and 'archives'", context));
    }
    auto reference = parse_git_reference(specification, context);
    if (reference.is_err()) return Err(rstd::move(reference).unwrap_err());
    if (git_value.is_none() && reference->kind != GitReferenceKind::DefaultBranch) {
        return failure<WorkspaceCMakeExternalDependencyDefinition>(
            rstd::format("{} Git selector requires 'git'", context));
    }
    auto cache = parse_cmake_cache(member(specification, "cache"_str),
                                   "CMake external dependency cache"_str);
    if (cache.is_err()) return Err(rstd::move(cache).unwrap_err());
    auto sourced = source_count == usize(1);
    if (! sourced && (! cache->is_empty() || config_directory->is_some())) {
        return failure<WorkspaceCMakeExternalDependencyDefinition>(
            rstd::format("{} cache and config-directory require a source", context));
    }
    auto integration_kind  = CMakeIntegration::Install;
    auto integration_value = rstd::move(integration).unwrap();
    if (integration_value.is_some()) {
        if (integration_value->as_str() == "build-tree"_str) {
            integration_kind = CMakeIntegration::BuildTree;
        } else if (integration_value->as_str() != "install"_str) {
            return failure<WorkspaceCMakeExternalDependencyDefinition>(
                rstd::format("{}.integration must be 'install' or 'build-tree'", context));
        }
    }
    if (integration_kind == CMakeIntegration::BuildTree && ! sourced) {
        return failure<WorkspaceCMakeExternalDependencyDefinition>(
            rstd::format("{} build-tree integration requires a source", context));
    }
    if (integration_kind == CMakeIntegration::BuildTree && config_directory->is_some()) {
        return failure<WorkspaceCMakeExternalDependencyDefinition>(
            rstd::format("{} build-tree integration cannot use config-directory", context));
    }
    auto adapter_value = rstd::move(adapter).unwrap();
    if (adapter_value.is_some() && integration_kind != CMakeIntegration::BuildTree) {
        return failure<WorkspaceCMakeExternalDependencyDefinition>(
            rstd::format("{}.adapter requires build-tree integration", context));
    }
    auto hash_value = rstd::move(sha256).unwrap();
    if (archive_value.is_some() != hash_value.is_some()) {
        return failure<WorkspaceCMakeExternalDependencyDefinition>(
            rstd::format("{}.archive and .sha256 must be specified together", context));
    }
    if (archive_value.is_some() && integration_kind != CMakeIntegration::BuildTree) {
        return failure<WorkspaceCMakeExternalDependencyDefinition>(
            rstd::format("{}.archive requires build-tree integration", context));
    }
    if (archives_value.is_some() && integration_kind != CMakeIntegration::BuildTree) {
        return failure<WorkspaceCMakeExternalDependencyDefinition>(
            rstd::format("{}.archives requires build-tree integration", context));
    }
    auto source = CMakeDependencySource::Installed();
    if (path_value.is_some()) {
        auto parsed =
            relative_path(rstd::move(path_value).unwrap(), "CMake external dependency path"_str);
        if (parsed.is_err()) return Err(rstd::move(parsed).unwrap_err());
        source = CMakeDependencySource::Path(rstd::move(parsed).unwrap());
    } else if (git_value.is_some()) {
        auto url = rstd::move(git_value).unwrap();
        rstd_try(validate_git_url(url.as_str(), context));
        source = CMakeDependencySource::Git(rstd::move(url), rstd::move(reference).unwrap());
    } else if (archive_value.is_some()) {
        auto url = rstd::move(archive_value).unwrap();
        rstd_try(validate_archive_url(url.as_str(), context));
        if (! sha256_is_valid(hash_value->as_str())) {
            return failure<WorkspaceCMakeExternalDependencyDefinition>(
                rstd::format("{}.sha256 must be a full hexadecimal SHA-256 digest", context));
        }
        source = CMakeDependencySource::Archive(rstd::move(url), rstd::move(hash_value).unwrap());
    } else if (archives_value.is_some()) {
        source = CMakeDependencySource::ArchitectureArchives(rstd::move(archives_value).unwrap());
    }
    auto adapter_path = Option<PathBuf> {};
    if (adapter_value.is_some()) {
        auto parsed = relative_path(rstd::move(adapter_value).unwrap(),
                                    "CMake external dependency adapter"_str);
        if (parsed.is_err()) return Err(rstd::move(parsed).unwrap_err());
        adapter_path = Some(rstd::move(parsed).unwrap());
    }
    auto directory = Option<PathBuf> {};
    if (config_directory->is_some()) {
        auto parsed = install_relative_path(rstd::move(config_directory).unwrap().unwrap(),
                                            "CMake external config-directory"_str);
        if (parsed.is_err()) return Err(rstd::move(parsed).unwrap_err());
        directory = Some(rstd::move(parsed).unwrap());
    }
    return Ok(WorkspaceCMakeExternalDependencyDefinition {
        .alias            = rstd::move(alias),
        .package          = rstd::move(package).unwrap(),
        .source           = rstd::move(source),
        .integration      = integration_kind,
        .adapter          = rstd::move(adapter_path),
        .config_directory = rstd::move(directory),
        .cache            = rstd::move(cache).unwrap(),
    });
}

struct ParsedCMakeExternalDependencies {
    Vec<CMakeDependencyRequirement>                explicit_dependencies;
    Vec<WorkspaceCMakeExternalDependencyReference> workspace_dependencies;
};

auto parse_cmake_external_dependencies(Option<ref<Toml>> value)
    -> ManifestSchemaResult<ParsedCMakeExternalDependencies> {
    auto result = ParsedCMakeExternalDependencies {};
    if (value.is_none()) return Ok(rstd::move(result));
    auto table = table_value(**value, "external-dependencies.cmake"_str);
    if (table.is_err()) return Err(rstd::move(table).unwrap_err());
    auto keys = (**table).keys();
    for (auto key = keys.next(); key.is_some(); key = keys.next()) {
        const auto& alias   = **key;
        auto        context = rstd::format("CMake external dependency '{}'", alias.as_str());
        if (! package_name_is_valid(alias.as_str())) {
            return failure<ParsedCMakeExternalDependencies>(
                rstd::format("external dependency alias '{}' is invalid", alias.as_str()));
        }
        auto specification = (**table).get(alias.as_str());
        auto fields        = table_value(**specification, context.as_str());
        if (fields.is_err()) return Err(rstd::move(fields).unwrap_err());
        rstd_try(reject_unknown(**fields, context.as_str(), cmake_external_key));
        auto inherited = workspace_reference_enabled(**specification, context.as_str());
        if (inherited.is_err()) return Err(rstd::move(inherited).unwrap_err());
        auto targets = parse_cmake_targets(**specification, context.as_str());
        if (targets.is_err()) return Err(rstd::move(targets).unwrap_err());
        if (*inherited) {
            rstd_try(
                reject_unknown(**fields, context.as_str(), workspace_cmake_external_reference_key));
            result.workspace_dependencies.push(WorkspaceCMakeExternalDependencyReference {
                .alias   = alias.clone(),
                .targets = rstd::move(targets).unwrap(),
            });
            continue;
        }
        auto definition = parse_cmake_external_dependency_definition(
            **specification, alias.clone(), context.as_str());
        if (definition.is_err()) return Err(rstd::move(definition).unwrap_err());
        auto value = rstd::move(definition).unwrap();
        result.explicit_dependencies.push(CMakeDependencyRequirement {
            .alias            = rstd::move(value.alias),
            .package          = rstd::move(value.package),
            .source           = rstd::move(value.source),
            .integration      = value.integration,
            .adapter          = rstd::move(value.adapter),
            .config_directory = rstd::move(value.config_directory),
            .cache            = rstd::move(value.cache),
            .targets          = rstd::move(targets).unwrap(),
        });
    }
    return Ok(rstd::move(result));
}

auto parse_workspace_cmake_external_dependencies(Option<ref<Toml>> value)
    -> ManifestSchemaResult<Vec<WorkspaceCMakeExternalDependencyDefinition>> {
    auto result = Vec<WorkspaceCMakeExternalDependencyDefinition>::make();
    if (value.is_none()) return Ok(rstd::move(result));
    auto table = table_value(**value, "workspace.external-dependencies.cmake"_str);
    if (table.is_err()) return Err(rstd::move(table).unwrap_err());
    auto keys = (**table).keys();
    for (auto key = keys.next(); key.is_some(); key = keys.next()) {
        const auto& alias = **key;
        auto context = rstd::format("workspace CMake external dependency '{}'", alias.as_str());
        if (! package_name_is_valid(alias.as_str())) {
            return failure<Vec<WorkspaceCMakeExternalDependencyDefinition>>(
                rstd::format("external dependency alias '{}' is invalid", alias.as_str()));
        }
        auto specification = (**table).get(alias.as_str());
        auto fields        = table_value(**specification, context.as_str());
        if (fields.is_err()) return Err(rstd::move(fields).unwrap_err());
        rstd_try(reject_unknown(**fields, context.as_str(), workspace_cmake_external_key));
        auto definition = parse_cmake_external_dependency_definition(
            **specification, alias.clone(), context.as_str());
        if (definition.is_err()) return Err(rstd::move(definition).unwrap_err());
        result.push(rstd::move(definition).unwrap());
    }
    return Ok(rstd::move(result));
}

auto parse_external_dependencies(Option<ref<Toml>> value) -> ManifestSchemaResult<ParsedExternalDependencies> {
    auto result = ParsedExternalDependencies {};
    if (value.is_none()) return Ok(rstd::move(result));
    auto table = table_value(**value, "manifest.external-dependencies"_str);
    if (table.is_err()) return Err(rstd::move(table).unwrap_err());
    rstd_try(
        reject_unknown(**table, "manifest.external-dependencies"_str, external_dependencies_key));
    auto pkg_config = parse_pkg_config_external_dependencies(member(**value, "pkg-config"_str));
    auto cmake      = parse_cmake_external_dependencies(member(**value, "cmake"_str));
    if (pkg_config.is_err()) return Err(rstd::move(pkg_config).unwrap_err());
    if (cmake.is_err()) return Err(rstd::move(cmake).unwrap_err());
    auto parsed_pkg_config      = rstd::move(pkg_config).unwrap();
    auto parsed_cmake           = rstd::move(cmake).unwrap();
    result.pkg_config           = rstd::move(parsed_pkg_config.explicit_dependencies);
    result.workspace_pkg_config = rstd::move(parsed_pkg_config.workspace_dependencies);
    result.cmake                = rstd::move(parsed_cmake.explicit_dependencies);
    result.workspace_cmake      = rstd::move(parsed_cmake.workspace_dependencies);
    return Ok(rstd::move(result));
}

struct ParsedWorkspaceExternalDependencies {
    Vec<WorkspacePkgConfigExternalDependencyDefinition> pkg_config;
    Vec<WorkspaceCMakeExternalDependencyDefinition>     cmake;
};

auto parse_workspace_external_dependencies(Option<ref<Toml>> value)
    -> ManifestSchemaResult<ParsedWorkspaceExternalDependencies> {
    auto result = ParsedWorkspaceExternalDependencies {};
    if (value.is_none()) return Ok(rstd::move(result));
    auto table = table_value(**value, "workspace.external-dependencies"_str);
    if (table.is_err()) return Err(rstd::move(table).unwrap_err());
    rstd_try(
        reject_unknown(**table, "workspace.external-dependencies"_str, external_dependencies_key));
    auto pkg_config =
        parse_workspace_pkg_config_external_dependencies(member(**value, "pkg-config"_str));
    auto cmake = parse_workspace_cmake_external_dependencies(member(**value, "cmake"_str));
    if (pkg_config.is_err()) return Err(rstd::move(pkg_config).unwrap_err());
    if (cmake.is_err()) return Err(rstd::move(cmake).unwrap_err());
    result.pkg_config = rstd::move(pkg_config).unwrap();
    result.cmake      = rstd::move(cmake).unwrap();
    return Ok(rstd::move(result));
}

} // namespace lito
