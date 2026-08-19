module;
#include <rstd/macro.hpp>

export module lito.core:source.acquisition;

import rstd;
import :source.git;
import :source.config;
import :source.requirement;
import :source.error;
import :source.event;
import lito.system;
import :source.seed;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;
using namespace lito::system;
using namespace rstd::literals;

export namespace lito::source
{

auto path_source_identity(ref<rstd::path::Path> path) -> String {
    return rstd::format("path+{}", path);
}

auto git_source_identity(ref<str> url, ref<str> commit) -> String {
    return rstd::format("git+{}#{}", url, commit);
}

auto git_requirement_identity(ref<str> url, const GitReference& reference) -> String {
    return rstd::format(
        "git\n{}\n{}\n{}", url, git_reference_kind_name(reference.kind), reference.value.as_str());
}

auto archive_source_identity(ref<str> url, ref<str> sha256) -> String {
    return rstd::format("archive+{}#sha256:{}", url, sha256);
}

struct AcquiredSource {
    PathBuf root;
    String  identity;
    bool    cacheable { false };
};

struct PackageSourceFetchRequest {
    String                   owner;
    String                   name;
    PackageSourceRequirement source;
    PathBuf                  declaring_root;
};

struct ArchiveSourceFetchRequest {
    String owner;
    String name;
    String url;
    String sha256;
};

struct ExternalSourceFetchOutcome {
    AcquiredSource             acquired;
    Vec<ResolvedPackageSource> sources;
};

} // namespace lito::source

using namespace lito::source;

auto archive_tool_requirement(const ArchiveSourceFetchRequest& request,
                              HostToolCapability               capability) -> HostToolRequirement {
    return external_source_tool_requirement(
        capability,
        request.owner.is_empty() ? "archive"_str : request.owner.as_str(),
        request.name.is_empty() ? request.url.as_str() : request.name.as_str());
}

auto process_path(Vec<String>& arguments, ref<rstd::path::Path> path) -> SourceResult<empty> {
    auto text = path.to_str();
    if (text.is_none()) {
        return source_failure<empty>(
            rstd::format("source operation path '{}' is not valid UTF-8", path));
    }
    arguments.push(String::make(*text));
    return Ok(empty {});
}

auto file_digest_matches(ref<rstd::path::Path> path, ref<str> expected) -> SourceResult<bool> {
    auto opened = rstd::fs::File::open(path);
    if (opened.is_err()) {
        auto error = rstd::move(opened).unwrap_err();
        if (error.kind() == rstd::io::error::ErrorKind { rstd::io::error::ErrorKind::NotFound })
            return Ok(false);
        return source_io_failure<bool>("open source file"_str, path, rstd::move(error));
    }
    auto file   = rstd::move(opened).unwrap();
    auto state  = rstd::crypto::Sha256::make();
    auto buffer = array<u8, 65536> {};
    while (true) {
        auto read = file.read(buffer.as_mut_slice());
        if (read.is_err()) {
            return source_io_failure<bool>(
                "read source file"_str, path, rstd::move(read).unwrap_err());
        }
        if (*read == usize {}) break;
        state.update(slice<u8>::from_raw_parts(buffer.as_ptr(), *read));
    }
    auto actual = rstd::crypto::sha256_hex(rstd::move(state).finalize());
    if (actual.len() != expected.len()) return Ok(false);
    for (usize index {}; index < actual.len(); ++index) {
        auto value = expected.as_bytes()[index].to_primitive();
        if (value >= 'A' && value <= 'F') value += 'a' - 'A';
        if (actual.as_str().as_bytes()[index].to_primitive() != value) return Ok(false);
    }
    return Ok(true);
}

auto reserve_staging_path(ref<rstd::path::Path> bucket) -> SourceResult<PathBuf> {
    for (usize index {}; index < usize(64); ++index) {
        auto name      = rstd::format("source.tmp.{}", index);
        auto candidate = PathBuf::from(bucket).join(PathBuf::from(rstd::move(name)).as_path());
        auto created   = rstd::fs::File::create_new(candidate.as_path());
        if (created.is_ok()) return Ok(rstd::move(candidate));
        auto error = rstd::move(created).unwrap_err();
        if (error.kind() !=
            rstd::io::error::ErrorKind { rstd::io::error::ErrorKind::AlreadyExists }) {
            return source_io_failure<PathBuf>(
                "reserve source download staging file"_str, candidate.as_path(), rstd::move(error));
        }
    }
    return source_failure<PathBuf>("cannot reserve source download staging file"_str);
}

auto cached_payload_is_ordinary(ref<rstd::path::Path> source) -> SourceResult<bool> {
    auto metadata = rstd::fs::symlink_metadata(source);
    if (metadata.is_err()) {
        auto error = rstd::move(metadata).unwrap_err();
        if (error.kind() == rstd::io::error::ErrorKind { rstd::io::error::ErrorKind::NotFound })
            return Ok(false);
        return source_io_failure<bool>("inspect cached source file"_str, source, rstd::move(error));
    }
    if (! metadata->is_file()) {
        return source_failure<bool>(
            rstd::format("cached source '{}' must be an ordinary file", source));
    }
    return Ok(true);
}

