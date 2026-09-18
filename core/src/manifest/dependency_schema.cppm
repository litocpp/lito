module;
#include <rstd/macro.hpp>

module lito.core:manifest.dependency_schema;

import rstd;
import rstd.serde;
import :manifest.dependency;
import :manifest.error;
import :package.identity;
import :dependency.consumption;
import :dependency.cargo;
import :dependency.cmake;
import :dependency.pkg_config;
import :dependency.source;
import :source.git;
import :source.requirement;
import :registry.identity;
import :registry.version;
import lito.system;
import :manifest.primitives;
import :manifest.wire.document;
import :manifest.target_schema;
import :manifest.wire;
import :manifest.wire.common;
import :manifest.wire.dependency;
import :manifest.wire.external;
import :condition;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;
using namespace lito::system;
using namespace rstd::literals;
using namespace lito::manifest;
using DataPath = rstd::serde::DataPath;

enum class LegacyVisibility
{
    Public,
    Private,
    LinkOnly,
};

// TODO: Remove legacy dependency visibility support after the compatibility window.
auto parse_legacy_visibility(ref<str> value, ref<str> context)
    -> ManifestSchemaResult<LegacyVisibility> {
    if (value == "public"_str) return Ok(LegacyVisibility::Public);
    if (value == "private"_str) return Ok(LegacyVisibility::Private);
    if (value == "link"_str) return Ok(LegacyVisibility::LinkOnly);
    return Err(
        ManifestSchemaError::Domain(rstd::format("{} must be public, private, or link", context)));
}

auto usage_facet(ref<str> value, ref<str> context)
    -> ManifestSchemaResult<lito::dependency::DependencyUsageFacet> {
    using lito::dependency::DependencyUsageFacet;
    if (value == "compile"_str) return Ok(DependencyUsageFacet::Compile);
    if (value == "link"_str) return Ok(DependencyUsageFacet::Link);
    if (value == "runtime"_str) return Ok(DependencyUsageFacet::Runtime);
    return Err(ManifestSchemaError::Domain(
        rstd::format("{} must be 'compile', 'link', or 'runtime'", context)));
}

auto parse_usage(const Option<wire::TextList>& value, ref<str> context)
    -> ManifestSchemaResult<Option<lito::dependency::DependencyUsage>> {
    using lito::dependency::DependencyUsage;
    using lito::dependency::DependencyUsageFacet;
    if (value.is_none()) return Ok(Option<DependencyUsage> {});

    auto has_compile = false;
    auto has_link    = false;
    auto has_runtime = false;
    auto append      = [&](DependencyUsageFacet facet,
                           ref<str>             item_context) -> ManifestSchemaResult<empty> {
        auto* selected = &has_compile;
        if (facet == DependencyUsageFacet::Link) selected = &has_link;
        if (facet == DependencyUsageFacet::Runtime) selected = &has_runtime;
        if (*selected) {
            return Err(ManifestSchemaError::Domain(
                rstd::format("{} repeats a dependency usage facet", item_context)));
        }
        *selected = true;
        return Ok(empty {});
    };

    if (value->values.is_empty())
        return Err(ManifestSchemaError::Domain(rstd::format("{} must not be empty", context)));
    for (usize index {}; index < value->values.len(); ++index) {
        auto item_context =
            value->scalar ? String::make(context) : rstd::format("{}[{}]", context, index);
        rstd_try(append(rstd_try(usage_facet(value->values[index].as_str(), item_context.as_str())),
                        item_context.as_str()));
    }
    auto usage = DependencyUsage::from_facets(has_compile, has_link, has_runtime);
    if (usage.is_none()) {
        return Err(ManifestSchemaError::Domain(
            rstd::format("{} cannot combine runtime with compile or link", context)));
    }
    return Ok(Some(*usage));
}

