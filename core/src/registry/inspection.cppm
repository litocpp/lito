module;
#include <rstd/macro.hpp>

export module lito.core:registry.inspection;

import rstd;
import licrypto;
import :manifest.document;
import :parse.value;
import :registry.artifact;
import :registry.identity;
import :registry.metadata;
import :registry.version;
import :source.requirement;
import :source.tree;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::registry
{

struct RegistryReadmeMetadata {
    String                 path;
    String                 contents;
    licrypto::Sha256Digest checksum;
    u64                    size {};
};

struct RegistryPackageMetadata {
    Vec<String>                    authors;
    Option<String>                 license;
    Option<String>                 description;
    Option<String>                 repository;
    Option<String>                 documentation;
    Option<RegistryReadmeMetadata> readme;
};

struct VerifiedRegistrySourceCandidate {
    RegistryPackageId                 package;
    SemanticVersion                   version;
    Vec<RegistryDependencyProjection> dependencies;
    RegistryPackageMetadata           metadata;
    lito::manifest::PackageManifest   manifest;
    usize                             file_count {};
    u64                               unpacked_size {};
};

auto inspect_registry_source_tree(const lito::source::SourceTree& tree,
                                  const RegistryPackageId&        expected_package,
                                  const SemanticVersion&          expected_version)
    -> RegistryArtifactResult<VerifiedRegistrySourceCandidate>;
auto inspect_registry_source_tree_at(const lito::source::SourceTree& tree,
                                     const RegistryPackageId&        expected_package,
                                     const SemanticVersion&          expected_version,
                                     ref<rstd::path::Path>           manifest_root)
    -> RegistryArtifactResult<VerifiedRegistrySourceCandidate>;

auto registry_dependencies_match(slice<RegistryDependencyProjection> left,
                                 slice<RegistryDependencyProjection> right) noexcept -> bool;
auto registry_candidate_has_external_inputs(
    const VerifiedRegistrySourceCandidate& candidate) noexcept -> bool;

} // namespace lito::registry