enum class FetchedFileOrigin
{
    Seed,
    Cache,
};

struct FetchedFile {
    String            identity;
    PathBuf           path;
    FetchedFileOrigin origin { FetchedFileOrigin::Cache };

    auto clone() const -> FetchedFile {
        return FetchedFile {
            .identity = identity.clone(),
            .path     = path.clone(),
            .origin   = origin,
        };
    }
};

auto locate_verified_file_seed(const ArchiveSourceFetchRequest& request,
                               const PackageSourceConfig&       source_config)
    -> SourceResult<Option<FetchedFile>> {
    auto identity = archive_fetch_identity(request.url.as_str(), request.sha256.as_str());
    auto located  = rstd_try(locate_fetch_seed(source_config.fetch_seeds, identity));
    if (located.is_none()) return Ok(None());
    auto matches = rstd_try(file_digest_matches(located->as_path(), request.sha256.as_str()));
    if (! matches) {
        return source_failure<Option<FetchedFile>>(
            rstd::format("file fetch seed '{}' does not match SHA-256 '{}'",
                         located->as_path(),
                         request.sha256));
    }
    return Ok(Some(FetchedFile {
        .identity = archive_source_identity(request.url.as_str(), request.sha256.as_str()),
        .path     = rstd::move(located).unwrap(),
        .origin   = FetchedFileOrigin::Seed,
    }));
}

auto acquire_cached_file(ArchiveSourceFetchRequest         request,
                         const SourceCacheSession&         session,
                         const FileCacheLayout&            layout,
                         ref<rstd::path::Path>             curl_executable,
                         const ResolvedProcessEnvironment& environment,
                         const PackageSourceConfig&        source_config,
                         SourceEventSink                   observer) -> SourceResult<FetchedFile> {
    (void)session;
    auto identity  = archive_fetch_identity(request.url.as_str(), request.sha256.as_str());
    auto fetch_key = fetch_identity_stable_key(identity);
    auto bucket    = layout.bucket(fetch_key.as_str());
    auto created   = rstd::fs::create_dir_all(bucket.as_path());
    if (created.is_err()) {
        return source_io_failure<FetchedFile>(
            "create file source cache"_str, bucket.as_path(), rstd::move(created).unwrap_err());
    }
    auto source = layout.source(fetch_key.as_str());
    auto exists = rstd_try(cached_payload_is_ordinary(source.as_path()));
    if (exists && rstd_try(file_digest_matches(source.as_path(), request.sha256.as_str()))) {
        return Ok(FetchedFile {
            .identity = archive_source_identity(request.url.as_str(), request.sha256.as_str()),
            .path     = rstd::move(source),
            .origin   = FetchedFileOrigin::Cache,
        });
    }
    if (source_config.network == NetworkPolicy::Offline) {
        return source_failure<FetchedFile>(rstd::format(
            "offline source resolution cannot fetch file source '{}'", request.url.as_str()));
    }

    auto staging   = rstd_try(reserve_staging_path(bucket.as_path()));
    auto arguments = Vec<String>::make();
    rstd_try(process_path(arguments, curl_executable));
    arguments.push(String::make("--fail"_str));
    arguments.push(String::make("--location"_str));
    arguments.push(String::make("--silent"_str));
    arguments.push(String::make("--show-error"_str));
    arguments.push(String::make("--globoff"_str));
    arguments.push(String::make("--output"_str));
    rstd_try(process_path(arguments, staging.as_path()));
    arguments.push(String::make("--"_str));
    arguments.push(request.url.clone());
    if (observer.notify != nullptr) {
        observer.notify(
            observer.context,
            SourceEvent { SourceEventKind::Fetch, request.url.as_str(), source.as_path() });
    }
    auto downloaded = run_command(arguments, environment);
    if (downloaded.is_err()) {
        (void)rstd::fs::remove_file(staging.as_path());
        return Err(SourceError::System(rstd::format("file source '{}' download", request.url),
                                       rstd::move(downloaded).unwrap_err()));
    }
    if (downloaded->exit_code != i32 {}) {
        (void)rstd::fs::remove_file(staging.as_path());
        return source_failure<FetchedFile>(
            rstd::format("file source '{}' download failed with exit code {}:\n{}{}",
                         request.url,
                         downloaded->exit_code,
                         downloaded->standard_output,
                         downloaded->standard_error));
    }
    auto matches = file_digest_matches(staging.as_path(), request.sha256.as_str());
    if (matches.is_err()) {
        (void)rstd::fs::remove_file(staging.as_path());
        return Err(rstd::move(matches).unwrap_err());
    }
    if (! *matches) {
        (void)rstd::fs::remove_file(staging.as_path());
        return source_failure<FetchedFile>(rstd::format(
            "file source '{}' does not match SHA-256 '{}'", request.url, request.sha256));
    }
    if (exists) {
        auto removed = rstd::fs::remove_file(source.as_path());
        if (removed.is_err()) {
            (void)rstd::fs::remove_file(staging.as_path());
            return source_io_failure<FetchedFile>("replace cached source file"_str,
                                                  source.as_path(),
                                                  rstd::move(removed).unwrap_err());
        }
    }
    auto published = rstd::fs::rename(staging.as_path(), source.as_path());
    if (published.is_err()) {
        (void)rstd::fs::remove_file(staging.as_path());
        return source_io_failure<FetchedFile>(
            "publish cached source file"_str, source.as_path(), rstd::move(published).unwrap_err());
    }
    return Ok(FetchedFile {
        .identity = archive_source_identity(request.url.as_str(), request.sha256.as_str()),
        .path     = rstd::move(source),
        .origin   = FetchedFileOrigin::Cache,
    });
}