template<typename Specification>
auto legacy_visibility(const Specification& specification, ref<str> context)
    -> ManifestSchemaResult<Option<LegacyVisibility>> {
    if (specification.visibility.is_none()) return Ok(Option<LegacyVisibility> {});
    if (specification.pub.is_some())
        return Err(ManifestSchemaError::Domain(
            rstd::format("{}.visibility cannot be combined with pub", context)));
    if (specification.usage.is_some() && ! specification.usage->scalar)
        return Err(ManifestSchemaError::Domain(
            rstd::format("{}.visibility cannot be combined with an array usage", context)));
    return Ok(Some(rstd_try(parse_legacy_visibility(specification.visibility->as_str(), context))));
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
        return Err(ManifestSchemaError::Domain(
            rstd::format("{} must begin with one of '=', '<', '>', '<=', or '>='", context)));
    }
    auto version = text.get(prefix, text.len());
    if (version.is_none()) {
        return Err(
            ManifestSchemaError::Domain(rstd::format("{} must contain a version value", context)));
    }
    auto normalized = version->trim_ascii();
    if (normalized.is_empty() || normalized.contains(" "_str) || normalized.contains("\t"_str) ||
        normalized.contains("<"_str) || normalized.contains(">"_str) ||
        normalized.contains("="_str)) {
        return Err(ManifestSchemaError::Domain(
            rstd::format("{} contains an invalid version value", context)));
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

auto parse_cmake_cache(Option<rstd::collections::BTreeMap<String, wire::CacheValue>> value,
                       ref<str>                                                      context)
    -> ManifestSchemaResult<Vec<lito::dependency::CMakeCacheEntry>> {
    auto result = Vec<lito::dependency::CMakeCacheEntry>::make();
    if (value.is_none()) return Ok(rstd::move(result));
    for (auto key : value->keys()) {
        if (! cmake_cache_key_is_valid(key->as_str()))
            return Err(ManifestSchemaError::Domain(rstd::format(
                "{} key '{}' must contain only ASCII letters, digits, or '_'", context, *key)));
        auto entry = value->get_mut(key->as_str()).unwrap();
        result.push(lito::dependency::CMakeCacheEntry { .name  = key->clone(),
                                                        .value = rstd::move(entry->text) });
    }
    return Ok(rstd::move(result));
}

template<typename Specification>
auto parse_git_reference(Specification& specification, ref<str> context)
    -> ManifestSchemaResult<lito::source::GitReference> {
    auto branch_value = rstd::move(specification.branch);
    auto tag_value    = rstd::move(specification.tag);
    auto rev_value    = rstd::move(specification.rev);
    auto commit_value = rstd::move(specification.commit);
    auto count        = usize {};
    if (branch_value.is_some()) ++count;
    if (tag_value.is_some()) ++count;
    if (rev_value.is_some()) ++count;
    if (commit_value.is_some()) ++count;
    if (count > usize(1)) {
        return Err(ManifestSchemaError::Domain(rstd::format(
            "{} may contain only one of 'branch', 'tag', 'rev', or 'commit'", context)));
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
        return Err(
            ManifestSchemaError::Domain(rstd::format("{} Git selector is invalid", context)));
    }
    if (reference.kind == lito::source::GitReferenceKind::Commit &&
        ! lito::source::git_commit_is_valid(reference.value.as_str())) {
        return Err(ManifestSchemaError::Domain(
            rstd::format("{} Git commit must be a full hexadecimal object id", context)));
    }
    return Ok(rstd::move(reference));
}

auto validate_git_url(ref<str> value, ref<str> context) -> ManifestSchemaResult<empty> {
    if (value.is_empty() || value.starts_with("-"_str) || value.contains("#"_str)) {
        return Err(ManifestSchemaError::Domain(
            rstd::format("{}.git is not a valid Git source URL", context)));
    }
    return Ok(empty {});
}

auto parse_external_archive_variants(
    Option<rstd::collections::BTreeMap<String, wire::ArchiveVariant>> value,
    ref<str>                                                          context)
    -> ManifestSchemaResult<Option<Vec<lito::dependency::ExternalArchiveVariant>>> {
    if (value.is_none()) return Ok(Option<Vec<lito::dependency::ExternalArchiveVariant>> {});
    if (value->is_empty())
        return Err(
            ManifestSchemaError::Domain(rstd::format("{}.archives must not be empty", context)));
    auto variants = Vec<lito::dependency::ExternalArchiveVariant>::make();
    for (auto key : value->keys()) {
        const auto& name          = *key;
        auto        entry         = value->get_mut(name.as_str()).unwrap();
        auto        entry_context = rstd::format("{}.archives.{}", context, name.as_str());
        auto        parsed_url =
            rstd_try(parse_archive_url(entry->archive->as_str(), entry_context.as_str()));
        auto parsed_sha =
            rstd_try(parse_manifest_sha256(entry->sha256->as_str(), entry_context.as_str()));
        auto architecture = require_architecture(name.as_str());
        if (architecture.is_err())
            return Err(ManifestSchemaError::Domain(
                rstd::format("{}.archives architecture '{}' is invalid", context, name.as_str())));
        variants.push(lito::dependency::ExternalArchiveVariant {
            .architecture = rstd::move(architecture).unwrap(),
            .url          = rstd::move(parsed_url),
            .sha256       = rstd::move(parsed_sha),
        });
    }
    rstd::slice_::sort_unstable_by(variants.as_mut_slice().as_mut_ref(),
                                   [](const lito::dependency::ExternalArchiveVariant& left,
                                      const lito::dependency::ExternalArchiveVariant& right) {
                                       return left.architecture < right.architecture;
                                   });
    return Ok(Some(rstd::move(variants)));
}

auto parse_external_source_requirement(wire::ExternalSourceFields& specification, ref<str> context)
    -> ManifestSchemaResult<lito::dependency::ExternalSourceRequirement> {
    auto path    = rstd::move(specification.path);
    auto git     = rstd::move(specification.git);
    auto archive = rstd::move(specification.archive);
    auto sha256  = rstd::move(specification.sha256);
    auto archives =
        rstd_try(parse_external_archive_variants(rstd::move(specification.archives), context));
    const auto source_count = usize(path.is_some()) + usize(git.is_some()) +
                              usize(archive.is_some()) + usize(archives.is_some());
    if (source_count != usize(1)) {
        return Err(ManifestSchemaError::Domain(rstd::format(
            "{} must contain exactly one of 'path', 'git', 'archive', or 'archives'", context)));
    }
    auto reference = rstd_try(parse_git_reference(specification, context));
    if (git.is_none() && reference.kind != lito::source::GitReferenceKind::DefaultBranch) {
        return Err(
            ManifestSchemaError::Domain(rstd::format("{} Git selector requires 'git'", context)));
    }
    if (archive.is_some() != sha256.is_some()) {
        return Err(ManifestSchemaError::Domain(
            rstd::format("{}.archive and .sha256 must be specified together", context)));
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
        auto url  = rstd_try(parse_archive_url(archive->as_str(), context));
        auto hash = rstd_try(parse_manifest_sha256(sha256->as_str(), context));
        return Ok(lito::dependency::ExternalSourceRequirement::Archive(rstd::move(url),
                                                                       rstd::move(hash)));
    }
    return Ok(lito::dependency::ExternalSourceRequirement::ArchitectureArchives(
        rstd::move(archives).unwrap()));
}

struct ParsedExternalSources {
    Vec<PackageExternalSourceDeclaration> explicit_sources;
    Vec<WorkspaceExternalSourceReference> workspace_sources;
};

auto parse_package_external_sources(
    Option<rstd::collections::BTreeMap<String, wire::ExternalSource<false>>> value,
    ref<rstd::path::Path> root) -> ManifestSchemaResult<ParsedExternalSources> {
    auto result = ParsedExternalSources {};
    if (value.is_none()) return Ok(rstd::move(result));
    auto table = rstd::move(value).unwrap();
    auto keys  = table.keys();
    for (auto key : keys) {
        const auto& name    = *key;
        const auto  context = rstd::format("external source '{}'", name.as_str());
        if (! package_name_is_valid(name.as_str())) {
            return Err(ManifestSchemaError::Domain(
                rstd::format("external source name '{}' is invalid", name.as_str())));
        }
        auto&      specification = *table.get_mut(name.as_str()).unwrap();
        const auto inherited     = specification.workspace.is_some();
        if (inherited) {
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

auto parse_workspace_external_sources(
    Option<rstd::collections::BTreeMap<String, wire::ExternalSource<true>>> value)
    -> ManifestSchemaResult<Vec<WorkspaceExternalSourceDefinition>> {
    auto result = Vec<WorkspaceExternalSourceDefinition>::make();
    if (value.is_none()) return Ok(rstd::move(result));
    auto table = rstd::move(value).unwrap();
    auto keys  = table.keys();
    for (auto key : keys) {
        const auto& name    = *key;
        const auto  context = rstd::format("workspace external source '{}'", name.as_str());
        if (! package_name_is_valid(name.as_str())) {
            return Err(ManifestSchemaError::Domain(
                rstd::format("workspace external source name '{}' is invalid", name.as_str())));
        }
        auto& specification = *table.get_mut(name.as_str()).unwrap();
        auto  source = rstd_try(parse_external_source_requirement(specification, context.as_str()));
        result.push(WorkspaceExternalSourceDefinition {
            .name   = name.clone(),
            .source = rstd::move(source),
        });
    }
    return Ok(rstd::move(result));
}

auto parse_package_dependency_source(wire::DependencyFields& specification,
                                     ref<str>                context,
                                     ref<str>                dependency_name)
    -> ManifestSchemaResult<PackageDependencySource> {
    auto       path_value     = rstd::move(specification.path);
    auto       git_value      = rstd::move(specification.git);
    auto       builtin_value  = rstd::move(specification.builtin);
    auto       version_value  = rstd::move(specification.version);
    auto       registry_value = rstd::move(specification.registry);
    const auto local_source_count =
        usize(path_value.is_some()) + usize(git_value.is_some()) + usize(builtin_value.is_some());
    if (local_source_count > usize(1)) {
        return Err(ManifestSchemaError::Domain(
            rstd::format("{} may contain only one of 'path', 'git', or 'builtin'", context)));
    }
    if (local_source_count == usize {} && version_value.is_none()) {
        return Err(ManifestSchemaError::Domain(rstd::format(
            "{} must contain one of 'path', 'git', 'builtin', or 'version'", context)));
    }
    auto reference = parse_git_reference(specification, context);
    if (reference.is_err()) return Err(rstd::move(reference).unwrap_err());
    if (git_value.is_none() && reference->kind != lito::source::GitReferenceKind::DefaultBranch) {
        return Err(
            ManifestSchemaError::Domain(rstd::format("{} Git selector requires 'git'", context)));
    }
    if (version_value.is_none() && registry_value.is_some()) {
        return Err(ManifestSchemaError::Domain(
            rstd::format("{}.registry requires a Registry 'version'", context)));
    }
    if (builtin_value.is_some() && version_value.is_some()) {
        return Err(ManifestSchemaError::Domain(
            rstd::format("{}.builtin cannot be combined with a Registry 'version'", context)));
    }

    auto publication = Option<lito::source::PackageRegistryRequirement> {};
    if (version_value.is_some()) {
        auto parsed_package = lito::registry::RegistryPackageName::parse(dependency_name);
        if (parsed_package.is_err()) {
            return Err(ManifestSchemaError::Domain(
                rstd::format("{} name '{}' is not a canonical Registry package name",
                             context,
                             dependency_name)));
        }
        auto parsed_requirement =
            lito::registry::VersionRequirement::parse(version_value->as_str());
        if (parsed_requirement.is_err()) {
            return Err(ManifestSchemaError::Domain(
                rstd::format("{}.version '{}' is not a supported Registry version requirement",
                             context,
                             version_value->as_str())));
        }
        if (registry_value.is_some()) {
            auto name = registry_value->as_str();
            if (! package_name_is_valid(name)) {
                return Err(ManifestSchemaError::Domain(rstd::format(
                    "{}.registry '{}' is not a valid configured registry name", context, name)));
            }
            for (auto byte : name.as_bytes()) {
                const auto ascii = byte.to_primitive();
                if (ascii >= 'A' && ascii <= 'Z') {
                    return Err(ManifestSchemaError::Domain(
                        rstd::format("{}.registry must use lowercase ASCII", context)));
                }
            }
        }
        publication = Some(lito::source::PackageRegistryRequirement {
            .registry    = rstd::move(registry_value),
            .package     = rstd::move(parsed_package).unwrap(),
            .requirement = rstd::move(parsed_requirement).unwrap(),
        });
    }

    auto resolution = Option<lito::source::PackageSourceRequirement> {};
    if (path_value.is_some()) {
        auto parsed = relative_path(rstd::move(path_value).unwrap(), "dependency.path"_str);
        if (parsed.is_err()) return Err(rstd::move(parsed).unwrap_err());
        resolution =
            Some(lito::source::PackageSourceRequirement::Path(rstd::move(parsed).unwrap()));
    } else if (builtin_value.is_some()) {
        auto id = rstd::move(builtin_value).unwrap();
        if (! package_name_is_valid(id.as_str())) {
            return Err(ManifestSchemaError::Domain(
                rstd::format("{}.builtin must be a valid builtin package id", context)));
        }
        resolution = Some(lito::source::PackageSourceRequirement::Builtin(rstd::move(id)));
    } else if (git_value.is_some()) {
        auto url = rstd::move(git_value).unwrap();
        rstd_try(validate_git_url(url.as_str(), context));
        resolution = Some(lito::source::PackageSourceRequirement::Git(
            rstd::move(url), rstd::move(reference).unwrap()));
    } else {
        const auto& registry = *publication;
        resolution           = Some(lito::source::PackageSourceRequirement::Registry(
            registry.registry.clone(), registry.package.clone(), registry.requirement.clone()));
    }
    return Ok(PackageDependencySource {
        .resolution  = rstd::move(resolution).unwrap(),
        .publication = rstd::move(publication),
    });
}

struct ParsedDependencies {
    Vec<DeclaredDependency>           explicit_dependencies;
    Vec<WorkspaceDependencyReference> workspace_dependencies;
};

template<wire::DependencyMode Mode>
auto parse_dependencies(Option<rstd::collections::BTreeMap<String, wire::Dependency<Mode>>> value,
                        bool development = false) -> ManifestSchemaResult<ParsedDependencies> {
    auto result = ParsedDependencies {};
    if (value.is_none()) return Ok(rstd::move(result));
    auto table = rstd::move(value).unwrap();
    for (auto key : table.keys()) {
        const auto& name    = *key;
        auto        context = rstd::format(
            "{} dependency '{}'", development ? "development"_str : "normal"_str, name.as_str());
        if (! package_name_is_valid(name.as_str()))
            return Err(ManifestSchemaError::Domain(rstd::format(
                "dependency name '{}' must contain only ASCII letters, digits, '-' or '_'",
                name.as_str())));
        auto& specification = *table.get_mut(name.as_str()).unwrap();
        auto  parsed_usage =
            rstd_try(parse_usage(specification.usage, rstd::format("{}.usage", context).as_str()));
        auto parsed_public = specification.pub;
        if (! development) {
            auto legacy = rstd_try(legacy_visibility(specification, context.as_str()));
            if (legacy.is_some()) {
                if (specification.usage.is_some())
                    return Err(ManifestSchemaError::Domain(
                        rstd::format("{}.visibility cannot be combined with usage", context)));

                parsed_public = Some(*legacy == LegacyVisibility::Public);
                if (*legacy == LegacyVisibility::LinkOnly)
                    parsed_usage = Some(lito::dependency::DependencyUsage::link_only());
            }
        } else if (parsed_public.is_some() && *parsed_public) {
            return Err(ManifestSchemaError::Domain(
                rstd::format("{}.pub must be false for a development dependency", context)));
        }
        if (specification.workspace.is_some()) {
            result.workspace_dependencies.push(WorkspaceDependencyReference {
                .name             = name.clone(),
                .usage            = parsed_usage,
                .is_public        = parsed_public,
                .features         = rstd::move(specification.features),
                .default_features = specification.default_features,
            });
        } else {
            auto source = rstd_try(
                parse_package_dependency_source(specification, context.as_str(), name.as_str()));
            result.explicit_dependencies.push(DeclaredDependency {
                .name             = name.clone(),
                .source           = rstd::move(source),
                .usage            = parsed_usage,
                .is_public        = parsed_public,
                .features         = rstd::move(specification.features),
                .default_features = specification.default_features,
            });
        }
    }
    return Ok(rstd::move(result));
}

auto contains_dependency(const ParsedDependencies& dependencies, ref<str> name) -> bool {
    return dependencies.explicit_dependencies.iter().any([&](auto dependency) {
        return dependency->name.as_str() == name;
    }) || dependencies.workspace_dependencies.iter().any([&](auto dependency) {
        return dependency->name.as_str() == name;
    });
}

struct ParsedRuntimeDependencies {
    Vec<DeclaredRuntimeDependency>           explicit_dependencies;
    Vec<WorkspaceRuntimeDependencyReference> workspace_dependencies;
};

auto parse_runtime_dependencies(
    Option<rstd::collections::BTreeMap<String, wire::Dependency<wire::DependencyMode::Runtime>>>
        value) -> ManifestSchemaResult<ParsedRuntimeDependencies> {
    auto result = ParsedRuntimeDependencies {};
    if (value.is_none()) return Ok(rstd::move(result));
    auto table = rstd::move(value).unwrap();
    auto keys  = table.keys();
    for (auto key : keys) {
        const auto& name    = *key;
        auto        context = rstd::format("runtime dependency '{}'", name.as_str());
        if (! package_name_is_valid(name.as_str())) {
            return Err(ManifestSchemaError::Domain(rstd::format(
                "runtime dependency name '{}' must contain only ASCII letters, digits, '-' or '_'",
                name.as_str())));
        }
        auto& specification = *table.get_mut(name.as_str()).unwrap();
        if (specification.workspace.is_some()) {
            result.workspace_dependencies.push(
                WorkspaceRuntimeDependencyReference { .name = name.clone() });
            continue;
        }
        auto source = rstd_try(
            parse_package_dependency_source(specification, context.as_str(), name.as_str()));
        result.explicit_dependencies.push(DeclaredRuntimeDependency {
            .name   = name.clone(),
            .source = rstd::move(source),
        });
    }
    return Ok(rstd::move(result));
}

auto parse_workspace_dependencies(
    Option<rstd::collections::BTreeMap<String, wire::Dependency<wire::DependencyMode::Workspace>>>
        value) -> ManifestSchemaResult<Vec<WorkspaceDependencyDefinition>> {
    auto result = Vec<WorkspaceDependencyDefinition>::make();
    if (value.is_none()) return Ok(rstd::move(result));
    auto table = rstd::move(value).unwrap();
    auto keys  = table.keys();
    for (auto key : keys) {
        const auto& name    = *key;
        auto        context = rstd::format("workspace dependency '{}'", name.as_str());
        if (! package_name_is_valid(name.as_str())) {
            return Err(ManifestSchemaError::Domain(
                rstd::format("workspace dependency alias '{}' is invalid", name.as_str())));
        }
        auto& specification = *table.get_mut(name.as_str()).unwrap();
        auto  source =
            parse_package_dependency_source(specification, context.as_str(), name.as_str());
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
    Vec<lito::dependency::CargoDependencyRequirement>  cargo;
    Vec<WorkspaceCargoExternalDependencyReference>     workspace_cargo;
};

auto parse_pkg_config_requirement(wire::PkgConfigFields& specification, ref<str> context)
    -> ManifestSchemaResult<lito::dependency::PkgConfigDependencyRequirement> {
    auto module  = rstd::move(specification.module).unwrap();
    auto version = rstd::move(specification.version);
    if (module.is_empty() || module.as_str().starts_with("-"_str)) {
        return Err(ManifestSchemaError::Domain(
            rstd::format("{}.module must be non-empty and must not start with '-'", context)));
    }
    auto version_requirement = Option<lito::dependency::PkgConfigVersionRequirement> {};
    if (version.is_some()) {
        auto parsed =
            parse_pkg_config_version(version->as_str(), "external pkg-config version"_str);
        if (parsed.is_err()) return Err(rstd::move(parsed).unwrap_err());
        version_requirement = Some(rstd::move(parsed).unwrap());
    }
    auto static_mode = specification.static_mode.unwrap_or(false);
    return Ok(lito::dependency::PkgConfigDependencyRequirement {
        .module  = rstd::move(module),
        .version = rstd::move(version_requirement),
        .mode    = static_mode ? lito::dependency::PkgConfigQueryMode::Static
                               : lito::dependency::PkgConfigQueryMode::Shared,
    });
}

auto parse_external_dependency_condition(Option<String> source, ref<str> context)
    -> ManifestSchemaResult<Option<lito::dependency::ExternalDependencyCondition>> {
    if (source.is_none()) {
        return Ok(Option<lito::dependency::ExternalDependencyCondition> {});
    }
    auto expression = lito::condition::parse(source->as_str());
    if (expression.is_err()) {
        return Err(ManifestSchemaError::Domain(rstd::format(
            "{} condition '{}': {}", context, source->as_str(), expression.unwrap_err())));
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

auto parse_pkg_config_external_dependencies(
    Option<rstd::collections::BTreeMap<String, wire::PkgConfig<false>>> value)
    -> ManifestSchemaResult<ParsedPkgConfigExternalDependencies> {
    auto result = ParsedPkgConfigExternalDependencies {};
    if (value.is_none()) return Ok(rstd::move(result));
    auto table = rstd::move(value).unwrap();
    auto keys  = table.keys();
    for (auto key : keys) {
        const auto& alias   = *key;
        auto        context = rstd::format("pkg-config external dependency '{}'", alias.as_str());
        if (! package_name_is_valid(alias.as_str())) {
            return Err(ManifestSchemaError::Domain(
                rstd::format("external dependency alias '{}' is invalid", alias.as_str())));
        }
        auto&      specification          = *table.get_mut(alias.as_str()).unwrap();
        const auto inherited              = specification.workspace.is_some();
        auto       dependency_consumption = lito::dependency::DependencyConsumption {};
        auto       legacy = rstd_try(legacy_visibility(specification, context.as_str()));
        if (legacy.is_some()) {
            auto old_usage =
                (specification.usage.is_some() ? Some(specification.usage->values[usize {}].clone())
                                               : Option<String> {});
            if (old_usage.is_some() && old_usage->as_str() != "link"_str &&
                old_usage->as_str() != "compile"_str) {
                return Err(ManifestSchemaError::Domain(rstd::format(
                    "{}.usage must be 'link' or 'compile' with legacy visibility", context)));
            }
            const auto compile_only = old_usage.is_some() && old_usage->as_str() == "compile"_str;
            if (compile_only && *legacy == LegacyVisibility::LinkOnly) {
                return Err(ManifestSchemaError::Domain(rstd::format(
                    "{}.visibility must be public or private when legacy usage is 'compile'",
                    context)));
            }
            dependency_consumption.usage =
                compile_only ? lito::dependency::DependencyUsage::compile_only()
                : *legacy == LegacyVisibility::LinkOnly
                    ? lito::dependency::DependencyUsage::link_only()
                    : lito::dependency::DependencyUsage::compile_and_link();
            dependency_consumption.is_public = *legacy == LegacyVisibility::Public;
        } else {
            auto usage = rstd_try(
                parse_usage(specification.usage, rstd::format("{}.usage", context).as_str()));
            dependency_consumption.usage =
                usage.is_some() ? *usage : lito::dependency::DependencyUsage::compile_and_link();
            if (dependency_consumption.usage.uses_runtime()) {
                return Err(ManifestSchemaError::Domain(
                    rstd::format("{}.usage does not support runtime", context)));
            }
            auto is_public                   = specification.pub;
            dependency_consumption.is_public = is_public.is_some() && *is_public;
        }
        auto condition = rstd_try(parse_external_dependency_condition(
            rstd::move(specification.condition), context.as_str()));
        if (inherited) {
            result.workspace_dependencies.push(WorkspacePkgConfigExternalDependencyReference {
                .alias       = alias.clone(),
                .consumption = dependency_consumption,
                .condition   = rstd::move(condition),
            });
            continue;
        }
        auto requirement = parse_pkg_config_requirement(specification, context.as_str());
        if (requirement.is_err()) return Err(rstd::move(requirement).unwrap_err());
        result.explicit_dependencies.push(lito::dependency::PkgConfigExternalDependency {
            .alias       = alias.clone(),
            .requirement = rstd::move(requirement).unwrap(),
            .consumption = dependency_consumption,
            .condition   = rstd::move(condition),
        });
    }
    return Ok(rstd::move(result));
}

auto parse_workspace_pkg_config_external_dependencies(
    Option<rstd::collections::BTreeMap<String, wire::PkgConfig<true>>> value)
    -> ManifestSchemaResult<Vec<WorkspacePkgConfigExternalDependencyDefinition>> {
    auto result = Vec<WorkspacePkgConfigExternalDependencyDefinition>::make();
    if (value.is_none()) return Ok(rstd::move(result));
    auto table = rstd::move(value).unwrap();
    auto keys  = table.keys();
    for (auto key : keys) {
        const auto& alias = *key;
        auto        context =
            rstd::format("workspace pkg-config external dependency '{}'", alias.as_str());
        if (! package_name_is_valid(alias.as_str())) {
            return Err(ManifestSchemaError::Domain(
                rstd::format("external dependency alias '{}' is invalid", alias.as_str())));
        }
        auto& specification = *table.get_mut(alias.as_str()).unwrap();
        auto  requirement   = parse_pkg_config_requirement(specification, context.as_str());
        if (requirement.is_err()) return Err(rstd::move(requirement).unwrap_err());
        result.push(WorkspacePkgConfigExternalDependencyDefinition {
            .alias       = alias.clone(),
            .requirement = rstd::move(requirement).unwrap(),
        });
    }
    return Ok(rstd::move(result));
}

auto parse_cmake_targets(Option<Vec<wire::CMakeTarget>> value, const DataPath& owner_path)
    -> ManifestSchemaResult<Vec<lito::dependency::CMakeTargetRequirement>> {
    auto path = owner_path.with_field("targets"_str);
    if (value.is_none()) {
        return Err(ManifestSchemaError::Data(
            rstd::serde::Error::invalid_value(rstd::move(path), "is required"_str)));
    }
    auto targets = rstd::move(value).unwrap();
    if (targets.is_empty()) {
        return Err(ManifestSchemaError::Data(
            rstd::serde::Error::invalid_value(rstd::move(path), "must not be empty"_str)));
    }
    auto result = Vec<lito::dependency::CMakeTargetRequirement>::with_capacity(targets.len());
    auto names  = rstd::collections::BTreeMap<String, empty>::make();
    for (usize index {}; index < targets.len(); ++index) {
        auto  item         = path.with_index(index);
        auto& target       = targets[index];
        auto  item_context = rstd::format("CMake target requirement [{}]", index);
        auto  name         = rstd::move(target.name).unwrap();
        if (! cmake_target_is_valid(name.as_str())) {
            return Err(ManifestSchemaError::Data(rstd::serde::Error::invalid_value(
                item.with_field("name"_str), "invalid CMake target name"_str)));
        }
        if (names.contains_key(name.as_str())) {
            return Err(ManifestSchemaError::Data(rstd::serde::Error::invalid_value(
                item.with_field("name"_str), "CMake target is repeated"_str)));
        }
        auto consumption = lito::dependency::DependencyConsumption {};
        auto legacy      = rstd_try(legacy_visibility(target, item_context.as_str()));
        if (legacy.is_some()) {
            if (target.usage.is_some()) {
                return Err(ManifestSchemaError::Domain(
                    rstd::format("{}.visibility cannot be combined with usage", item_context)));
            }
            consumption.is_public = *legacy == LegacyVisibility::Public;
            if (*legacy == LegacyVisibility::LinkOnly) {
                consumption.usage = lito::dependency::DependencyUsage::link_only();
            }
        } else {
            auto usage = rstd_try(
                parse_usage(target.usage, rstd::format("{}.usage", item_context).as_str()));
            if (usage.is_some()) consumption.usage = *usage;
            if (consumption.usage.uses_runtime()) {
                return Err(ManifestSchemaError::Domain(
                    rstd::format("{}.usage does not support runtime", item_context)));
            }
            auto is_public        = target.pub;
            consumption.is_public = is_public.is_some() && *is_public;
        }
        names.insert(name.clone(), empty {});
        result.push(lito::dependency::CMakeTargetRequirement {
            .name        = rstd::move(name),
            .consumption = consumption,
        });
    }
    return Ok(rstd::move(result));
}

auto parse_cmake_host_tools(Option<Vec<wire::CMakeHostTool>> value, const DataPath& owner_path)
    -> ManifestSchemaResult<Vec<lito::dependency::CMakeHostToolRequirement>> {
    auto result = Vec<lito::dependency::CMakeHostToolRequirement>::make();
    if (value.is_none()) return Ok(rstd::move(result));
    auto path  = owner_path.with_field("host-tools"_str);
    auto tools = rstd::move(value).unwrap();
    if (tools.is_empty()) {
        return Err(ManifestSchemaError::Data(
            rstd::serde::Error::invalid_value(rstd::move(path), "must not be empty"_str)));
    }
    auto names = rstd::collections::BTreeMap<String, empty>::make();
    for (usize index {}; index < tools.len(); ++index) {
        auto  item = path.with_index(index);
        auto& wire = tools[index];
        if (! package_name_is_valid(wire.name.as_str())) {
            return Err(ManifestSchemaError::Data(rstd::serde::Error::invalid_value(
                item.with_field("name"_str), "invalid host tool name"_str)));
        }
        if (! cmake_target_is_valid(wire.target.as_str())) {
            return Err(ManifestSchemaError::Data(rstd::serde::Error::invalid_value(
                item.with_field("target"_str), "invalid CMake target name"_str)));
        }
        if (names.contains_key(wire.name.as_str())) {
            return Err(ManifestSchemaError::Data(rstd::serde::Error::invalid_value(
                item.with_field("name"_str), "host tool name is repeated"_str)));
        }
        names.insert(wire.name.clone(), empty {});
        result.push(lito::dependency::CMakeHostToolRequirement {
            .name   = rstd::move(wire.name),
            .target = rstd::move(wire.target),
        });
    }
    return Ok(rstd::move(result));
}

auto parse_cmake_components(Option<Vec<String>> value, ref<str> context)
    -> ManifestSchemaResult<Vec<String>> {
    const auto present    = value.is_some();
    auto       components = rstd::move(value).unwrap_or(Vec<String>::make());
    if (present && components.is_empty()) {
        return Err(
            ManifestSchemaError::Domain(rstd::format("{}.components must not be empty", context)));
    }
    auto names = rstd::collections::BTreeMap<String, empty>::make();
    for (const auto& component : components) {
        if (! lito::dependency::cmake_component_name_is_valid(component.as_str())) {
            return Err(ManifestSchemaError::Domain(rstd::format(
                "{}.components contains unsafe component '{}'", context, component.as_str())));
        }
        if (names.contains_key(component.as_str())) {
            return Err(ManifestSchemaError::Domain(
                rstd::format("{}.components repeats component '{}'", context, component.as_str())));
        }
        names.insert(component.clone(), empty {});
    }
    return Ok(rstd::move(components));
}

auto parse_cmake_external_dependency_definition(wire::CMakeExternalFields& specification,
                                                String                     alias,
                                                ref<str>                   context,
                                                const DataPath&            path)
    -> ManifestSchemaResult<WorkspaceCMakeExternalDependencyDefinition> {
    auto package          = rstd::move(specification.package).unwrap();
    auto components       = parse_cmake_components(rstd::move(specification.components), context);
    auto source           = rstd::move(specification.source);
    auto adapter          = rstd::move(specification.adapter);
    auto config_directory = rstd::move(specification.config_directory);
    auto host_tools       = parse_cmake_host_tools(rstd::move(specification.host_tools), path);
    if (components.is_err()) return Err(rstd::move(components).unwrap_err());
    if (host_tools.is_err()) return Err(rstd::move(host_tools).unwrap_err());
    if (! lito::dependency::cmake_package_name_is_valid(package.as_str())) {
        return Err(ManifestSchemaError::Domain(rstd::format("{}.package is unsafe", context)));
    }
    auto source_value = rstd::move(source);
    if (source_value.is_some() && ! package_name_is_valid(source_value->as_str())) {
        return Err(ManifestSchemaError::Domain(
            rstd::format("{}.source must name a package external source", context)));
    }
    auto cache =
        parse_cmake_cache(rstd::move(specification.cache), "CMake external dependency cache"_str);
    if (cache.is_err()) return Err(rstd::move(cache).unwrap_err());
    if (source_value.is_none() && (! cache->is_empty() || config_directory.is_some())) {
        return Err(ManifestSchemaError::Domain(
            rstd::format("{} cache and config-directory require a source", context)));
    }
    auto adapter_value = rstd::move(adapter);
    if (adapter_value.is_some() && config_directory.is_some()) {
        return Err(ManifestSchemaError::Domain(
            rstd::format("{}.config-directory cannot be combined with adapter", context)));
    }
    auto adapter_path = Option<PathBuf> {};
    if (adapter_value.is_some()) {
        auto parsed = relative_path(rstd::move(adapter_value).unwrap(),
                                    "CMake external dependency adapter"_str);
        if (parsed.is_err()) return Err(rstd::move(parsed).unwrap_err());
        adapter_path = Some(rstd::move(parsed).unwrap());
    }
    auto directory = Option<PathBuf> {};
    if (config_directory.is_some()) {
        auto parsed = install_relative_path(rstd::move(config_directory).unwrap(),
                                            "CMake external config-directory"_str);
        if (parsed.is_err()) return Err(rstd::move(parsed).unwrap_err());
        directory = Some(rstd::move(parsed).unwrap());
    }
    return Ok(WorkspaceCMakeExternalDependencyDefinition {
        .alias            = rstd::move(alias),
        .package          = rstd::move(package),
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

auto parse_cmake_external_dependencies(
    Option<rstd::collections::BTreeMap<String, wire::CMakeExternal<false>>> value)
    -> ManifestSchemaResult<ParsedCMakeExternalDependencies> {
    auto result = ParsedCMakeExternalDependencies {};
    if (value.is_none()) return Ok(rstd::move(result));
    auto table = rstd::move(value).unwrap();
    auto keys  = table.keys();
    for (auto key : keys) {
        const auto& alias   = *key;
        auto        context = rstd::format("CMake external dependency '{}'", alias.as_str());
        auto        path    = DataPath()
                                  .with_field("external-dependencies"_str)
                                  .with_field("cmake"_str)
                                  .with_map_key(alias.as_str());
        if (! package_name_is_valid(alias.as_str())) {
            return Err(ManifestSchemaError::Domain(
                rstd::format("external dependency alias '{}' is invalid", alias.as_str())));
        }
        auto&      specification = *table.get_mut(alias.as_str()).unwrap();
        const auto inherited     = specification.workspace.is_some();
        auto       targets       = parse_cmake_targets(rstd::move(specification.targets), path);
        if (targets.is_err()) return Err(rstd::move(targets).unwrap_err());
        auto condition = rstd_try(parse_external_dependency_condition(
            rstd::move(specification.condition), context.as_str()));
        if (inherited) {
            result.workspace_dependencies.push(WorkspaceCMakeExternalDependencyReference {
                .alias     = alias.clone(),
                .targets   = rstd::move(targets).unwrap(),
                .condition = rstd::move(condition),
            });
            continue;
        }
        auto definition = parse_cmake_external_dependency_definition(
            specification, alias.clone(), context.as_str(), path);
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

auto parse_workspace_cmake_external_dependencies(
    Option<rstd::collections::BTreeMap<String, wire::CMakeExternal<true>>> value)
    -> ManifestSchemaResult<Vec<WorkspaceCMakeExternalDependencyDefinition>> {
    auto result = Vec<WorkspaceCMakeExternalDependencyDefinition>::make();
    if (value.is_none()) return Ok(rstd::move(result));
    auto table = rstd::move(value).unwrap();
    auto keys  = table.keys();
    for (auto key : keys) {
        const auto& alias = *key;
        auto context = rstd::format("workspace CMake external dependency '{}'", alias.as_str());
        auto path    = DataPath()
                           .with_field("workspace"_str)
                           .with_field("external-dependencies"_str)
                           .with_field("cmake"_str)
                           .with_map_key(alias.as_str());
        if (! package_name_is_valid(alias.as_str())) {
            return Err(ManifestSchemaError::Domain(
                rstd::format("external dependency alias '{}' is invalid", alias.as_str())));
        }
        auto& specification = *table.get_mut(alias.as_str()).unwrap();
        auto  definition    = parse_cmake_external_dependency_definition(
            specification, alias.clone(), context.as_str(), path);
        if (definition.is_err()) return Err(rstd::move(definition).unwrap_err());
        result.push(rstd::move(definition).unwrap());
    }
    return Ok(rstd::move(result));
}

auto cargo_feature_is_valid(ref<str> value) -> bool {
    if (value.is_empty() || value.starts_with("-"_str)) return false;
    for (auto byte : value.as_bytes()) {
        const auto character = byte.to_primitive();
        const auto alpha =
            (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z');
        const auto digit = character >= '0' && character <= '9';
        if (! (alpha || digit || character == '_' || character == '-' || character == '+' ||
               character == '.' || character == '/' || character == ':')) {
            return false;
        }
    }
    return true;
}

auto parse_cargo_features(Option<Vec<String>> value, ref<str> context)
    -> ManifestSchemaResult<Vec<String>> {
    auto features = rstd::move(value).unwrap_or(Vec<String>::make());
    auto seen     = rstd::collections::BTreeMap<String, empty>::make();
    for (const auto& feature : features) {
        if (! cargo_feature_is_valid(feature.as_str())) {
            return Err(ManifestSchemaError::Domain(rstd::format(
                "{}.features contains invalid Cargo feature '{}'", context, feature.as_str())));
        }
        if (seen.contains_key(feature.as_str())) {
            return Err(ManifestSchemaError::Domain(
                rstd::format("{}.features repeats Cargo feature '{}'", context, feature.as_str())));
        }
        seen.insert(feature.clone(), empty {});
    }
    rstd::slice_::sort_unstable(features.as_mut_slice().as_mut_ref());
    return Ok(rstd::move(features));
}

auto parse_cargo_manifest_path(Option<String> declared, ref<str> context)
    -> ManifestSchemaResult<PathBuf> {
    auto path = declared.is_some() ? PathBuf::from(rstd::move(declared).unwrap())
                                   : PathBuf::from("Cargo.toml"_str);
    if (! path.as_path().is_relative()) {
        return Err(ManifestSchemaError::Domain(
            rstd::format("{}.manifest-path must be a relative path", context)));
    }
    auto found = false;
    for (auto component : path.as_path().components()) {
        found = true;
        if (! component.is_normal()) {
            return Err(ManifestSchemaError::Domain(
                rstd::format("{}.manifest-path must stay within the external source", context)));
        }
    }
    if (! found) {
        return Err(ManifestSchemaError::Domain(
            rstd::format("{}.manifest-path must not be empty", context)));
    }
    return Ok(rstd::move(path));
}

auto parse_cargo_recipe(wire::CargoExternalFields& specification, ref<str> context)
    -> ManifestSchemaResult<lito::dependency::CargoDependencyRecipe> {
    auto source  = rstd::move(specification.source).unwrap();
    auto package = rstd::move(specification.package).unwrap();
    if (! package_name_is_valid(source.as_str())) {
        return Err(ManifestSchemaError::Domain(
            rstd::format("{}.source must name a package external source", context)));
    }
    if (! package_name_is_valid(package.as_str())) {
        return Err(ManifestSchemaError::Domain(
            rstd::format("{}.package must be a valid Cargo package name", context)));
    }
    return Ok(lito::dependency::CargoDependencyRecipe {
        .package = rstd::move(package),
        .source  = rstd::move(source),
        .manifest_path =
            rstd_try(parse_cargo_manifest_path(rstd::move(specification.manifest_path), context)),
    });
}

auto parse_cargo_consumption(wire::CargoExternalFields& specification, ref<str> context)
    -> ManifestSchemaResult<lito::dependency::CargoDependencyConsumption> {
    auto features = rstd_try(parse_cargo_features(rstd::move(specification.features), context));
    auto default_features = specification.default_features.unwrap_or(true);
    auto profile_name     = rstd::move(specification.profile);
    if (profile_name.is_some() && ! package_name_is_valid(profile_name->as_str())) {
        return Err(ManifestSchemaError::Domain(
            rstd::format("{}.profile must be a valid Cargo profile name", context)));
    }
    auto profile = Option<lito::dependency::CargoProfileName> {};
    if (profile_name.is_some()) {
        profile = Some(lito::dependency::CargoProfileName {
            .value = rstd::move(profile_name).unwrap(),
        });
    }
    auto dependency_consumption = lito::dependency::DependencyConsumption {
        .usage = lito::dependency::DependencyUsage::link_only(),
    };
    auto legacy = rstd_try(legacy_visibility(specification, context));
    if (legacy.is_some()) {
        auto old_usage =
            (specification.usage.is_some() ? Some(specification.usage->values[usize {}].clone())
                                           : Option<String> {});
        if (old_usage.is_some() && old_usage->as_str() != "link"_str &&
            old_usage->as_str() != "runtime"_str) {
            return Err(ManifestSchemaError::Domain(rstd::format(
                "{}.usage must be 'link' or 'runtime' with legacy visibility", context)));
        }
        if (old_usage.is_some() && old_usage->as_str() == "runtime"_str) {
            return Err(ManifestSchemaError::Domain(
                rstd::format("{}.visibility is not accepted when usage is 'runtime'", context)));
        }
        dependency_consumption.is_public = *legacy == LegacyVisibility::Public;
    } else {
        auto usage =
            rstd_try(parse_usage(specification.usage, rstd::format("{}.usage", context).as_str()));
        if (usage.is_some()) dependency_consumption.usage = *usage;
        if (dependency_consumption.usage.uses_compile() ||
            (dependency_consumption.usage.uses_link() &&
             dependency_consumption.usage.uses_runtime())) {
            return Err(ManifestSchemaError::Domain(
                rstd::format("{}.usage must select only 'link' or only 'runtime'", context)));
        }
        auto is_public                   = specification.pub;
        dependency_consumption.is_public = is_public.is_some() && *is_public;
        if (dependency_consumption.usage.uses_runtime() && dependency_consumption.is_public) {
            return Err(ManifestSchemaError::Domain(
                rstd::format("{}.pub must be false when usage is 'runtime'", context)));
        }
    }
    return Ok(lito::dependency::CargoDependencyConsumption {
        .features         = rstd::move(features),
        .default_features = default_features,
        .profile          = rstd::move(profile),
        .dependency       = dependency_consumption,
        .condition        = rstd_try(
            parse_external_dependency_condition(rstd::move(specification.condition), context)),
    });
}

struct ParsedCargoExternalDependencies {
    Vec<lito::dependency::CargoDependencyRequirement> explicit_dependencies;
    Vec<WorkspaceCargoExternalDependencyReference>    workspace_dependencies;
};

auto parse_cargo_external_dependencies(
    Option<rstd::collections::BTreeMap<String, wire::CargoExternal<false>>> value)
    -> ManifestSchemaResult<ParsedCargoExternalDependencies> {
    auto result = ParsedCargoExternalDependencies {};
    if (value.is_none()) return Ok(rstd::move(result));
    auto table = rstd::move(value).unwrap();
    for (auto key : table.keys()) {
        const auto& alias   = *key;
        auto        context = rstd::format("Cargo external dependency '{}'", alias.as_str());
        if (! package_name_is_valid(alias.as_str())) {
            return Err(ManifestSchemaError::Domain(
                rstd::format("external dependency alias '{}' is invalid", alias.as_str())));
        }
        auto&      specification = *table.get_mut(alias.as_str()).unwrap();
        const auto inherited     = specification.workspace.is_some();
        if (inherited) {
            result.workspace_dependencies.push(WorkspaceCargoExternalDependencyReference {
                .alias       = alias.clone(),
                .consumption = rstd_try(parse_cargo_consumption(specification, context.as_str())),
            });
            continue;
        }
        result.explicit_dependencies.push(lito::dependency::CargoDependencyRequirement {
            .alias       = alias.clone(),
            .recipe      = rstd_try(parse_cargo_recipe(specification, context.as_str())),
            .consumption = rstd_try(parse_cargo_consumption(specification, context.as_str())),
        });
    }
    return Ok(rstd::move(result));
}

auto parse_workspace_cargo_external_dependencies(
    Option<rstd::collections::BTreeMap<String, wire::CargoExternal<true>>> value)
    -> ManifestSchemaResult<Vec<WorkspaceCargoExternalDependencyDefinition>> {
    auto result = Vec<WorkspaceCargoExternalDependencyDefinition>::make();
    if (value.is_none()) return Ok(rstd::move(result));
    auto table = rstd::move(value).unwrap();
    for (auto key : table.keys()) {
        const auto& alias = *key;
        auto context = rstd::format("workspace Cargo external dependency '{}'", alias.as_str());
        if (! package_name_is_valid(alias.as_str())) {
            return Err(ManifestSchemaError::Domain(
                rstd::format("external dependency alias '{}' is invalid", alias.as_str())));
        }
        auto& specification = *table.get_mut(alias.as_str()).unwrap();
        result.push(WorkspaceCargoExternalDependencyDefinition {
            .alias  = alias.clone(),
            .recipe = rstd_try(parse_cargo_recipe(specification, context.as_str())),
        });
    }
    return Ok(rstd::move(result));
}

auto parse_external_dependencies(Option<wire::ExternalDependencies<false>> value)
    -> ManifestSchemaResult<ParsedExternalDependencies> {
    auto result = ParsedExternalDependencies {};
    if (value.is_none()) return Ok(rstd::move(result));
    auto pkg_config = parse_pkg_config_external_dependencies(rstd::move(value->pkg_config));
    auto cmake      = parse_cmake_external_dependencies(rstd::move(value->cmake));
    auto cargo      = parse_cargo_external_dependencies(rstd::move(value->cargo));
    if (pkg_config.is_err()) return Err(rstd::move(pkg_config).unwrap_err());
    if (cmake.is_err()) return Err(rstd::move(cmake).unwrap_err());
    if (cargo.is_err()) return Err(rstd::move(cargo).unwrap_err());
    auto parsed_pkg_config      = rstd::move(pkg_config).unwrap();
    auto parsed_cmake           = rstd::move(cmake).unwrap();
    auto parsed_cargo           = rstd::move(cargo).unwrap();
    result.pkg_config           = rstd::move(parsed_pkg_config.explicit_dependencies);
    result.workspace_pkg_config = rstd::move(parsed_pkg_config.workspace_dependencies);
    result.cmake                = rstd::move(parsed_cmake.explicit_dependencies);
    result.workspace_cmake      = rstd::move(parsed_cmake.workspace_dependencies);
    result.cargo                = rstd::move(parsed_cargo.explicit_dependencies);
    result.workspace_cargo      = rstd::move(parsed_cargo.workspace_dependencies);
    return Ok(rstd::move(result));
}

struct ParsedWorkspaceExternalDependencies {
    Vec<WorkspacePkgConfigExternalDependencyDefinition> pkg_config;
    Vec<WorkspaceCMakeExternalDependencyDefinition>     cmake;
    Vec<WorkspaceCargoExternalDependencyDefinition>     cargo;
};

auto parse_workspace_external_dependencies(Option<wire::ExternalDependencies<true>> value)
    -> ManifestSchemaResult<ParsedWorkspaceExternalDependencies> {
    auto result = ParsedWorkspaceExternalDependencies {};
    if (value.is_none()) return Ok(rstd::move(result));
    auto pkg_config =
        parse_workspace_pkg_config_external_dependencies(rstd::move(value->pkg_config));
    auto cmake = parse_workspace_cmake_external_dependencies(rstd::move(value->cmake));
    auto cargo = parse_workspace_cargo_external_dependencies(rstd::move(value->cargo));
    if (pkg_config.is_err()) return Err(rstd::move(pkg_config).unwrap_err());
    if (cmake.is_err()) return Err(rstd::move(cmake).unwrap_err());
    if (cargo.is_err()) return Err(rstd::move(cargo).unwrap_err());
    result.pkg_config = rstd::move(pkg_config).unwrap();
    result.cmake      = rstd::move(cmake).unwrap();
    result.cargo      = rstd::move(cargo).unwrap();
    return Ok(rstd::move(result));
}