namespace
{

using namespace lito::registry;

template<typename T>
auto inspection_failure(
    RegistryArtifactErrorKind   kind,
    const RegistryPackageId&    package,
    String                      message,
    RegistryArtifactFailureCode code = RegistryArtifactFailureCode::PackageInvalid)
    -> RegistryArtifactResult<T> {
    return Err(RegistryArtifactError {
        .kind    = kind,
        .code    = code,
        .package = package.clone(),
        .message = rstd::move(message),
    });
}

template<typename RegistrySource>
auto dependency_registry(const RegistrySource& source, const RegistryPackageId& owner)
    -> RegistryArtifactResult<RegistryId> {
    if (source.registry.is_none()) return Ok(owner.registry.clone());
    auto parsed = RegistryId::parse(source.registry->as_str());
    if (parsed.is_err()) {
        return inspection_failure<RegistryId>(
            RegistryArtifactErrorKind::Manifest,
            owner,
            rstd::format("published dependency registry '{}' must be a canonical Registry identity",
                         source.registry->as_str()));
    }
    return Ok(rstd::move(parsed).unwrap());
}

auto append_dependency(Vec<RegistryDependencyProjection>&        output,
                       const lito::manifest::DeclaredDependency& dependency,
                       RegistryDependencyKind                    kind,
                       const RegistryPackageId& owner) -> RegistryArtifactResult<empty> {
    if (! dependency.source.resolution.is_Registry()) {
        return inspection_failure<empty>(
            RegistryArtifactErrorKind::Manifest,
            owner,
            rstd::format("published dependency '{}' must use a Registry source",
                         dependency.name.as_str()));
    }
    const auto& source = dependency.source.resolution.as_Registry();
    auto        features =
        dependency.features.is_some() ? dependency.features->clone() : Vec<String>::make();
    output.push(RegistryDependencyProjection {
        .alias = dependency.name.clone(),
        .package =
            RegistryPackageId {
                .registry = rstd_try(dependency_registry(source, owner)),
                .name     = source.package.clone(),
            },
        .requirement = source.requirement.clone(),
        .kind        = kind,
        .visibility  = dependency.visibility.is_some()
                           ? *dependency.visibility
                           : lito::dependency::DependencyVisibility::Private,
        .features    = rstd::move(features),
        .default_features =
            dependency.default_features.is_some() ? *dependency.default_features : true,
    });
    return Ok(empty {});
}

auto append_runtime_dependency(Vec<RegistryDependencyProjection>&               output,
                               const lito::manifest::DeclaredRuntimeDependency& dependency,
                               const RegistryPackageId& owner) -> RegistryArtifactResult<empty> {
    if (! dependency.source.resolution.is_Registry()) {
        return inspection_failure<empty>(
            RegistryArtifactErrorKind::Manifest,
            owner,
            rstd::format("published runtime dependency '{}' must use a Registry source",
                         dependency.name.as_str()));
    }
    const auto& source = dependency.source.resolution.as_Registry();
    output.push(RegistryDependencyProjection {
        .alias = dependency.name.clone(),
        .package =
            RegistryPackageId {
                .registry = rstd_try(dependency_registry(source, owner)),
                .name     = source.package.clone(),
            },
        .requirement = source.requirement.clone(),
        .kind        = RegistryDependencyKind::Runtime,
        .visibility  = lito::dependency::DependencyVisibility::Private,
    });
    return Ok(empty {});
}

auto dependency_kind_rank(RegistryDependencyKind kind) noexcept -> u8 {
    switch (kind) {
    case RegistryDependencyKind::Normal: return u8 {};
    case RegistryDependencyKind::Development: return u8(1);
    case RegistryDependencyKind::Runtime: return u8(2);
    }
    return u8(3);
}

auto dependency_less(const RegistryDependencyProjection& left,
                     const RegistryDependencyProjection& right) noexcept -> bool {
    if (left.alias != right.alias) return left.alias < right.alias;
    if (left.kind != right.kind)
        return dependency_kind_rank(left.kind) < dependency_kind_rank(right.kind);
    return registry_package_id_text(left.package) < registry_package_id_text(right.package);
}

auto projection_equal(const RegistryDependencyProjection& left,
                      const RegistryDependencyProjection& right) noexcept -> bool {
    if (left.alias != right.alias || ! (left.package == right.package) ||
        left.requirement.text() != right.requirement.text() || left.kind != right.kind ||
        left.visibility != right.visibility || left.default_features != right.default_features ||
        left.features.len() != right.features.len()) {
        return false;
    }
    for (usize index {}; index < left.features.len(); ++index) {
        if (left.features[index] != right.features[index]) return false;
    }
    return true;
}

auto validate_description(const lito::manifest::PackageMetadata& value,
                          const RegistryPackageId&               package)
    -> RegistryArtifactResult<Option<String>> {
    if (value.source == lito::manifest::PackageMetadataSource::Workspace) {
        return inspection_failure<Option<String>>(
            RegistryArtifactErrorKind::Manifest,
            package,
            String::make("standalone Registry manifest must not inherit package.description"_str));
    }
    if (value.value.is_none()) return Ok(None());
    if (value.value->as_str().trim_ascii().is_empty()) {
        return inspection_failure<Option<String>>(
            RegistryArtifactErrorKind::Manifest,
            package,
            String::make("package.description must contain non-whitespace text"_str));
    }
    if (value.value->len() > usize(512)) {
        return inspection_failure<Option<String>>(
            RegistryArtifactErrorKind::Manifest,
            package,
            String::make("package.description must not exceed 512 UTF-8 bytes"_str));
    }
    for (auto byte : value.value->as_str().as_bytes()) {
        const auto raw = byte.to_primitive();
        if (raw < 0x20 || raw == 0x7f) {
            return inspection_failure<Option<String>>(
                RegistryArtifactErrorKind::Manifest,
                package,
                String::make("package.description must not contain control characters"_str));
        }
    }
    return Ok(Some(value.value->clone()));
}

auto validate_metadata_url(const lito::manifest::PackageMetadata& value,
                           ref<str>                               key,
                           const RegistryPackageId&               package)
    -> RegistryArtifactResult<Option<String>> {
    if (value.source == lito::manifest::PackageMetadataSource::Workspace) {
        return inspection_failure<Option<String>>(
            RegistryArtifactErrorKind::Manifest,
            package,
            rstd::format("standalone Registry manifest must not inherit package.{}", key));
    }
    if (value.value.is_none()) return Ok(None());
    if (value.value->len() > usize(2048)) {
        return inspection_failure<Option<String>>(
            RegistryArtifactErrorKind::Manifest,
            package,
            rstd::format("package.{} must not exceed 2048 UTF-8 bytes", key));
    }
    auto parsed = lito::parse::HttpsUrl::parse(value.value->as_str());
    if (parsed.is_err() || parsed->as_str() != value.value->as_str() ||
        parsed->url()->authority().contains("@"_str)) {
        return inspection_failure<Option<String>>(
            RegistryArtifactErrorKind::Manifest,
            package,
            rstd::format("package.{} must be a canonical HTTPS URL without credentials", key));
    }
    return Ok(Some(value.value->clone()));
}

auto inspect_readme(const lito::manifest::PackageManifest& manifest,
                    const lito::source::SourceTree&        tree,
                    const RegistryPackageId&               package)
    -> RegistryArtifactResult<Option<RegistryReadmeMetadata>> {
    if (manifest.readme.source == lito::manifest::PackageReadmeSource::Workspace) {
        return inspection_failure<Option<RegistryReadmeMetadata>>(
            RegistryArtifactErrorKind::Manifest,
            package,
            String::make("standalone Registry manifest must not inherit package.readme"_str));
    }
    if (manifest.readme.archive_path.is_none()) return Ok(None());
    const lito::source::SourceTreeEntry* readme = nullptr;
    for (const auto& entry : tree.entries()) {
        if (entry.kind() == lito::source::SourceEntryKind::File &&
            entry.path().as_str() == manifest.readme.archive_path->as_str()) {
            readme = rstd::addressof(entry);
            break;
        }
    }
    if (readme == nullptr) {
        return inspection_failure<Option<RegistryReadmeMetadata>>(
            RegistryArtifactErrorKind::Manifest,
            package,
            rstd::format("package.readme '{}' is missing from the Registry archive",
                         manifest.readme.archive_path->as_str()));
    }
    if (readme->contents().len() > usize(256) * usize(1024)) {
        return inspection_failure<Option<RegistryReadmeMetadata>>(
            RegistryArtifactErrorKind::Manifest,
            package,
            String::make("package.readme must not exceed 256 KiB"_str));
    }
    auto contents = String::from_utf8(Vec<u8>::from(readme->contents()));
    if (contents.is_err()) {
        return inspection_failure<Option<RegistryReadmeMetadata>>(
            RegistryArtifactErrorKind::Manifest,
            package,
            String::make("package.readme must contain valid UTF-8"_str));
    }
    auto size = as_cast<u64>(readme->contents().len());
    return Ok(Some(RegistryReadmeMetadata {
        .path     = String::make(readme->path().as_str()),
        .contents = rstd::move(contents).unwrap(),
        .checksum = licrypto::sha256_digest(readme->contents()),
        .size     = size,
    }));
}

auto inspect_package_metadata(const lito::manifest::PackageManifest& manifest,
                              const lito::source::SourceTree&        tree,
                              const RegistryPackageId&               package)
    -> RegistryArtifactResult<RegistryPackageMetadata> {
    if (manifest.authors.source == lito::manifest::PackageAuthorsSource::Workspace ||
        manifest.license.source == lito::manifest::PackageLicenseSource::Workspace) {
        return inspection_failure<RegistryPackageMetadata>(
            RegistryArtifactErrorKind::Manifest,
            package,
            String::make("standalone Registry manifest must not inherit package metadata"_str));
    }
    return Ok(RegistryPackageMetadata {
        .authors     = manifest.authors.values.clone(),
        .license     = manifest.license.value.clone(),
        .description = rstd_try(validate_description(manifest.description, package)),
        .repository =
            rstd_try(validate_metadata_url(manifest.repository, "repository"_str, package)),
        .documentation =
            rstd_try(validate_metadata_url(manifest.documentation, "documentation"_str, package)),
        .readme = rstd_try(inspect_readme(manifest, tree, package)),
    });
}

} // namespace