struct FetchedFileTask {
    usize       request {};
    FetchedFile file;
};

auto locate_cached_file(const ArchiveSourceFetchRequest& request, const FileCacheLayout& layout)
    -> SourceResult<Option<FetchedFile>> {
    auto identity  = archive_fetch_identity(request.url.as_str(), request.sha256.as_str());
    auto fetch_key = fetch_identity_stable_key(identity);
    auto source    = layout.source(fetch_key.as_str());
    auto exists    = rstd_try(cached_payload_is_ordinary(source.as_path()));
    if (! exists || ! rstd_try(file_digest_matches(source.as_path(), request.sha256.as_str()))) {
        return Ok(Option<FetchedFile> {});
    }
    return Ok(Some(FetchedFile {
        .identity = archive_source_identity(request.url.as_str(), request.sha256.as_str()),
        .path     = rstd::move(source),
        .origin   = FetchedFileOrigin::Cache,
    }));
}

auto acquire_file_frontier(Vec<ArchiveSourceFetchRequest>    requests,
                           usize                             jobs,
                           ToolResolver&                     resolver,
                           const ResolvedProcessEnvironment& environment,
                           const PackageSourceConfig&        source_config,
                           SourceEventSink observer) -> SourceResult<Vec<FetchedFile>> {
    auto fetched = Vec<Option<FetchedFile>>::with_capacity(requests.len());
    auto pending = Vec<usize>::make();
    for (usize index {}; index < requests.len(); ++index) {
        auto seed = rstd_try(locate_verified_file_seed(requests[index], source_config));
        if (seed.is_some()) {
            resolver.report_not_required(
                archive_tool_requirement(requests[index], HostToolCapability::HttpDownload),
                "verified fetch seed is available"_str);
            fetched.push(Some(rstd::move(seed).unwrap()));
        } else {
            fetched.push(None());
            pending.push(usize(index));
        }
    }
    if (! pending.is_empty()) {
        auto root = LitoDataRoot::resolve();
        if (root.is_err()) return Err(rstd::into<SourceError>(rstd::move(root).unwrap_err()));
        auto session = root->acquire_source_cache();
        if (session.is_err()) {
            return Err(rstd::into<SourceError>(rstd::move(session).unwrap_err()));
        }
        auto layout = session->open_file_cache();
        if (layout.is_err()) {
            return Err(rstd::into<SourceError>(rstd::move(layout).unwrap_err()));
        }
        auto downloads = Vec<usize>::make();
        for (const auto index : pending) {
            auto cached = rstd_try(locate_cached_file(requests[index], *layout));
            if (cached.is_some()) {
                resolver.report_not_required(
                    archive_tool_requirement(requests[index], HostToolCapability::HttpDownload),
                    "verified file cache entry is available"_str);
                fetched[index] = Some(rstd::move(cached).unwrap());
            } else {
                downloads.push(usize(index));
            }
        }
        if (! downloads.is_empty() && source_config.network == NetworkPolicy::Offline) {
            return source_failure<Vec<FetchedFile>>(
                rstd::format("offline source resolution cannot fetch file source '{}'",
                             requests[downloads[usize {}]].url.as_str()));
        }
        if (! downloads.is_empty()) {
            const auto& request = requests[downloads[usize {}]];
            const auto  requirement =
                archive_tool_requirement(request, HostToolCapability::HttpDownload);
            auto curl = resolver.require(Tool::Curl, requirement);
            if (curl.is_err()) {
                return Err(SourceError::System(String::make("resolve curl executable"_str),
                                               rstd::move(curl).unwrap_err()));
            }
            auto worker_count = jobs < downloads.len() ? jobs : downloads.len();
            auto created = rstd::thread::BlockingTaskGroup<SourceResult<FetchedFileTask>>::make(
                worker_count, downloads.len());
            if (created.is_err()) {
                return Err(SourceError::System(
                    String::make("create file source fetch executor"_str),
                    SystemError::Io(String::make("create file source fetch executor"_str),
                                    PathBuf::make(),
                                    rstd::move(created).unwrap_err_unchecked())));
            }
            auto group = rstd::move(created).unwrap_unchecked();
            for (auto index : downloads) {
                auto request      = rstd::move(requests[index]);
                auto task_session = session->clone();
                auto task_layout  = layout->clone();
                auto task_env     = environment.clone();
                auto task_curl    = curl->executable.clone();
                auto task_sources = source_config.clone();
                auto submitted =
                    group.submit([index,
                                  request     = rstd::move(request),
                                  session     = rstd::move(task_session),
                                  layout      = rstd::move(task_layout),
                                  curl        = rstd::move(task_curl),
                                  environment = rstd::move(task_env),
                                  sources     = rstd::move(task_sources),
                                  observer]() mutable -> SourceResult<FetchedFileTask> {
                        auto file = acquire_cached_file(rstd::move(request),
                                                        session,
                                                        layout,
                                                        curl.as_path(),
                                                        environment,
                                                        sources,
                                                        observer);
                        if (file.is_err()) return Err(rstd::move(file).unwrap_err());
                        return Ok(FetchedFileTask {
                            .request = index,
                            .file    = rstd::move(file).unwrap(),
                        });
                    });
                if (submitted.is_err()) {
                    return source_failure<Vec<FetchedFile>>(
                        "cannot submit file source fetch task"_str);
                }
            }
            auto outcomes = rstd::move(group).join();
            for (auto& outcome : outcomes) {
                auto value = rstd::move(outcome).into_value();
                if (value.is_none()) {
                    return source_failure<Vec<FetchedFile>>(
                        "file source fetch task was cancelled"_str);
                }
                auto result = rstd::move(value).unwrap_unchecked();
                if (result.is_err()) return Err(rstd::move(result).unwrap_err());
                auto task             = rstd::move(result).unwrap();
                fetched[task.request] = Some(rstd::move(task.file));
            }
        }
    }

    auto result = Vec<FetchedFile>::with_capacity(fetched.len());
    for (auto& file : fetched) {
        if (file.is_none()) {
            return source_failure<Vec<FetchedFile>>("file source fetch result is missing"_str);
        }
        result.push(rstd::move(file).unwrap());
    }
    return Ok(rstd::move(result));
}

