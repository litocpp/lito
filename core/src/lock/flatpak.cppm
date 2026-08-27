module;
#include <rstd/macro.hpp>

export module lito.core:lock.flatpak;

import rstd;
import rstd.json;
import lito.system;
import :lock;
import :lock.config;
import :lock.document;
import :source.fetch;
import :source.bundle;

using namespace rstd::prelude;
using namespace lito::system;
using PathBuf = rstd::path::PathBuf;
using namespace rstd::literals;
using Json  = rstd::json::Value;
using Map   = rstd::json::Map;
using Array = rstd::json::Array;
using namespace lito::lock;

export namespace lito::lock
{

enum class LockExportFormat
{
    FlatpakSources,
};

constexpr auto lock_export_format_name(LockExportFormat format) noexcept -> ref<str> {
    switch (format) {
    case LockExportFormat::FlatpakSources: return "flatpak-sources"_str;
    }
    __builtin_unreachable();
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
auto flatpak_failure(String message) -> LockResult<T> {
    return Err(LockError::Schema(rstd::move(message)));
}

template<typename T>
auto flatpak_failure(ref<str> message) -> LockResult<T> {
    return flatpak_failure<T>(String::make(message));
}

auto flatpak_string(ref<str> value) -> Json {
    return Json::String(String::make(value));
}

auto public_http_url(ref<str> value) -> bool {
    auto remainder = value.strip_prefix("https://"_str);
    if (remainder.is_none()) remainder = value.strip_prefix("http://"_str);
    if (remainder.is_none() || remainder->is_empty()) return false;
    auto has_authority = false;
    for (const auto character : *remainder) {
        const auto ascii = character.to_primitive();
        if (ascii == '/' || ascii == '?' || ascii == '#') break;
        if (ascii == '@' || ascii <= 0x20 || ascii == 0x7f) return false;
        has_authority = true;
    }
    return has_authority;
}

struct FlatpakCandidate {
    lito::source::FetchIdentity identity;
    Vec<Architecture>           architectures;
    Vec<String>                 owners;
    bool                        all_architectures { false };
};

auto candidate_owners(const FlatpakCandidate& candidate) -> String {
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
            return flatpak_failure<empty>(
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

auto architecture_json(const FlatpakCandidate& candidate) -> Option<Json> {
    if (candidate.all_architectures || candidate.architectures.is_empty()) return None();
    auto architectures = Array::make();
    for (const auto& architecture : candidate.architectures) {
        architectures.push(flatpak_string(architecture_name(architecture)));
    }
    return Some(Json::Array(rstd::move(architectures)));
}

auto flatpak_sources_document(const LockedProject& project) -> LockResult<Json> {
    auto candidates = rstd::collections::BTreeMap<String, FlatpakCandidate>::make();
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

    auto sources = Array::make();
    auto values  = candidates.values();
    auto layout  = lito::source::SourceBundleLayout(PathBuf::from(".lito/source-bundle"_str));
    for (auto value : values) {
        const auto& candidate    = *value;
        auto        flatpak      = Map::make();
        auto        architecture = architecture_json(candidate);
        if (candidate.identity.is_Git()) {
            const auto& source = candidate.identity.as_Git();
            if (! public_http_url(source.url.as_str())) {
                auto owners = candidate_owners(candidate);
                return flatpak_failure<Json>(
                    rstd::format("Flatpak source export requires a public HTTP(S) Git URL for {}",
                                 owners.as_str()));
            }
            auto destination = layout.git(candidate.identity);
            flatpak.insert(String::make("commit"_str), flatpak_string(source.commit.as_str()));
            flatpak.insert(String::make("dest"_str),
                           flatpak_string(destination.as_path().to_str().unwrap()));
            flatpak.insert(String::make("type"_str), flatpak_string("git"_str));
            flatpak.insert(String::make("url"_str), flatpak_string(source.url.as_str()));
        } else {
            const auto& source = candidate.identity.as_Archive();
            if (! public_http_url(source.url.as_str())) {
                auto owners = candidate_owners(candidate);
                return flatpak_failure<Json>(rstd::format(
                    "Flatpak source export requires a public HTTP(S) archive URL for {}",
                    owners.as_str()));
            }
            auto destination = layout.archive(candidate.identity);
            flatpak.insert(
                String::make("dest"_str),
                flatpak_string(destination.as_path().parent().unwrap().to_str().unwrap()));
            flatpak.insert(String::make("dest-filename"_str), flatpak_string("source.archive"_str));
            auto sha256 = source.sha256.to_hex();
            flatpak.insert(String::make("sha256"_str), flatpak_string(sha256.as_str()));
            flatpak.insert(String::make("type"_str), flatpak_string("file"_str));
            flatpak.insert(String::make("url"_str), flatpak_string(source.url.as_str()));
        }
        if (architecture.is_some()) {
            flatpak.insert(String::make("only-arches"_str), rstd::move(architecture).unwrap());
        }
        sources.push(Json::Object(rstd::move(flatpak)));
    }
    return Ok(Json::Array(rstd::move(sources)));
}

export namespace lito::lock
{

auto flatpak_sources_json(const LockedProject& project) -> LockResult<String> {
    auto document = rstd_try(flatpak_sources_document(project));
    return Ok(rstd::json::to_string(
        document, rstd::json::FormatOptions { .pretty = true, .indent = usize(2) }));
}

auto export_flatpak_sources(ref<rstd::path::Path> root,
                            const LockConfig&     lock,
                            ref<rstd::path::Path> output) -> LockResult<empty> {
    auto project  = rstd_try(load_locked_project(root, lock));
    auto contents = rstd_try(flatpak_sources_json(project));
    auto destination =
        output.is_absolute() ? PathBuf::from(output) : PathBuf::from(root).join(output);
    auto written = rstd::fs::write_atomic(destination.as_path(), contents.as_str().as_bytes());
    if (written.is_err()) {
        return Err(LockError::Io(String::make("write Flatpak sources"_str),
                                 rstd::move(destination),
                                 rstd::move(written).unwrap_err()));
    }
    return Ok(empty {});
}

} // namespace lito::lock
