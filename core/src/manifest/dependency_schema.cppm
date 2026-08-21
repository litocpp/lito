module;
#include <rstd/macro.hpp>

module lito.core:manifest.dependency_schema;

import rstd;
import rstd.toml;
import :manifest.dependency;
import :manifest.error;
import :package.identity;
import :dependency.visibility;
import :dependency.cmake;
import :dependency.pkg_config;
import :dependency.source;
import :source.git;
import :source.requirement;
import lito.system;
import :manifest.primitives;
import :manifest.key_schema;
import :manifest.target_schema;
import :condition;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;
using namespace lito::system;
using namespace rstd::literals;
using Toml  = rstd::toml::Value;
using Table = rstd::toml::Table;
using namespace lito::manifest;

auto parse_visibility(ref<str> value, ref<str> context)
    -> ManifestSchemaResult<lito::dependency::DependencyVisibility> {
    if (value == "public"_str) return Ok(lito::dependency::DependencyVisibility::Public);
    if (value == "private"_str) return Ok(lito::dependency::DependencyVisibility::Private);
    if (value == "link"_str) return Ok(lito::dependency::DependencyVisibility::LinkOnly);
    return manifest_schema_failure<lito::dependency::DependencyVisibility>(
        rstd::format("{} must be public, private, or link", context));
}