struct ArchiveMaterializationLayout {
    PathBuf area;
    PathBuf lock;
    PathBuf extracted;
    PathBuf receipt;
};

auto archive_materialization_layout(ref<rstd::path::Path> materialization_root, ref<str> identity)
    -> ArchiveMaterializationLayout {
    auto archives =
        PathBuf::from(materialization_root).join(PathBuf::from("archives"_str).as_path());
    auto area = archives.join(PathBuf::from(rstd::crypto::sha256_hex(identity)).as_path());
    return ArchiveMaterializationLayout {
        .area      = area.clone(),
        .lock      = area.join(PathBuf::from("lock"_str).as_path()),
        .extracted = area.join(PathBuf::from("extracted"_str).as_path()),
        .receipt   = area.join(PathBuf::from("source-v2"_str).as_path()),
    };
}

auto reusable_archive_materialization(const ArchiveMaterializationLayout& layout, ref<str> identity)
    -> SourceResult<Option<AcquiredSource>> {
    auto current = rstd::fs::read_to_string(layout.receipt.as_path());
    if (current.is_err()) {
        auto error = rstd::move(current).unwrap_err();
        if (error.kind() == rstd::io::error::ErrorKind { rstd::io::error::ErrorKind::NotFound }) {
            return Ok(Option<AcquiredSource> {});
        }
        return source_io_failure<Option<AcquiredSource>>("read archive materialization receipt"_str,
                                                         layout.receipt.as_path(),
                                                         rstd::move(error));
    }
    auto parts = current->as_str().split_once("\n"_str);
    if (parts.is_none() || parts->get<0>() != "lito-archive-materialization-v2"_str) {
        return Ok(Option<AcquiredSource> {});
    }
    auto relative  = parts->get<1>().trim_ascii();
    auto candidate = relative.is_empty() || relative == "."_str
                         ? layout.extracted.clone()
                         : layout.extracted.join(PathBuf::from(relative).as_path());
    auto canonical = rstd::fs::canonicalize(candidate.as_path());
    if (canonical.is_err()) {
        auto error = rstd::move(canonical).unwrap_err();
        if (error.kind() == rstd::io::error::ErrorKind { rstd::io::error::ErrorKind::NotFound }) {
            return Ok(Option<AcquiredSource> {});
        }
        return source_io_failure<Option<AcquiredSource>>(
            "resolve archive materialization"_str, candidate.as_path(), rstd::move(error));
    }
    auto canonical_extracted = rstd::fs::canonicalize(layout.extracted.as_path());
    if (canonical_extracted.is_err()) {
        auto error = rstd::move(canonical_extracted).unwrap_err();
        if (error.kind() == rstd::io::error::ErrorKind { rstd::io::error::ErrorKind::NotFound }) {
            return Ok(Option<AcquiredSource> {});
        }
        return source_io_failure<Option<AcquiredSource>>(
            "resolve archive extraction"_str, layout.extracted.as_path(), rstd::move(error));
    }
    auto metadata = rstd::fs::metadata(canonical->as_path());
    if (metadata.is_err()) {
        auto error = rstd::move(metadata).unwrap_err();
        if (error.kind() == rstd::io::error::ErrorKind { rstd::io::error::ErrorKind::NotFound }) {
            return Ok(Option<AcquiredSource> {});
        }
        return source_io_failure<Option<AcquiredSource>>(
            "inspect archive materialization"_str, canonical->as_path(), rstd::move(error));
    }
    if (! metadata->is_dir() ||
        ! canonical->as_path().starts_with(canonical_extracted->as_path())) {
        return Ok(Option<AcquiredSource> {});
    }
    return Ok(Some(AcquiredSource {
        .root      = rstd::move(canonical).unwrap(),
        .identity  = String::make(identity),
        .cacheable = true,
    }));
}

