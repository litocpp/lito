module;
#include <rstd/enum.hpp>

export module lito.driver:install.plan;

import rstd;
import lito.core;
import :install.error;
import :install.entry;
import :install.store.model;
import :install.materialize_error;
import :install.recipe;
import :install.package;
import :build.artifact;
import :build.result;
import :dependency.preparation;
import :dependency.cmake;
import lito.cpp;
import :install.source;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito
{

struct InstallPlan {
    Vec<InstallPackageRecord> packages;
};

} // namespace lito

namespace lito
{

template<typename T>
auto materialize_failure(String message) -> InstallMaterializeResult<T> {
    return Err(InstallMaterializeError::Message(rstd::move(message)));
}

auto source_file(const InstallRecipe& recipe, ref<rstd::path::Path> relative)
    -> InstallMaterializeResult<PathBuf> {
    auto requested = recipe.root.join(relative);
    auto metadata  = rstd::fs::symlink_metadata(requested.as_path());
    if (metadata.is_err()) {
        return materialize_failure<PathBuf>(rstd::format("cannot inspect package file '{}': {}",
                                                         requested.as_path(),
                                                         rstd::move(metadata).unwrap_err()));
    }
    if (! metadata->is_file() || metadata->is_symlink()) {
        return materialize_failure<PathBuf>(rstd::format(
            "package file '{}' is not a regular non-symlink file", requested.as_path()));
    }
    auto canonical = rstd::fs::canonicalize(requested.as_path());
    if (canonical.is_err()) {
        return materialize_failure<PathBuf>(rstd::format("cannot resolve package file '{}': {}",
                                                         requested.as_path(),
                                                         rstd::move(canonical).unwrap_err()));
    }
    if (canonical->as_path().strip_prefix(recipe.root.as_path()).is_none()) {
        return materialize_failure<PathBuf>(
            rstd::format("package file '{}' escapes package root", requested.as_path()));
    }
    return Ok(rstd::move(canonical).unwrap());
}

auto append_entry(Vec<InstallEntry>& entries, InstallEntry entry)
    -> InstallMaterializeResult<empty> {
    for (const auto& prior : entries) {
        if (prior.relative_destination.as_path() == entry.relative_destination.as_path()) {
            return materialize_failure<empty>(
                rstd::format("install destination '{}' is declared more than once",
                             entry.relative_destination.as_path()));
        }
    }
    entries.push(rstd::move(entry));
    return Ok(empty {});
}

auto artifact_for(const BuildSummary&                   summary,
                  const InstallBuildRequirements&       requirements,
                  const lito::package::PackageTargetId& target)
    -> InstallMaterializeResult<const BuiltArtifact*> {
    const RequestedArtifactLinkVariant* expected = nullptr;
    for (const auto& variant : requirements.artifact_link_variants) {
        if (variant.target != target) continue;
        if (expected != nullptr) {
            return materialize_failure<const BuiltArtifact*>(
                rstd::format("install requirements repeat link variant for '{}'",
                             lito::package::package_target_id_text(target)));
        }
        expected = rstd::addressof(variant);
    }
    const BuiltArtifact* result = nullptr;
    for (const auto& artifact : summary.artifacts) {
        if (artifact.target != target) continue;
        if (expected == nullptr && artifact.install_link.is_some()) continue;
        if (expected != nullptr &&
            (artifact.install_link.is_none() ||
             artifact.install_link->identity != expected->policy.identity.as_str()))
            continue;
        if (result != nullptr) {
            return materialize_failure<const BuiltArtifact*>(
                rstd::format("build returned duplicate artifact for '{}'",
                             lito::package::package_target_id_text(target)));
        }
        result = rstd::addressof(artifact);
    }
    if (result == nullptr || result->kind != cpp::ArtifactKind::Executable) {
        return materialize_failure<const BuiltArtifact*>(
            rstd::format("build did not return an executable artifact for '{}'",
                         lito::package::package_target_id_text(target)));
    }
    return Ok(result);
}

auto relative_text(ref<rstd::path::Path> destination, ref<rstd::path::Path> base) -> String {
    if (base.is_empty()) return destination.to_string_lossy();
    auto relative = rstd::path::lexically_relative(base, destination);
    return relative.is_some() ? relative->as_path().to_string_lossy()
                              : destination.to_string_lossy();
}

auto install_path_less(ref<rstd::path::Path> left, ref<rstd::path::Path> right) -> bool {
    return left.to_string_lossy() < right.to_string_lossy();
}

auto sort_entries(Vec<InstallEntry>& entries) -> void {
    rstd::slice_::sort_unstable_by(entries.as_mut_slice().as_mut_ref(),
                                   [](const InstallEntry& left, const InstallEntry& right) {
                                       return install_path_less(
                                           left.relative_destination.as_path(),
                                           right.relative_destination.as_path());
                                   });
}

} // namespace lito