auto inspect_registry_source_tree_impl(const lito::source::SourceTree& tree,
                                       const RegistryPackageId&        expected_package,
                                       const SemanticVersion&          expected_version,
                                       Option<ref<rstd::path::Path>>   manifest_root)
    -> RegistryArtifactResult<VerifiedRegistrySourceCandidate> {
    const lito::source::SourceTreeEntry* manifest_entry = nullptr;
    auto                                 file_count     = usize {};
    auto                                 unpacked_size  = u64 {};
    for (const auto& entry : tree.entries()) {
        if (entry.kind() != lito::source::SourceEntryKind::File) continue;
        ++file_count;
        auto length = as_cast<u64>(entry.contents().len());
        if (unpacked_size > u64::MAX - length) {
            return inspection_failure<VerifiedRegistrySourceCandidate>(
                RegistryArtifactErrorKind::Archive,
                expected_package,
                String::make("Registry source size exceeds the protocol range"_str));
        }
        unpacked_size += length;
        if (entry.path().as_str() == "lito.toml"_str) manifest_entry = rstd::addressof(entry);
    }
    if (manifest_entry == nullptr) {
        return inspection_failure<VerifiedRegistrySourceCandidate>(
            RegistryArtifactErrorKind::Manifest,
            expected_package,
            String::make("Registry source has no standalone lito.toml"_str));
    }
    auto source_identity = rstd::format("registry:{}:{}:{}",
                                        expected_package.registry.as_str(),
                                        expected_package.name.as_str(),
                                        expected_version.text().as_str());
    auto loaded          = manifest_root.is_some()
                               ? lito::manifest::load_package_manifest_from_source_tree_at(
                                     source_identity.as_str(), *manifest_root, tree)
                               : lito::manifest::load_package_manifest_from_source_tree(
                                     source_identity.as_str(), tree);
    if (loaded.is_err()) {
        auto error   = rstd::move(loaded).unwrap_err();
        auto message = rstd::format("cannot parse standalone Registry manifest: {}", error);
        auto source  = as<rstd::error::Error>(error).source();
        while (source.is_some()) {
            message.push_str(rstd::format(": {}", *source).as_str());
            source = (*source)->source();
        }
        return inspection_failure<VerifiedRegistrySourceCandidate>(
            RegistryArtifactErrorKind::Manifest, expected_package, rstd::move(message));
    }
    auto manifest = rstd::move(loaded).unwrap();
    if (manifest.name.as_str() != expected_package.name.as_str() ||
        manifest.version.source != lito::manifest::PackageVersionSource::Explicit ||
        manifest.version.value.is_none()) {
        return inspection_failure<VerifiedRegistrySourceCandidate>(
            RegistryArtifactErrorKind::Manifest,
            expected_package,
            String::make("standalone Registry manifest package name/version does not match"_str));
    }
    auto parsed_version = SemanticVersion::parse(manifest.version.value->as_str());
    if (parsed_version.is_err() || ! (*parsed_version == expected_version)) {
        return inspection_failure<VerifiedRegistrySourceCandidate>(
            RegistryArtifactErrorKind::Manifest,
            expected_package,
            String::make("standalone Registry manifest version is not the requested SemVer"_str));
    }
    if (! manifest.workspace_dependencies.is_empty() ||
        ! manifest.workspace_dev_dependencies.is_empty() ||
        ! manifest.workspace_runtime_dependencies.is_empty()) {
        return inspection_failure<VerifiedRegistrySourceCandidate>(
            RegistryArtifactErrorKind::Manifest,
            expected_package,
            String::make(
                "standalone Registry manifest must not inherit workspace dependencies"_str));
    }
    auto dependencies = Vec<RegistryDependencyProjection>::make();
    for (const auto& dependency : manifest.dependencies) {
        rstd_try(append_dependency(
            dependencies, dependency, RegistryDependencyKind::Normal, expected_package));
    }
    for (const auto& dependency : manifest.dev_dependencies) {
        rstd_try(append_dependency(
            dependencies, dependency, RegistryDependencyKind::Development, expected_package));
    }
    for (const auto& dependency : manifest.runtime_dependencies) {
        rstd_try(append_runtime_dependency(dependencies, dependency, expected_package));
    }
    rstd::slice_::sort_unstable_by(dependencies.as_mut_slice().as_mut_ref(), dependency_less);
    for (usize index = usize(1); index < dependencies.len(); ++index) {
        if (dependencies[index - usize(1)].alias == dependencies[index].alias) {
            return inspection_failure<VerifiedRegistrySourceCandidate>(
                RegistryArtifactErrorKind::Manifest,
                expected_package,
                rstd::format("standalone Registry manifest repeats dependency alias '{}'",
                             dependencies[index].alias.as_str()));
        }
    }
    return Ok(VerifiedRegistrySourceCandidate {
        .package       = expected_package.clone(),
        .version       = expected_version.clone(),
        .dependencies  = rstd::move(dependencies),
        .metadata      = rstd_try(inspect_package_metadata(manifest, tree, expected_package)),
        .manifest      = rstd::move(manifest),
        .file_count    = file_count,
        .unpacked_size = unpacked_size,
    });
}