auto inspect_archive_materialization(ref<rstd::path::Path> materialization_root, ref<str> identity)
    -> SourceResult<Option<AcquiredSource>> {
    auto layout  = archive_materialization_layout(materialization_root, identity);
    auto created = rstd::fs::create_dir_all(layout.area.as_path());
    if (created.is_err()) {
        return source_io_failure<Option<AcquiredSource>>("create archive materialization area"_str,
                                                         layout.area.as_path(),
                                                         rstd::move(created).unwrap_err());
    }
    auto opened = rstd::fs::OpenOptions::make().read(true).write(true).create(true).open(
        layout.lock.as_path());
    if (opened.is_err()) {
        return source_io_failure<Option<AcquiredSource>>("open archive materialization lock"_str,
                                                         layout.lock.as_path(),
                                                         rstd::move(opened).unwrap_err());
    }
    auto locked =
        rstd::fs::FileLock::acquire(rstd::move(opened).unwrap(), rstd::fs::FileLockMode::Exclusive);
    if (locked.is_err()) {
        return source_io_failure<Option<AcquiredSource>>("lock archive materialization"_str,
                                                         layout.lock.as_path(),
                                                         rstd::move(locked).unwrap_err());
    }
    return reusable_archive_materialization(layout, identity);
}

enum class ArchiveExtractorKind
{
    BsdTar,
    Tar,
    CMakeTar,
};

struct ArchiveExtractor {
    ArchiveExtractorKind kind { ArchiveExtractorKind::BsdTar };
    ResolvedTool         tool;

    auto clone() const -> ArchiveExtractor {
        return ArchiveExtractor {
            .kind = kind,
            .tool = tool.clone(),
        };
    }

    auto provider_name() const noexcept -> ref<str> {
        if (kind == ArchiveExtractorKind::BsdTar) return "bsdtar"_str;
        if (kind == ArchiveExtractorKind::Tar) return "tar"_str;
        return "cmake -E tar"_str;
    }

    auto invocation(ref<rstd::path::Path> archive) const -> SourceResult<Vec<String>> {
        auto arguments = Vec<String>::make();
        rstd_try(process_path(arguments, tool.executable.as_path()));
        if (kind == ArchiveExtractorKind::CMakeTar) {
            arguments.push(String::make("-E"_str));
            arguments.push(String::make("tar"_str));
        }
        arguments.push(String::make("xvf"_str));
        rstd_try(process_path(arguments, archive));
        return Ok(rstd::move(arguments));
    }
};