export namespace lito
{

auto materialize_install_plan(Vec<InstallRecipe>              recipes,
                              const InstallBuildRequirements& requirements,
                              const BuildSummary&             build,
                              ref<str>                        profile,
                              ref<str> target) -> InstallMaterializeResult<InstallPlan> {
    auto plan = InstallPlan {};
    for (auto& recipe : recipes) {
        auto entries = Vec<InstallEntry>::make();
        for (const auto& file : recipe.files) {
            auto source = rstd_try(source_file(recipe, file.source.as_path()));
            rstd_try(append_entry(entries,
                                  InstallEntry {
                                      .origin = InstallEntryOrigin::PackageFile(
                                          recipe.owner.clone(), file.source.clone()),
                                      .payload = InstallEntryPayload::CopyFile(rstd::move(source)),
                                      .relative_destination = file.destination.clone(),
                                  }));
        }
        for (const auto& artifact : recipe.artifacts) {
            auto built      = rstd_try(artifact_for(build, requirements, artifact.target));
            auto production = Option<InstallLinkProduction> {};
            if (built->install_link.is_some()) {
                production = Some(InstallLinkProduction {
                    .variant_identity = built->install_link->identity.clone(),
                    .link_identity    = built->link_identity.clone(),
                    .runtime_search   = built->install_link->runtime_search.clone(),
                });
            }
            rstd_try(append_entry(
                entries,
                InstallEntry {
                    .origin          = InstallEntryOrigin::BuildArtifact(artifact.target.clone()),
                    .payload         = InstallEntryPayload::CopyFile(built->path.clone()),
                    .link_production = rstd::move(production),
                    .relative_destination = artifact.destination.clone(),
                }));
        }
        for (const auto& requested : recipe.external_assets) {
            auto set = build.external_assets.resolve(requested.dependency.as_str(),
                                                     requested.set.as_str());
            if (set.is_err()) {
                return materialize_failure<InstallPlan>(rstd::move(set).unwrap_err());
            }
            const auto* resolved_set = *set;
            if (resolved_set->disposition == ExternalAssetDisposition::Provided) continue;
            auto strip_matches = Vec<usize>::make();
            if (requested.strip.is_some()) {
                strip_matches.reserve(requested.strip->files.len());
                for (usize index {}; index < requested.strip->files.len(); ++index) {
                    strip_matches.push(usize {});
                }
            }
            for (const auto& asset : resolved_set->entries) {
                auto destination = requested.destination.join(asset.logical_path.as_path());
                auto transforms  = Vec<InstallEntryTransform>::make();
                if (requested.strip.is_some()) {
                    for (usize index {}; index < requested.strip->files.len(); ++index) {
                        if (requested.strip->files[index].as_path() != asset.logical_path.as_path())
                            continue;
                        ++strip_matches[index];
                        transforms.push(InstallEntryTransform::Strip(requested.strip->mode));
                    }
                }
                rstd_try(append_entry(
                    entries,
                    InstallEntry {
                        .origin  = InstallEntryOrigin::ExternalAsset(requested.dependency.clone(),
                                                                     requested.set.clone(),
                                                                     asset.logical_path.clone()),
                        .payload = InstallEntryPayload::CopyFile(asset.source.clone()),
                        .transforms           = rstd::move(transforms),
                        .relative_destination = rstd::move(destination),
                    }));
            }
            if (requested.strip.is_some()) {
                for (usize index {}; index < strip_matches.len(); ++index) {
                    if (strip_matches[index] == usize(1)) continue;
                    return materialize_failure<InstallPlan>(rstd::format(
                        "external asset strip path '{}:{}:{}' matched {} catalog entries",
                        requested.dependency.as_str(),
                        requested.set.as_str(),
                        requested.strip->files[index].as_path(),
                        strip_matches[index]));
                }
            }
        }
        for (const auto& configured : recipe.templates) {
            auto input    = rstd_try(source_file(recipe, configured.input.as_path()));
            auto contents = rstd::fs::read_to_string(input.as_path());
            if (contents.is_err()) {
                return materialize_failure<InstallPlan>(
                    rstd::format("cannot read install template '{}': {}",
                                 input.as_path(),
                                 rstd::move(contents).unwrap_err()));
            }
            auto rendered =
                render_configure_template(contents->as_str(), configured.values, input.as_path());
            if (rendered.is_err()) {
                return materialize_failure<InstallPlan>(
                    rstd::format("cannot render install template '{}': {}",
                                 input.as_path(),
                                 rstd::move(rendered).unwrap_err()));
            }
            auto bytes = Vec<u8>::with_capacity(rendered->len());
            for (auto byte : rendered->as_str().as_bytes()) bytes.push(rstd::move(byte));
            rstd_try(append_entry(
                entries,
                InstallEntry {
                    .origin  = InstallEntryOrigin::Template(configured.input.clone()),
                    .payload = InstallEntryPayload::Bytes(rstd::move(bytes), u32(0644)),
                    .relative_destination = configured.destination.clone(),
                }));
        }
        sort_entries(entries);
        for (const auto& inventory : recipe.inventories) {
            auto text = String::make();
            for (const auto& entry : entries) {
                text.push_str(relative_text(entry.relative_destination.as_path(),
                                            inventory.relative_to.as_path())
                                  .as_str());
                text.push_ascii('\n');
            }
            auto bytes = Vec<u8>::with_capacity(text.len());
            for (auto byte : text.as_str().as_bytes()) bytes.push(rstd::move(byte));
            rstd_try(append_entry(
                entries,
                InstallEntry {
                    .origin  = InstallEntryOrigin::Inventory(),
                    .payload = InstallEntryPayload::Bytes(rstd::move(bytes), u32(0644)),
                    .relative_destination = inventory.destination.clone(),
                }));
        }
        sort_entries(entries);
        auto provenance = install_source_provenance(recipe.source);
        if (provenance.is_err()) {
            return materialize_failure<InstallPlan>(
                rstd::format("package '{}' install source is invalid: {}",
                             recipe.owner.as_str(),
                             rstd::move(provenance).unwrap_err()));
        }
        plan.packages.push(InstallPackageRecord {
            .name                 = rstd::move(recipe.owner),
            .version              = rstd::move(recipe.version),
            .profile              = String::make(profile),
            .target               = String::make(target),
            .entries              = rstd::move(entries),
            .provenance           = rstd::move(provenance).unwrap(),
            .runtime_dependencies = rstd::move(recipe.runtime_dependencies),
        });
    }
    return Ok(rstd::move(plan));
}

} // namespace lito
