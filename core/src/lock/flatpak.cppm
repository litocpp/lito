module;
#include <rstd/macro.hpp>

export module lito.core:lock.flatpak;

import rstd;
import lito.system;
import :flatpak;
import :lock;
import :lock.config;
import :lock.document;
import :source.fetch;
import :source.bundle;

using namespace rstd::prelude;
using namespace lito::system;
using PathBuf = rstd::path::PathBuf;
using namespace rstd::literals;
using namespace lito::lock;

export namespace lito::lock
{

enum class LockExportFormat
{
    FlatpakSources,
};

struct RegistryFlatpakArchiveProvider {
    void* context {};
    LockResult<String> (*download_url)(void*, const lito::registry::RegistryReleaseId&) noexcept {};
};

constexpr auto lock_export_format_name(LockExportFormat format) noexcept -> ref<str> {
    switch (format) {
    case LockExportFormat::FlatpakSources: return "flatpak-sources"_str;
    }
    rstd::unreachable();
}

auto parse_lock_export_format(ref<str> value) noexcept -> Option<LockExportFormat> {
    if (value == lock_export_format_name(LockExportFormat::FlatpakSources)) {
        return Some(LockExportFormat::FlatpakSources);
    }
    return None();
}

auto lock_export_format_names() -> Vec<String> {
    auto values = Vec<String>::with_capacity(usize(1));
    values.push(String::make(lock_export_format_name(LockExportFormat::FlatpakSources)));
    return values;
}

} // namespace lito::lock

template<typename T>
auto lock_flatpak_failure(String message) -> LockResult<T> {
    return Err(LockError::Schema(rstd::move(message)));
}

auto lock_flatpak_failure(lito::flatpak::Error error) -> LockError {
    return LockError::Schema(rstd::format("Flatpak source export failed: {}", error));
}

struct FlatpakCandidate {
    lito::source::FetchIdentity identity;
    Vec<Architecture>           architectures;
    Vec<String>                 owners;
    bool                        all_architectures { false };
};

struct RegistryFlatpakCandidate {
    lito::registry::RegistryReleasePin pin;
    Vec<String>                        owners;
};

auto candidate_owners(const FlatpakCandidate& candidate) -> String {
    auto result = String::make();
    for (const auto& owner : candidate.owners) {
        if (! result.is_empty()) result.push_str(", "_str);
        result.push_str(owner.as_str());
    }
    return result;
}

auto candidate_owners(const RegistryFlatpakCandidate& candidate) -> String {
    auto result = String::make();
    for (const auto& owner : candidate.owners) {
        if (! result.is_empty()) result.push_str(", "_str);
        result.push_str(owner.as_str());
    }
    return result;
}

auto merge_architectures(FlatpakCandidate& candidate, const Vec<Architecture>& architectures)
    -> LockResult<empty> {
    if (architectures.is_empty()) {
        candidate.all_architectures = true;
        candidate.architectures.clear();
        return Ok(empty {});
    }
    if (candidate.all_architectures) return Ok(empty {});
    for (const auto& architecture : architectures) {
        if (architecture != Architecture::X86_64 && architecture != Architecture::Aarch64) {
            auto owners = candidate_owners(candidate);
            return lock_flatpak_failure<empty>(
                rstd::format("Flatpak source export does not support architecture '{}' for {}",
                             architecture_name(architecture),
                             owners.as_str()));
        }
        auto duplicate = false;
        for (const auto& current : candidate.architectures) {
            if (current == architecture) duplicate = true;
        }
        if (! duplicate) candidate.architectures.push(Architecture(architecture));
    }
    rstd::slice_::sort_unstable(candidate.architectures.as_mut_slice().as_mut_ref());
    return Ok(empty {});
}