auto select_archive_extractor(ToolResolver& resolver, const HostToolRequirement& requirement)
    -> SourceResult<ArchiveExtractor> {
    Tool first  = Tool::BsdTar;
    Tool second = Tool::Tar;
    if (resolver.tools().explicitly_configured(Tool::Tar) &&
        ! resolver.tools().explicitly_configured(Tool::BsdTar)) {
        first  = Tool::Tar;
        second = Tool::BsdTar;
    }
    const Tool candidates[] = { first, second };
    auto       attempts     = String::make();
    for (const auto candidate : candidates) {
        if (candidate == second &&
            resolver.tools().requested(first).as_os_str().as_encoded_bytes() ==
                resolver.tools().requested(second).as_os_str().as_encoded_bytes()) {
            continue;
        }
        auto probed = resolver.probe(candidate);
        if (probed.is_err()) {
            return Err(SourceError::System(
                rstd::format("probe {} archive extractor", tool_name(candidate)),
                rstd::move(probed).unwrap_err()));
        }
        if (probed->is_some()) {
            auto tool   = rstd::move(probed).unwrap().unwrap();
            auto kind   = candidate == Tool::BsdTar ? ArchiveExtractorKind::BsdTar
                                                    : ArchiveExtractorKind::Tar;
            auto result = ArchiveExtractor {
                .kind = kind,
                .tool = rstd::move(tool),
            };
            resolver.report(requirement, result.provider_name(), result.tool);
            return Ok(rstd::move(result));
        }
        resolver.report_candidate_missing(requirement, tool_name(candidate), candidate);
        attempts.push_str(rstd::format("\n    {} '{}': not found",
                                       tool_name(candidate),
                                       resolver.tools().requested(candidate))
                              .as_str());
    }
    auto cmake = resolver.probe(Tool::CMake);
    if (cmake.is_err()) {
        return Err(SourceError::System(String::make("probe CMake archive extractor"_str),
                                       rstd::move(cmake).unwrap_err()));
    }
    if (cmake->is_some()) {
        auto result = ArchiveExtractor {
            .kind = ArchiveExtractorKind::CMakeTar,
            .tool = rstd::move(cmake).unwrap().unwrap(),
        };
        resolver.report(requirement, result.provider_name(), result.tool);
        return Ok(rstd::move(result));
    }
    resolver.report_candidate_missing(requirement, "cmake -E tar"_str, Tool::CMake);
    attempts.push_str(rstd::format("\n    cmake -E tar via '{}': not found",
                                   resolver.tools().requested(Tool::CMake))
                          .as_str());
    return source_failure<ArchiveExtractor>(
        rstd::format("cannot provide {} required by {}; tried:{}",
                     host_tool_capability_name(requirement.capability),
                     host_tool_requirement_origin_text(requirement.origin),
                     attempts.as_str()));
}

