module;
#include <rstd/macro.hpp>

export module lito.core:registry.inspection;

import rstd;
import :manifest.document;
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

struct VerifiedRegistrySourceCandidate {
    RegistryPackageId                 package;
    SemanticVersion                   version;
    Vec<RegistryDependencyProjection> dependencies;
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