auto add_candidate(rstd::collections::BTreeMap<String, FlatpakCandidate>& candidates,
                   lito::source::FetchIdentity                            identity,
                   const Vec<Architecture>&                               architectures,
                   String owner) -> LockResult<empty> {
    auto key      = lito::source::fetch_identity_text(identity);
    auto existing = candidates.get_mut(key.as_str());
    if (existing.is_some()) {
        auto duplicate = false;
        for (const auto& current : (**existing).owners) {
            if (current == owner) duplicate = true;
        }
        if (! duplicate) {
            (**existing).owners.push(rstd::move(owner));
            rstd::slice_::sort_unstable((**existing).owners.as_mut_slice().as_mut_ref());
        }
        return merge_architectures(**existing, architectures);
    }
    auto candidate = FlatpakCandidate { .identity = rstd::move(identity) };
    candidate.owners.push(rstd::move(owner));
    rstd_try(merge_architectures(candidate, architectures));
    candidates.insert(rstd::move(key), rstd::move(candidate));
    return Ok(empty {});
}

auto add_registry_candidate(
    rstd::collections::BTreeMap<String, RegistryFlatpakCandidate>& candidates,
    const lito::lock::LockedSource&                                source,
    String                                                         owner) -> void {
    const auto& registry = source.as_Registry();
    auto        key      = rstd::format(
        "{}@{}",
        lito::registry::registry_package_id_text(registry.pin.release.package).as_str(),
        registry.pin.release.version.text().as_str());
    auto existing = candidates.get_mut(key.as_str());
    if (existing.is_some()) {
        for (const auto& current : (**existing).owners) {
            if (current == owner) return;
        }
        (**existing).owners.push(rstd::move(owner));
        rstd::slice_::sort_unstable((**existing).owners.as_mut_slice().as_mut_ref());
        return;
    }
    auto candidate = RegistryFlatpakCandidate {
        .pin = registry.pin.clone(),
    };
    candidate.owners.push(rstd::move(owner));
    candidates.insert(rstd::move(key), rstd::move(candidate));
}

auto flatpak_architectures(const FlatpakCandidate& candidate) -> Vec<String> {
    auto result = Vec<String>::with_capacity(candidate.architectures.len());
    if (candidate.all_architectures) return result;
    for (const auto architecture : candidate.architectures) {
        result.push(String::make(architecture_name(architecture)));
    }
    return result;
}