auto materialize_archive(FetchedFile                       file,
                         ref<rstd::path::Path>             materialization_root,
                         const ArchiveExtractor&           extractor,
                         const ResolvedProcessEnvironment& environment,
                         SourceEventSink observer) -> SourceResult<AcquiredSource> {
    auto layout  = archive_materialization_layout(materialization_root, file.identity.as_str());
    auto created = rstd::fs::create_dir_all(layout.area.as_path());
    if (created.is_err()) {
        return source_io_failure<AcquiredSource>("create archive materialization area"_str,
                                                 layout.area.as_path(),
                                                 rstd::move(created).unwrap_err());
    }
    auto opened = rstd::fs::OpenOptions::make().read(true).write(true).create(true).open(
        layout.lock.as_path());
    if (opened.is_err()) {
        return source_io_failure<AcquiredSource>("open archive materialization lock"_str,
                                                 layout.lock.as_path(),
                                                 rstd::move(opened).unwrap_err());
    }
    auto locked =
        rstd::fs::FileLock::acquire(rstd::move(opened).unwrap(), rstd::fs::FileLockMode::Exclusive);
    if (locked.is_err()) {
        return source_io_failure<AcquiredSource>("lock archive materialization"_str,
                                                 layout.lock.as_path(),
                                                 rstd::move(locked).unwrap_err());
    }

    auto reusable = rstd_try(reusable_archive_materialization(layout, file.identity.as_str()));
    if (reusable.is_some()) return Ok(rstd::move(reusable).unwrap());

    auto receipt_exists = rstd::fs::exists(layout.receipt.as_path());
    if (receipt_exists.is_err()) {
        return source_io_failure<AcquiredSource>("inspect archive materialization receipt"_str,
                                                 layout.receipt.as_path(),
                                                 rstd::move(receipt_exists).unwrap_err());
    }
    if (*receipt_exists) {
        auto removed = rstd::fs::remove_file(layout.receipt.as_path());
        if (removed.is_err()) {
            return source_io_failure<AcquiredSource>(
                "invalidate archive materialization receipt"_str,
                layout.receipt.as_path(),
                rstd::move(removed).unwrap_err());
        }
    }

    auto exists = rstd::fs::exists(layout.extracted.as_path());
    if (exists.is_err()) {
        return source_io_failure<AcquiredSource>("inspect archive extraction"_str,
                                                 layout.extracted.as_path(),
                                                 rstd::move(exists).unwrap_err());
    }
    if (*exists) {
        auto removed = rstd::fs::remove_dir_all(layout.extracted.as_path());
        if (removed.is_err()) {
            return source_io_failure<AcquiredSource>("reset archive extraction"_str,
                                                     layout.extracted.as_path(),
                                                     rstd::move(removed).unwrap_err());
        }
    }
    created = rstd::fs::create_dir_all(layout.extracted.as_path());
    if (created.is_err()) {
        return source_io_failure<AcquiredSource>("create archive extraction"_str,
                                                 layout.extracted.as_path(),
                                                 rstd::move(created).unwrap_err());
    }
    auto arguments = rstd_try(extractor.invocation(file.path.as_path()));
    if (observer.notify != nullptr) {
        observer.notify(observer.context,
                        SourceEvent { SourceEventKind::Extract,
                                      file.identity.as_str(),
                                      layout.extracted.as_path() });
    }
    auto status = run_command(arguments, environment, Some(layout.extracted.as_path()));
    if (status.is_err()) {
        (void)rstd::fs::remove_dir_all(layout.extracted.as_path());
        return Err(SourceError::System(String::make("archive source extraction"_str),
                                       rstd::move(status).unwrap_err()));
    }
    if (status->exit_code != i32 {}) {
        (void)rstd::fs::remove_dir_all(layout.extracted.as_path());
        return source_failure<AcquiredSource>(
            rstd::format("archive source extraction with {} failed with exit code {}:\n{}{}",
                         extractor.provider_name(),
                         status->exit_code,
                         status->standard_output,
                         status->standard_error));
    }

    auto opened_directory = rstd::fs::read_dir(layout.extracted.as_path());
    if (opened_directory.is_err()) {
        return source_io_failure<AcquiredSource>("enumerate archive"_str,
                                                 layout.extracted.as_path(),
                                                 rstd::move(opened_directory).unwrap_err());
    }
    auto root       = layout.extracted.clone();
    auto only_entry = Option<PathBuf> {};
    auto count      = usize {};
    auto entries    = rstd::move(opened_directory).unwrap();
    for (auto next = entries.next(); next.is_some(); next = entries.next()) {
        if (next->is_err()) {
            return source_io_failure<AcquiredSource>("enumerate archive"_str,
                                                     layout.extracted.as_path(),
                                                     rstd::move(*next).unwrap_err());
        }
        auto entry = rstd::move(*next).unwrap();
        ++count;
        if (count != usize(1)) continue;
        auto type = entry.file_type();
        if (type.is_err()) {
            auto path = entry.path();
            return source_io_failure<AcquiredSource>(
                "inspect archive entry"_str, path.as_path(), rstd::move(type).unwrap_err());
        }
        if (type->is_dir()) only_entry = Some(entry.path());
    }
    if (count == usize(1) && only_entry.is_some()) root = rstd::move(only_entry).unwrap();
    auto canonical = rstd::fs::canonicalize(root.as_path());
    if (canonical.is_err()) {
        return source_io_failure<AcquiredSource>(
            "resolve archive root"_str, root.as_path(), rstd::move(canonical).unwrap_err());
    }
    auto relative = canonical->as_path().strip_prefix(layout.extracted.as_path());
    if (relative.is_none()) {
        return source_failure<AcquiredSource>(rstd::format(
            "archive root '{}' is outside materialization area", canonical->as_path()));
    }
    auto relative_text = relative->is_empty() ? "."_str : relative->to_str().unwrap_or(""_str);
    if (relative_text.is_empty()) {
        return source_failure<AcquiredSource>(
            rstd::format("archive root '{}' is not valid UTF-8", canonical->as_path()));
    }
    auto receipt_text = String::make("lito-archive-materialization-v2\n"_str);
    receipt_text.push_str(relative_text);
    receipt_text.push('\n');
    auto written =
        rstd::fs::write_atomic(layout.receipt.as_path(), receipt_text.as_str().as_bytes());
    if (written.is_err()) {
        return source_io_failure<AcquiredSource>("publish archive receipt"_str,
                                                 layout.receipt.as_path(),
                                                 rstd::move(written).unwrap_err());
    }
    return Ok(AcquiredSource {
        .root      = rstd::move(canonical).unwrap(),
        .identity  = rstd::move(file.identity),
        .cacheable = true,
    });
}

struct MaterializedArchiveTask {
    usize          request {};
    AcquiredSource source;
};