auto parse_pkg_config_version(ref<str> value, ref<str> context)
    -> ManifestSchemaResult<lito::dependency::PkgConfigVersionRequirement> {
    auto text       = value.trim_ascii();
    auto comparison = lito::dependency::PkgConfigVersionOperator::Equal;
    auto prefix     = usize {};
    if (text.starts_with(">="_str)) {
        comparison = lito::dependency::PkgConfigVersionOperator::GreaterEqual;
        prefix     = usize(2);
    } else if (text.starts_with("<="_str)) {
        comparison = lito::dependency::PkgConfigVersionOperator::LessEqual;
        prefix     = usize(2);
    } else if (text.starts_with("="_str)) {
        comparison = lito::dependency::PkgConfigVersionOperator::Equal;
        prefix     = usize(1);
    } else if (text.starts_with(">"_str)) {
        comparison = lito::dependency::PkgConfigVersionOperator::Greater;
        prefix     = usize(1);
    } else if (text.starts_with("<"_str)) {
        comparison = lito::dependency::PkgConfigVersionOperator::Less;
        prefix     = usize(1);
    } else {
        return manifest_schema_failure<lito::dependency::PkgConfigVersionRequirement>(
            rstd::format("{} must begin with one of '=', '<', '>', '<=', or '>='", context));
    }
    auto version = text.get(prefix, text.len());
    if (version.is_none()) {
        return manifest_schema_failure<lito::dependency::PkgConfigVersionRequirement>(
            rstd::format("{} must contain a version value", context));
    }
    auto normalized = version->trim_ascii();
    if (normalized.is_empty() || normalized.contains(" "_str) || normalized.contains("\t"_str) ||
        normalized.contains("<"_str) || normalized.contains(">"_str) ||
        normalized.contains("="_str)) {
        return manifest_schema_failure<lito::dependency::PkgConfigVersionRequirement>(
            rstd::format("{} contains an invalid version value", context));
    }
    return Ok(lito::dependency::PkgConfigVersionRequirement {
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

auto parse_cmake_cache(Option<ref<Toml>> value, ref<str> context)
    -> ManifestSchemaResult<Vec<lito::dependency::CMakeCacheEntry>> {
    auto result = Vec<lito::dependency::CMakeCacheEntry>::make();
    if (value.is_none()) return Ok(rstd::move(result));
    auto table = table_value(**value, context);
    if (table.is_err()) return Err(rstd::move(table).unwrap_err());
    auto keys = (**table).keys();
    for (auto key = keys.next(); key.is_some(); key = keys.next()) {
        if (! cmake_cache_key_is_valid((**key).as_str())) {
            return manifest_schema_failure<Vec<lito::dependency::CMakeCacheEntry>>(rstd::format(
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
            return manifest_schema_failure<Vec<lito::dependency::CMakeCacheEntry>>(rstd::format(
                "{} value '{}' must be a string, boolean, or integer", context, **key));
        result.push(lito::dependency::CMakeCacheEntry {
            .name  = (**key).clone(),
            .value = rstd::move(cache_text),
        });
    }
    return Ok(rstd::move(result));
}

auto parse_git_reference(const Toml& specification, ref<str> context)
    -> ManifestSchemaResult<lito::source::GitReference> {
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
        return manifest_schema_failure<lito::source::GitReference>(rstd::format(
            "{} may contain only one of 'branch', 'tag', 'rev', or 'commit'", context));
    }
    auto reference = lito::source::GitReference {};
    if (branch_value.is_some()) {
        reference.kind  = lito::source::GitReferenceKind::Branch;
        reference.value = rstd::move(branch_value).unwrap();
    } else if (tag_value.is_some()) {
        reference.kind  = lito::source::GitReferenceKind::Tag;
        reference.value = rstd::move(tag_value).unwrap();
    } else if (rev_value.is_some()) {
        reference.kind  = lito::source::GitReferenceKind::Rev;
        reference.value = rstd::move(rev_value).unwrap();
    } else if (commit_value.is_some()) {
        reference.kind  = lito::source::GitReferenceKind::Commit;
        reference.value = rstd::move(commit_value).unwrap();
    }
    if (reference.kind != lito::source::GitReferenceKind::DefaultBranch &&
        (reference.value.is_empty() || reference.value.as_str().starts_with("-"_str))) {
        return manifest_schema_failure<lito::source::GitReference>(
            rstd::format("{} Git selector is invalid", context));
    }
    if (reference.kind == lito::source::GitReferenceKind::Commit &&
        ! lito::source::git_commit_is_valid(reference.value.as_str())) {
        return manifest_schema_failure<lito::source::GitReference>(
            rstd::format("{} Git commit must be a full hexadecimal object id", context));
    }
    return Ok(rstd::move(reference));
}

auto validate_git_url(ref<str> value, ref<str> context) -> ManifestSchemaResult<empty> {
    if (value.is_empty() || value.starts_with("-"_str) || value.contains("#"_str)) {
        return manifest_schema_failure<empty>(
            rstd::format("{}.git is not a valid Git source URL", context));
    }
    return Ok(empty {});
}

auto validate_archive_url(ref<str> value, ref<str> context) -> ManifestSchemaResult<empty> {
    if (! archive_url_is_valid(value)) {
        return manifest_schema_failure<empty>(
            rstd::format("{}.archive is not a valid archive URL", context));
    }
    return Ok(empty {});
}

auto parse_external_archive_variants(Option<ref<Toml>> value, ref<str> context)
    -> ManifestSchemaResult<Option<Vec<lito::dependency::ExternalArchiveVariant>>> {
    if (value.is_none()) return Ok(Option<Vec<lito::dependency::ExternalArchiveVariant>> {});
    auto variant_context = rstd::format("{}.archives", context);
    auto table           = table_value(**value, variant_context.as_str());
    if (table.is_err()) return Err(rstd::move(table).unwrap_err());
    if ((**table).is_empty()) {
        return manifest_schema_failure<Option<Vec<lito::dependency::ExternalArchiveVariant>>>(
            rstd::format("{}.archives must not be empty", context));
    }
    auto variants = Vec<lito::dependency::ExternalArchiveVariant>::with_capacity((**table).len());
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
            return manifest_schema_failure<Option<Vec<lito::dependency::ExternalArchiveVariant>>>(
                rstd::format("{}.sha256 must be a full hexadecimal SHA-256 digest",
                             entry_context.as_str()));
        }
        auto architecture = canonical_architecture(name.as_str());
        if (architecture.is_err()) {
            return manifest_schema_failure<Option<Vec<lito::dependency::ExternalArchiveVariant>>>(
                rstd::format("{}.archives architecture '{}' is invalid", context, name.as_str()));
        }
        if (architecture->as_str() != name.as_str()) {
            return manifest_schema_failure<Option<Vec<lito::dependency::ExternalArchiveVariant>>>(
                rstd::format("{}.archives architecture '{}' is not canonical; use '{}'",
                             context,
                             name.as_str(),
                             architecture->as_str()));
        }
        variants.push(lito::dependency::ExternalArchiveVariant {
            .architecture = rstd::move(architecture).unwrap(),
            .url          = rstd::move(archive).unwrap(),
            .sha256       = rstd::move(sha256).unwrap(),
        });
    }
    rstd::slice_::sort_unstable_by(variants.as_mut_slice().as_mut_ref(),
                                   [](const lito::dependency::ExternalArchiveVariant& left,
                                      const lito::dependency::ExternalArchiveVariant& right) {
                                       return left.architecture.name < right.architecture.name;
                                   });
    return Ok(Some(rstd::move(variants)));
}

auto parse_external_source_requirement(const Toml& specification, ref<str> context)
    -> ManifestSchemaResult<lito::dependency::ExternalSourceRequirement> {
    auto path    = rstd_try(optional_string(specification, "path"_str, context));
    auto git     = rstd_try(optional_string(specification, "git"_str, context));
    auto archive = rstd_try(optional_string(specification, "archive"_str, context));
    auto sha256  = rstd_try(optional_string(specification, "sha256"_str, context));
    auto archives =
        rstd_try(parse_external_archive_variants(member(specification, "archives"_str), context));
    const auto source_count = usize(path.is_some()) + usize(git.is_some()) +
                              usize(archive.is_some()) + usize(archives.is_some());
    if (source_count != usize(1)) {
        return manifest_schema_failure<lito::dependency::ExternalSourceRequirement>(rstd::format(
            "{} must contain exactly one of 'path', 'git', 'archive', or 'archives'", context));
    }
    auto reference = rstd_try(parse_git_reference(specification, context));
    if (git.is_none() && reference.kind != lito::source::GitReferenceKind::DefaultBranch) {
        return manifest_schema_failure<lito::dependency::ExternalSourceRequirement>(
            rstd::format("{} Git selector requires 'git'", context));
    }
    if (archive.is_some() != sha256.is_some()) {
        return manifest_schema_failure<lito::dependency::ExternalSourceRequirement>(
            rstd::format("{}.archive and .sha256 must be specified together", context));
    }
    if (path.is_some()) {
        auto parsed = rstd_try(
            relative_path(rstd::move(path).unwrap(), rstd::format("{}.path", context).as_str()));
        return Ok(lito::dependency::ExternalSourceRequirement::Path(rstd::move(parsed)));
    }
    if (git.is_some()) {
        auto url = rstd::move(git).unwrap();
        rstd_try(validate_git_url(url.as_str(), context));
        return Ok(lito::dependency::ExternalSourceRequirement::Git(rstd::move(url),
                                                                   rstd::move(reference)));
    }
    if (archive.is_some()) {
        auto url  = rstd::move(archive).unwrap();
        auto hash = rstd::move(sha256).unwrap();
        rstd_try(validate_archive_url(url.as_str(), context));
        if (! sha256_is_valid(hash.as_str())) {
            return manifest_schema_failure<lito::dependency::ExternalSourceRequirement>(
                rstd::format("{}.sha256 must be a full hexadecimal SHA-256 digest", context));
        }
        return Ok(lito::dependency::ExternalSourceRequirement::Archive(rstd::move(url),
                                                                       rstd::move(hash)));
    }
    return Ok(lito::dependency::ExternalSourceRequirement::ArchitectureArchives(
        rstd::move(archives).unwrap()));
}

auto workspace_reference_enabled(const Toml& specification, ref<str> context)
    -> ManifestSchemaResult<bool>;

struct ParsedExternalSources {
    Vec<PackageExternalSourceDeclaration> explicit_sources;
    Vec<WorkspaceExternalSourceReference> workspace_sources;
};

auto parse_package_external_sources(Option<ref<Toml>> value, ref<rstd::path::Path> root)
    -> ManifestSchemaResult<ParsedExternalSources> {
    auto result = ParsedExternalSources {};
    if (value.is_none()) return Ok(rstd::move(result));
    auto table = rstd_try(table_value(**value, "manifest.external-sources"_str));
    auto keys  = table->keys();
    for (auto key = keys.next(); key.is_some(); key = keys.next()) {
        const auto& name    = **key;
        const auto  context = rstd::format("external source '{}'", name.as_str());
        if (! package_name_is_valid(name.as_str())) {
            return manifest_schema_failure<ParsedExternalSources>(
                rstd::format("external source name '{}' is invalid", name.as_str()));
        }
        const auto& specification = **table->get(name.as_str());
        auto        fields        = rstd_try(table_value(specification, context.as_str()));
        rstd_try(reject_unknown(*fields, context.as_str(), external_source_key));
        const auto inherited =
            rstd_try(workspace_reference_enabled(specification, context.as_str()));
        if (inherited) {
            rstd_try(
                reject_unknown(*fields, context.as_str(), workspace_external_source_reference_key));
            result.workspace_sources.push(
                WorkspaceExternalSourceReference { .name = name.clone() });
            continue;
        }
        auto source = rstd_try(parse_external_source_requirement(specification, context.as_str()));
        result.explicit_sources.push(PackageExternalSourceDeclaration {
            .name             = name.clone(),
            .source           = rstd::move(source),
            .declaration_root = Some(PathBuf::from(root)),
        });
    }
    return Ok(rstd::move(result));
}

auto parse_workspace_external_sources(Option<ref<Toml>> value)
    -> ManifestSchemaResult<Vec<WorkspaceExternalSourceDefinition>> {
    auto result = Vec<WorkspaceExternalSourceDefinition>::make();
    if (value.is_none()) return Ok(rstd::move(result));
    auto table = rstd_try(table_value(**value, "workspace.external-sources"_str));
    auto keys  = table->keys();
    for (auto key = keys.next(); key.is_some(); key = keys.next()) {
        const auto& name    = **key;
        const auto  context = rstd::format("workspace external source '{}'", name.as_str());
        if (! package_name_is_valid(name.as_str())) {
            return manifest_schema_failure<Vec<WorkspaceExternalSourceDefinition>>(
                rstd::format("workspace external source name '{}' is invalid", name.as_str()));
        }
        const auto& specification = **table->get(name.as_str());
        auto        fields        = rstd_try(table_value(specification, context.as_str()));
        rstd_try(reject_unknown(*fields, context.as_str(), workspace_external_source_key));
        auto source = rstd_try(parse_external_source_requirement(specification, context.as_str()));
        result.push(WorkspaceExternalSourceDefinition {
            .name   = name.clone(),
            .source = rstd::move(source),
        });
    }
    return Ok(rstd::move(result));
}

auto workspace_reference_enabled(const Toml& specification, ref<str> context)
    -> ManifestSchemaResult<bool> {
    auto value = member(specification, "workspace"_str);
    if (value.is_none()) return Ok(false);
    auto enabled = (**value).as_bool();
    if (enabled.is_none() || ! *enabled) {
        return manifest_schema_failure<bool>(rstd::format("{}.workspace must be true", context));
    }
    return Ok(true);
}

auto parse_package_dependency_source(const Toml& specification, ref<str> context)
    -> ManifestSchemaResult<lito::source::PackageSourceRequirement> {
    auto path    = optional_string(specification, "path"_str, context);
    auto git     = optional_string(specification, "git"_str, context);
    auto builtin = optional_string(specification, "builtin"_str, context);
    if (path.is_err()) return Err(rstd::move(path).unwrap_err());
    if (git.is_err()) return Err(rstd::move(git).unwrap_err());
    if (builtin.is_err()) return Err(rstd::move(builtin).unwrap_err());
    auto       path_value    = rstd::move(path).unwrap();
    auto       git_value     = rstd::move(git).unwrap();
    auto       builtin_value = rstd::move(builtin).unwrap();
    const auto source_count =
        usize(path_value.is_some()) + usize(git_value.is_some()) + usize(builtin_value.is_some());
    if (source_count != usize(1)) {
        return manifest_schema_failure<lito::source::PackageSourceRequirement>(
            rstd::format("{} must contain exactly one of 'path', 'git', or 'builtin'", context));
    }
    auto reference = parse_git_reference(specification, context);
    if (reference.is_err()) return Err(rstd::move(reference).unwrap_err());
    if (git_value.is_none() && reference->kind != lito::source::GitReferenceKind::DefaultBranch) {
        return manifest_schema_failure<lito::source::PackageSourceRequirement>(
            rstd::format("{} Git selector requires 'git'", context));
    }
    if (path_value.is_some()) {
        auto parsed = relative_path(rstd::move(path_value).unwrap(), "dependency.path"_str);
        if (parsed.is_err()) return Err(rstd::move(parsed).unwrap_err());
        return Ok(lito::source::PackageSourceRequirement::Path(rstd::move(parsed).unwrap()));
    }
    if (builtin_value.is_some()) {
        auto id = rstd::move(builtin_value).unwrap();
        if (! package_name_is_valid(id.as_str())) {
            return manifest_schema_failure<lito::source::PackageSourceRequirement>(
                rstd::format("{}.builtin must be a valid builtin package id", context));
        }
        return Ok(lito::source::PackageSourceRequirement::Builtin(rstd::move(id)));
    }
    auto url = rstd::move(git_value).unwrap();
    rstd_try(validate_git_url(url.as_str(), context));
    return Ok(lito::source::PackageSourceRequirement::Git(rstd::move(url),
                                                          rstd::move(reference).unwrap()));
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
            return manifest_schema_failure<ParsedDependencies>(rstd::format(
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
        auto parsed_visibility = Option<lito::dependency::DependencyVisibility> {};
        if (! development && member(**specification, "visibility"_str).is_some()) {
            auto visibility =
                rstd_try(required_string(**specification, "visibility"_str, context.as_str()));
            parsed_visibility =
                Some(rstd_try(parse_visibility(visibility.as_str(), "dependency.visibility"_str)));
        }
        auto requested_features = string_array(member(**specification, "features"_str),
                                               rstd::format("{}.features", context).as_str());
        if (requested_features.is_err()) {
            return Err(rstd::move(requested_features).unwrap_err());
        }
        auto parsed_features  = member(**specification, "features"_str).is_some()
                                    ? Some(rstd::move(requested_features).unwrap())
                                    : Option<Vec<String>> {};
        auto default_features = Option<bool> {};
        auto default_value    = member(**specification, "default-features"_str);
        if (default_value.is_some()) {
            auto parsed = (**default_value).as_bool();
            if (parsed.is_none()) {
                return manifest_schema_failure<ParsedDependencies>(
                    rstd::format("{}.default-features must be a boolean", context.as_str()));
            }
            default_features = Some(*parsed);
        }
        if (*inherited) {
            result.workspace_dependencies.push(WorkspaceDependencyReference {
                .name             = name.clone(),
                .visibility       = parsed_visibility,
                .features         = rstd::move(parsed_features),
                .default_features = default_features,
            });
            continue;
        }
        auto source = parse_package_dependency_source(**specification, context.as_str());
        if (source.is_err()) return Err(rstd::move(source).unwrap_err());
        result.explicit_dependencies.push(DeclaredDependency {
            .name             = name.clone(),
            .source           = rstd::move(source).unwrap(),
            .visibility       = parsed_visibility,
            .features         = rstd::move(parsed_features),
            .default_features = default_features,
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
        const auto& name    = **key;
        auto        context = rstd::format("runtime dependency '{}'", name.as_str());
        if (! package_name_is_valid(name.as_str())) {
            return manifest_schema_failure<ParsedRuntimeDependencies>(rstd::format(
                "runtime dependency name '{}' must contain only ASCII letters, digits, '-' or '_'",
                name.as_str()));
        }
        auto specification = (**table).get(name.as_str());
        auto fields        = table_value(**specification, context.as_str());
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
            return manifest_schema_failure<Vec<WorkspaceDependencyDefinition>>(
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
    Vec<lito::dependency::PkgConfigExternalDependency> pkg_config;
    Vec<WorkspacePkgConfigExternalDependencyReference> workspace_pkg_config;
    Vec<lito::dependency::CMakeDependencyRequirement>  cmake;
    Vec<WorkspaceCMakeExternalDependencyReference>     workspace_cmake;
};

auto parse_pkg_config_requirement(const Toml& specification, ref<str> context)
    -> ManifestSchemaResult<lito::dependency::PkgConfigDependencyRequirement> {
    auto module  = required_string(specification, "module"_str, context);
    auto version = optional_string(specification, "version"_str, context);
    if (module.is_err()) return Err(rstd::move(module).unwrap_err());
    if (version.is_err()) return Err(rstd::move(version).unwrap_err());
    if (module->is_empty() || module->as_str().starts_with("-"_str)) {
        return manifest_schema_failure<lito::dependency::PkgConfigDependencyRequirement>(
            rstd::format("{}.module must be non-empty and must not start with '-'", context));
    }
    auto version_requirement = Option<lito::dependency::PkgConfigVersionRequirement> {};
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
            return manifest_schema_failure<lito::dependency::PkgConfigDependencyRequirement>(
                rstd::format("{}.static must be a boolean", context));
        }
        static_mode = *parsed;
    }
    return Ok(lito::dependency::PkgConfigDependencyRequirement {
        .module  = rstd::move(module).unwrap(),
        .version = rstd::move(version_requirement),
        .mode    = static_mode ? lito::dependency::PkgConfigQueryMode::Static
                               : lito::dependency::PkgConfigQueryMode::Shared,
    });
}

auto parse_external_dependency_condition(const Toml& specification, ref<str> context)
    -> ManifestSchemaResult<Option<lito::dependency::ExternalDependencyCondition>> {
    auto source = rstd_try(optional_string(specification, "condition"_str, context));
    if (source.is_none()) {
        return Ok(Option<lito::dependency::ExternalDependencyCondition> {});
    }
    auto expression = lito::condition::parse(source->as_str());
    if (expression.is_err()) {
        return manifest_schema_failure<Option<lito::dependency::ExternalDependencyCondition>>(
            rstd::format(
                "{} condition '{}': {}", context, source->as_str(), expression.unwrap_err()));
    }
    return Ok(Some(lito::dependency::ExternalDependencyCondition {
        .source     = rstd::move(source).unwrap(),
        .expression = rstd::move(expression).unwrap(),
    }));
}

struct ParsedPkgConfigExternalDependencies {
    Vec<lito::dependency::PkgConfigExternalDependency> explicit_dependencies;
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
            return manifest_schema_failure<ParsedPkgConfigExternalDependencies>(
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
        auto condition =
            rstd_try(parse_external_dependency_condition(**specification, context.as_str()));
        if (*inherited) {
            result.workspace_dependencies.push(WorkspacePkgConfigExternalDependencyReference {
                .alias      = alias.clone(),
                .visibility = rstd::move(parsed_visibility).unwrap(),
                .condition  = rstd::move(condition),
            });
            continue;
        }
        auto requirement = parse_pkg_config_requirement(**specification, context.as_str());
        if (requirement.is_err()) return Err(rstd::move(requirement).unwrap_err());
        result.explicit_dependencies.push(lito::dependency::PkgConfigExternalDependency {
            .alias       = alias.clone(),
            .requirement = rstd::move(requirement).unwrap(),
            .visibility  = rstd::move(parsed_visibility).unwrap(),
            .condition   = rstd::move(condition),
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
            return manifest_schema_failure<Vec<WorkspacePkgConfigExternalDependencyDefinition>>(
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
    -> ManifestSchemaResult<Vec<lito::dependency::CMakeTargetRequirement>> {
    auto value = member(specification, "targets"_str);
    if (value.is_none()) {
        return manifest_schema_failure<Vec<lito::dependency::CMakeTargetRequirement>>(
            rstd::format("{} is missing 'targets'", context));
    }
    auto array = (**value).as_array();
    if (array.is_none() || (**array).is_empty()) {
        return manifest_schema_failure<Vec<lito::dependency::CMakeTargetRequirement>>(
            rstd::format("{}.targets must be a non-empty array", context));
    }
    auto result = Vec<lito::dependency::CMakeTargetRequirement>::with_capacity((**array).len());
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
            return manifest_schema_failure<Vec<lito::dependency::CMakeTargetRequirement>>(
                rstd::format("CMake target '{}' is invalid", name->as_str()));
        }
        if (names.contains_key(name->as_str())) {
            return manifest_schema_failure<Vec<lito::dependency::CMakeTargetRequirement>>(
                rstd::format("{} repeats CMake target '{}'", context, name->as_str()));
        }
        names.insert(name->clone(), empty {});
        auto parsed_visibility =
            parse_visibility(visibility->as_str(), "CMake target visibility"_str);
        if (parsed_visibility.is_err()) return Err(rstd::move(parsed_visibility).unwrap_err());
        result.push(lito::dependency::CMakeTargetRequirement {
            .name       = rstd::move(name).unwrap(),
            .visibility = rstd::move(parsed_visibility).unwrap(),
        });
    }
    return Ok(rstd::move(result));
}

auto parse_cmake_host_tools(const Toml& specification, ref<str> context)
    -> ManifestSchemaResult<Vec<lito::dependency::CMakeHostToolRequirement>> {
    auto value  = member(specification, "host-tools"_str);
    auto result = Vec<lito::dependency::CMakeHostToolRequirement>::make();
    if (value.is_none()) return Ok(rstd::move(result));
    auto array = (**value).as_array();
    if (array.is_none() || (**array).is_empty()) {
        return manifest_schema_failure<Vec<lito::dependency::CMakeHostToolRequirement>>(
            rstd::format("{}.host-tools must be a non-empty array", context));
    }
    auto names = rstd::collections::BTreeMap<String, empty>::make();
    for (const auto& item : **array) {
        auto tool =
            rstd_try(table_value(item, rstd::format("{}.host-tools item", context).as_str()));
        rstd_try(reject_unknown(*tool, "CMake host tool"_str, cmake_host_tool_key));
        auto name   = rstd_try(required_string(item, "name"_str, "CMake host tool"_str));
        auto target = rstd_try(required_string(item, "target"_str, "CMake host tool"_str));
        if (! package_name_is_valid(name.as_str()) || ! cmake_target_is_valid(target.as_str())) {
            return manifest_schema_failure<Vec<lito::dependency::CMakeHostToolRequirement>>(
                rstd::format("{}.host-tools contains invalid name '{}' or target '{}'",
                             context,
                             name.as_str(),
                             target.as_str()));
        }
        if (names.contains_key(name.as_str())) {
            return manifest_schema_failure<Vec<lito::dependency::CMakeHostToolRequirement>>(
                rstd::format("{}.host-tools repeats name '{}'", context, name.as_str()));
        }
        names.insert(name.clone(), empty {});
        result.push(lito::dependency::CMakeHostToolRequirement {
            .name   = rstd::move(name),
            .target = rstd::move(target),
        });
    }
    return Ok(rstd::move(result));
}

auto parse_cmake_components(const Toml& specification, ref<str> context)
    -> ManifestSchemaResult<Vec<String>> {
    auto value = member(specification, "components"_str);
    auto components =
        rstd_try(string_array(value, rstd::format("{}.components", context).as_str()));
    if (value.is_some() && components.is_empty()) {
        return manifest_schema_failure<Vec<String>>(
            rstd::format("{}.components must not be empty", context));
    }
    auto names = rstd::collections::BTreeMap<String, empty>::make();
    for (const auto& component : components) {
        if (! lito::dependency::cmake_component_name_is_valid(component.as_str())) {
            return manifest_schema_failure<Vec<String>>(rstd::format(
                "{}.components contains unsafe component '{}'", context, component.as_str()));
        }
        if (names.contains_key(component.as_str())) {
            return manifest_schema_failure<Vec<String>>(
                rstd::format("{}.components repeats component '{}'", context, component.as_str()));
        }
        names.insert(component.clone(), empty {});
    }
    return Ok(rstd::move(components));
}

auto parse_cmake_external_dependency_definition(const Toml& specification,
                                                String      alias,
                                                ref<str>    context)
    -> ManifestSchemaResult<WorkspaceCMakeExternalDependencyDefinition> {
    auto package          = required_string(specification, "package"_str, context);
    auto components       = parse_cmake_components(specification, context);
    auto source           = optional_string(specification, "source"_str, context);
    auto adapter          = optional_string(specification, "adapter"_str, context);
    auto config_directory = optional_string(specification, "config-directory"_str, context);
    auto host_tools       = parse_cmake_host_tools(specification, context);
    if (package.is_err()) return Err(rstd::move(package).unwrap_err());
    if (components.is_err()) return Err(rstd::move(components).unwrap_err());
    if (source.is_err()) return Err(rstd::move(source).unwrap_err());
    if (adapter.is_err()) return Err(rstd::move(adapter).unwrap_err());
    if (config_directory.is_err()) return Err(rstd::move(config_directory).unwrap_err());
    if (host_tools.is_err()) return Err(rstd::move(host_tools).unwrap_err());
    if (! lito::dependency::cmake_package_name_is_valid(package->as_str())) {
        return manifest_schema_failure<WorkspaceCMakeExternalDependencyDefinition>(
            rstd::format("{}.package is unsafe", context));
    }
    auto source_value = rstd::move(source).unwrap();
    if (source_value.is_some() && ! package_name_is_valid(source_value->as_str())) {
        return manifest_schema_failure<WorkspaceCMakeExternalDependencyDefinition>(
            rstd::format("{}.source must name a package external source", context));
    }
    auto cache = parse_cmake_cache(member(specification, "cache"_str),
                                   "CMake external dependency cache"_str);
    if (cache.is_err()) return Err(rstd::move(cache).unwrap_err());
    if (source_value.is_none() && (! cache->is_empty() || config_directory->is_some())) {
        return manifest_schema_failure<WorkspaceCMakeExternalDependencyDefinition>(
            rstd::format("{} cache and config-directory require a source", context));
    }
    auto adapter_value = rstd::move(adapter).unwrap();
    if (adapter_value.is_some() && config_directory->is_some()) {
        return manifest_schema_failure<WorkspaceCMakeExternalDependencyDefinition>(
            rstd::format("{}.config-directory cannot be combined with adapter", context));
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
        .components       = rstd::move(components).unwrap(),
        .source           = rstd::move(source_value),
        .adapter          = rstd::move(adapter_path),
        .config_directory = rstd::move(directory),
        .cache            = rstd::move(cache).unwrap(),
        .host_tools       = rstd::move(host_tools).unwrap(),
    });
}

struct ParsedCMakeExternalDependencies {
    Vec<lito::dependency::CMakeDependencyRequirement> explicit_dependencies;
    Vec<WorkspaceCMakeExternalDependencyReference>    workspace_dependencies;
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
            return manifest_schema_failure<ParsedCMakeExternalDependencies>(
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
        auto condition =
            rstd_try(parse_external_dependency_condition(**specification, context.as_str()));
        if (*inherited) {
            rstd_try(
                reject_unknown(**fields, context.as_str(), workspace_cmake_external_reference_key));
            result.workspace_dependencies.push(WorkspaceCMakeExternalDependencyReference {
                .alias     = alias.clone(),
                .targets   = rstd::move(targets).unwrap(),
                .condition = rstd::move(condition),
            });
            continue;
        }
        auto definition = parse_cmake_external_dependency_definition(
            **specification, alias.clone(), context.as_str());
        if (definition.is_err()) return Err(rstd::move(definition).unwrap_err());
        auto value = rstd::move(definition).unwrap();
        result.explicit_dependencies.push(lito::dependency::CMakeDependencyRequirement {
            .alias            = rstd::move(value.alias),
            .package          = rstd::move(value.package),
            .components       = rstd::move(value.components),
            .condition        = rstd::move(condition),
            .source           = rstd::move(value.source),
            .adapter          = rstd::move(value.adapter),
            .config_directory = rstd::move(value.config_directory),
            .cache            = rstd::move(value.cache),
            .targets          = rstd::move(targets).unwrap(),
            .host_tools       = rstd::move(value.host_tools),
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
            return manifest_schema_failure<Vec<WorkspaceCMakeExternalDependencyDefinition>>(
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

auto parse_external_dependencies(Option<ref<Toml>> value)
    -> ManifestSchemaResult<ParsedExternalDependencies> {
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