export namespace lito::lock
{

auto project_flatpak_sources(const LockedProject&           project,
                             RegistryFlatpakArchiveProvider registry = {})
    -> LockResult<lito::flatpak::SourceSet> {
    auto candidates = rstd::collections::BTreeMap<String, FlatpakCandidate>::make();
    auto registry_candidates =
        rstd::collections::BTreeMap<String, RegistryFlatpakCandidate>::make();
    for (usize index {}; index < project.packages.len(); ++index) {
        const auto& package = project.packages[index];
        if (package.source.is_some() && package.source->is_Git()) {
            const auto& source        = package.source->as_Git();
            auto        architectures = Vec<Architecture>::make();
            rstd_try(add_candidate(
                candidates,
                lito::source::git_fetch_identity(source.url.as_str(), source.commit.as_str()),
                architectures,
                rstd::format("package '{}'", package.name.as_str())));
        }
        if (package.source.is_some() && package.source->is_Registry()) {
            add_registry_candidate(registry_candidates,
                                   *package.source,
                                   rstd::format("package '{}'", package.name.as_str()));
        }
        for (const auto& external : package.externals) {
            auto owner = rstd::format("{}:{}", package.name.as_str(), external.name.as_str());
            if (external.source.is_Git()) {
                const auto& source = external.source.as_Git();
                rstd_try(add_candidate(
                    candidates,
                    lito::source::git_fetch_identity(source.url.as_str(), source.commit.as_str()),
                    external.architectures,
                    rstd::format("external '{}'", owner.as_str())));
                continue;
            }
            const auto& source = external.source.as_Archive();
            rstd_try(add_candidate(
                candidates,
                lito::source::archive_fetch_identity(source.url.clone(), source.sha256.clone()),
                external.architectures,
                rstd::format("external '{}'", owner.as_str())));
        }
    }

    auto result = lito::flatpak::SourceSet {};
    auto values = candidates.values();
    auto layout = lito::source::SourceBundleLayout(PathBuf::from(".lito/source-bundle"_str));
    for (auto value : values) {
        auto& candidate     = *value;
        auto  owners        = candidate_owners(candidate);
        auto  architectures = flatpak_architectures(candidate);
        if (candidate.identity.is_Git()) {
            const auto& source      = candidate.identity.as_Git();
            auto        destination = layout.git(candidate.identity);
            result.push(rstd::format("Lito lock {}", owners.as_str()),
                        lito::flatpak::Source::Git(source.url.clone(),
                                                   source.commit.clone(),
                                                   rstd::move(destination),
                                                   rstd::move(architectures)));
            continue;
        }
        const auto& source      = candidate.identity.as_Archive();
        auto        destination = layout.archive(candidate.identity);
        result.push(
            rstd::format("Lito lock {}", owners.as_str()),
            lito::flatpak::Source::File(String::make(source.url.as_str()),
                                        source.sha256.clone(),
                                        PathBuf::from(destination.as_path().parent().unwrap()),
                                        String::make("source.archive"_str),
                                        rstd::move(architectures)));
    }
    auto registry_blobs  = rstd::collections::BTreeMap<String, empty>::make();
    auto registry_values = registry_candidates.values();
    for (auto value : registry_values) {
        auto& candidate = *value;
        if (registry.download_url == nullptr) {
            return lock_flatpak_failure<lito::flatpak::SourceSet>(rstd::format(
                "Flatpak source export has no Registry provider for '{}@{}'",
                lito::registry::registry_package_id_text(candidate.pin.release.package).as_str(),
                candidate.pin.release.version.text().as_str()));
        }
        auto download = registry.download_url(registry.context, candidate.pin.release);
        if (download.is_err()) return Err(rstd::move(download).unwrap_err());
        auto owners = candidate_owners(candidate);

        auto archive     = layout.registry_package(candidate.pin);
        auto archive_key = archive.as_path().to_string_lossy();
        if (registry_blobs.contains_key(archive_key.as_str())) continue;
        result.push(
            rstd::format("Lito lock Registry package {}", owners.as_str()),
            lito::flatpak::Source::File(rstd::move(download).unwrap(),
                                        candidate.pin.checksum.digest().clone(),
                                        PathBuf::from(archive.as_path().parent().unwrap()),
                                        lito::registry::registry_archive_filename(candidate.pin),
                                        Vec<String>::make()));
        registry_blobs.insert(rstd::move(archive_key), empty {});
    }
    return Ok(rstd::move(result));
}

auto flatpak_sources_json(const LockedProject&           project,
                          RegistryFlatpakArchiveProvider registry = {}) -> LockResult<String> {
    auto sources = rstd_try(project_flatpak_sources(project, registry));
    auto json    = lito::flatpak::sources_json(sources);
    if (json.is_err()) return Err(lock_flatpak_failure(rstd::move(json).unwrap_err()));
    return Ok(rstd::move(json).unwrap());
}

auto export_flatpak_sources(ref<rstd::path::Path>          root,
                            const LockConfig&              lock,
                            ref<rstd::path::Path>          output,
                            RegistryFlatpakArchiveProvider registry = {}) -> LockResult<empty> {
    auto project = rstd_try(load_locked_project(root, lock));
    auto sources = rstd_try(project_flatpak_sources(project, registry));
    auto written = lito::flatpak::write_sources(root, output, sources);
    if (written.is_err()) return Err(lock_flatpak_failure(rstd::move(written).unwrap_err()));
    return Ok(empty {});
}

} // namespace lito::lock