auto lito::registry::inspect_registry_source_tree(const lito::source::SourceTree& tree,
                                                  const RegistryPackageId&        expected_package,
                                                  const SemanticVersion&          expected_version)
    -> RegistryArtifactResult<VerifiedRegistrySourceCandidate> {
    return inspect_registry_source_tree_impl(tree, expected_package, expected_version, None());
}

auto lito::registry::inspect_registry_source_tree_at(const lito::source::SourceTree& tree,
                                                     const RegistryPackageId& expected_package,
                                                     const SemanticVersion&   expected_version,
                                                     ref<rstd::path::Path>    manifest_root)
    -> RegistryArtifactResult<VerifiedRegistrySourceCandidate> {
    return inspect_registry_source_tree_impl(
        tree, expected_package, expected_version, Some(manifest_root));
}

auto lito::registry::registry_dependencies_match(slice<RegistryDependencyProjection> left,
                                                 slice<RegistryDependencyProjection> right) noexcept
    -> bool {
    if (left.len() != right.len()) return false;
    auto left_sorted  = Vec<RegistryDependencyProjection>::with_capacity(left.len());
    auto right_sorted = Vec<RegistryDependencyProjection>::with_capacity(right.len());
    for (const auto& dependency : left) left_sorted.push(dependency.clone());
    for (const auto& dependency : right) right_sorted.push(dependency.clone());
    rstd::slice_::sort_unstable_by(left_sorted.as_mut_slice().as_mut_ref(), dependency_less);
    rstd::slice_::sort_unstable_by(right_sorted.as_mut_slice().as_mut_ref(), dependency_less);
    for (usize index {}; index < left_sorted.len(); ++index) {
        if (! projection_equal(left_sorted[index], right_sorted[index])) return false;
    }
    return true;
}

auto lito::registry::registry_candidate_has_external_inputs(
    const VerifiedRegistrySourceCandidate& candidate) noexcept -> bool {
    const auto& manifest = candidate.manifest;
    if (! manifest.external_sources.is_empty() ||
        ! manifest.pkg_config_external_dependencies.is_empty() ||
        ! manifest.workspace_pkg_config_external_dependencies.is_empty() ||
        ! manifest.cmake_external_dependencies.is_empty() ||
        ! manifest.workspace_cmake_external_dependencies.is_empty() ||
        ! manifest.cargo_external_dependencies.is_empty() ||
        ! manifest.workspace_cargo_external_dependencies.is_empty()) {
        return true;
    }
    for (const auto& group : manifest.source_groups) {
        if (group.external_source.is_some()) return true;
    }
    return false;
}