export namespace lito::source
{

auto acquire_archive_frontier(Vec<ArchiveSourceFetchRequest>    requests,
                              usize                             jobs,
                              ref<rstd::path::Path>             materialization_root,
                              ToolResolver&                     resolver,
                              const ResolvedProcessEnvironment& environment,
                              const PackageSourceConfig&        source_config = {},
                              SourceEventSink observer = {}) -> SourceResult<Vec<AcquiredSource>> {
    if (jobs == usize {}) {
        return source_failure<Vec<AcquiredSource>>(
            "archive source fetch jobs must be greater than zero"_str);
    }
    auto result = Vec<AcquiredSource>::with_capacity(requests.len());
    if (requests.is_empty()) return Ok(rstd::move(result));

    auto unique       = Vec<ArchiveSourceFetchRequest>::make();
    auto request_keys = rstd::collections::BTreeMap<String, usize>::make();
    auto bindings     = Vec<usize>::with_capacity(requests.len());
    for (auto& request : requests) {
        auto key      = archive_source_identity(request.url.as_str(), request.sha256.as_str());
        auto existing = request_keys.get(key.as_str());
        if (existing.is_some()) {
            bindings.push(usize(**existing));
            continue;
        }
        auto index = unique.len();
        request_keys.insert(rstd::move(key), index);
        bindings.push(usize(index));
        unique.push(rstd::move(request));
    }

    auto materialized    = Vec<Option<AcquiredSource>>::with_capacity(unique.len());
    auto pending         = Vec<ArchiveSourceFetchRequest>::make();
    auto pending_indices = Vec<usize>::make();
    for (usize index {}; index < unique.len(); ++index) {
        auto identity =
            archive_source_identity(unique[index].url.as_str(), unique[index].sha256.as_str());
        auto reused =
            rstd_try(inspect_archive_materialization(materialization_root, identity.as_str()));
        materialized.push(rstd::move(reused));
        if (materialized[index].is_some()) {
            resolver.report_not_required(
                archive_tool_requirement(unique[index], HostToolCapability::ArchiveExtraction),
                "archive materialization is reusable"_str);
            resolver.report_not_required(
                archive_tool_requirement(unique[index], HostToolCapability::HttpDownload),
                "archive materialization is reusable"_str);
            continue;
        }
        pending_indices.push(usize(index));
        pending.push(rstd::move(unique[index]));
    }

    if (! pending.is_empty()) {
        const auto& first                  = pending[usize {}];
        auto        extraction_requirement = external_source_tool_requirement(
            HostToolCapability::ArchiveExtraction,
            first.owner.is_empty() ? "archive"_str : first.owner.as_str(),
            first.name.is_empty() ? first.url.as_str() : first.name.as_str());
        auto files              = rstd_try(acquire_file_frontier(
            rstd::move(pending), jobs, resolver, environment, source_config, observer));
        auto extraction_files   = Vec<FetchedFile>::make();
        auto extraction_indices = Vec<usize>::make();
        for (usize index {}; index < files.len(); ++index) {
            auto unique_index = pending_indices[index];
            auto reused = rstd_try(inspect_archive_materialization(materialization_root,
                                                                   files[index].identity.as_str()));
            if (reused.is_some()) {
                materialized[unique_index] = Some(rstd::move(reused).unwrap());
                continue;
            }
            extraction_indices.push(usize(unique_index));
            extraction_files.push(rstd::move(files[index]));
        }
        if (! extraction_files.is_empty()) {
            auto extractor = rstd_try(select_archive_extractor(resolver, extraction_requirement));
            auto worker_count = jobs < extraction_files.len() ? jobs : extraction_files.len();
            auto created =
                rstd::thread::BlockingTaskGroup<SourceResult<MaterializedArchiveTask>>::make(
                    worker_count, extraction_files.len());
            if (created.is_err()) {
                return Err(SourceError::System(
                    String::make("create archive materialization executor"_str),
                    SystemError::Io(String::make("create archive materialization executor"_str),
                                    PathBuf::make(),
                                    rstd::move(created).unwrap_err_unchecked())));
            }
            auto group = rstd::move(created).unwrap_unchecked();
            for (usize index {}; index < extraction_files.len(); ++index) {
                auto file           = rstd::move(extraction_files[index]);
                auto unique_index   = extraction_indices[index];
                auto task_root      = PathBuf::from(materialization_root);
                auto task_extractor = extractor.clone();
                auto task_env       = environment.clone();
                auto task_observer  = observer;
                auto submitted      = group.submit(
                    [unique_index,
                     file        = rstd::move(file),
                     root        = rstd::move(task_root),
                     extractor   = rstd::move(task_extractor),
                     environment = rstd::move(task_env),
                     observer = task_observer]() mutable -> SourceResult<MaterializedArchiveTask> {
                        auto source = materialize_archive(
                            rstd::move(file), root.as_path(), extractor, environment, observer);
                        if (source.is_err()) return Err(rstd::move(source).unwrap_err());
                        return Ok(MaterializedArchiveTask {
                            .request = unique_index,
                            .source  = rstd::move(source).unwrap(),
                        });
                    });
                if (submitted.is_err()) {
                    return source_failure<Vec<AcquiredSource>>(
                        "cannot submit archive materialization task"_str);
                }
            }
            auto outcomes = rstd::move(group).join();
            for (auto& outcome : outcomes) {
                auto value = rstd::move(outcome).into_value();
                if (value.is_none()) {
                    return source_failure<Vec<AcquiredSource>>(
                        "archive materialization task was cancelled"_str);
                }
                auto task = rstd::move(value).unwrap_unchecked();
                if (task.is_err()) return Err(rstd::move(task).unwrap_err());
                auto source                  = rstd::move(task).unwrap();
                materialized[source.request] = Some(rstd::move(source.source));
            }
        }
    }
    for (auto binding : bindings) {
        if (materialized[binding].is_none()) {
            return source_failure<Vec<AcquiredSource>>(
                "archive materialization result is missing"_str);
        }
        const auto& source = *materialized[binding];
        result.push(AcquiredSource {
            .root      = source.root.clone(),
            .identity  = source.identity.clone(),
            .cacheable = source.cacheable,
        });
    }
    return Ok(rstd::move(result));
}

} // namespace lito::source
