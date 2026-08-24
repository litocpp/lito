module;
#include <rstd/enum.hpp>
#include <rstd/macro.hpp>

export module lito.tools:acquisition;

import rstd;
import lito.crypto;
import lito.core;
import lito.system;
import :error;
import :model;
import :resolver;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito::system;
using namespace lito::tools;
using PathBuf = rstd::path::PathBuf;

export namespace lito::tools::acquisition
{

class AcquisitionError {
    RSTD_ENUM(AcquisitionError,
              (System, (String operation; SystemError source;)),
              (Tools, (String operation; lito::tools::ToolError source;)),
              (Io, (String operation; PathBuf path; rstd::io::error::Error source;)),
              (Message, (String message;)))
};

template<typename T>
using AcquisitionResult = Result<T, AcquisitionError>;

enum class AcquisitionEventKind
{
    Fetch,
    Extract,
};

struct AcquisitionEvent {
    AcquisitionEventKind  kind { AcquisitionEventKind::Fetch };
    ref<str>              label;
    ref<str>              source;
    ref<rstd::path::Path> destination;
    rstd::time::Duration  elapsed;
    bool                  completed { false };
};

struct AcquisitionEventSink {
    void* context {};
    void (*notify)(void*, const AcquisitionEvent&) noexcept {};
};

struct VerifiedArchiveRequest {
    String                           label;
    lito::parse::FetchUrl            url;
    lito::crypto::Sha256Digest       sha256;
    Option<u64>                      expected_size;
    Option<PathBuf>                  seed;
    lito::tools::HostToolRequirement download_requirement;
    lito::tools::HostToolRequirement extraction_requirement;
};

struct VerifiedFile {
    String  identity;
    PathBuf path;
    u64     size {};

    auto clone() const -> VerifiedFile {
        return VerifiedFile {
            .identity = identity.clone(),
            .path     = path.clone(),
            .size     = size,
        };
    }
};

enum class ArchiveExtractorKind
{
    BsdTar,
    Tar,
    CMakeTar,
};

struct ArchiveExtractor {
    ArchiveExtractorKind      kind { ArchiveExtractorKind::BsdTar };
    lito::tools::ResolvedTool tool;

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
};

struct ExtractedArchive {
    PathBuf root;
    String  identity;
};

auto acquire_verified_files(Vec<VerifiedArchiveRequest>       requests,
                            usize                             jobs,
                            lito::tools::ToolResolver&        resolver,
                            const ResolvedProcessEnvironment& environment,
                            bool                              offline  = false,
                            AcquisitionEventSink              observer = {})
    -> AcquisitionResult<Vec<VerifiedFile>>;

auto select_archive_extractor(lito::tools::ToolResolver&              resolver,
                              const lito::tools::HostToolRequirement& requirement)
    -> AcquisitionResult<ArchiveExtractor>;

auto extract_verified_archive(VerifiedFile                      file,
                              ref<rstd::path::Path>             destination,
                              Option<ref<str>>                  expected_root,
                              const ArchiveExtractor&           extractor,
                              const ResolvedProcessEnvironment& environment,
                              AcquisitionEventSink              observer = {})
    -> AcquisitionResult<ExtractedArchive>;

} // namespace lito::tools::acquisition

export namespace rstd
{

template<>
struct Impl<convert::From<lito::system::SystemError>, lito::tools::acquisition::AcquisitionError> {
    static auto from(lito::system::SystemError error)
        -> lito::tools::acquisition::AcquisitionError {
        return lito::tools::acquisition::AcquisitionError::System(
            String::make("acquisition operation"_str), rstd::move(error));
    }
};

template<>
struct Impl<fmt::Display, lito::tools::acquisition::AcquisitionError>
    : ImplBase<lito::tools::acquisition::AcquisitionError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        const auto& error = this->self();
        if (error.is_System()) return formatter.write_str(error.as_System().operation.as_str());
        if (error.is_Tools()) return formatter.write_str(error.as_Tools().operation.as_str());
        if (error.is_Io()) {
            const auto& value = error.as_Io();
            return formatter.write_fmt(
                fmt::Arguments::make("cannot {} '{}'", value.operation, value.path.as_path()));
        }
        return formatter.write_str(error.as_Message().message.as_str());
    }
};

template<>
struct Impl<fmt::Debug, lito::tools::acquisition::AcquisitionError>
    : ImplBase<lito::tools::acquisition::AcquisitionError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, lito::tools::acquisition::AcquisitionError>
    : ImplBase<lito::tools::acquisition::AcquisitionError> {
    auto source() const noexcept -> Option<error::ErrorRef> {
        const auto& error = this->self();
        if (error.is_System()) {
            return Some(dyn<error::Error>::from_ref(error.as_System().source));
        }
        if (error.is_Tools()) {
            return Some(dyn<error::Error>::from_ref(error.as_Tools().source));
        }
        if (error.is_Io()) return Some(dyn<error::Error>::from_ref(error.as_Io().source));
        return None();
    }
};

} // namespace rstd

namespace lito::tools::acquisition
{

template<typename T>
auto failure(String message) -> AcquisitionResult<T> {
    return Err(AcquisitionError::Message(rstd::move(message)));
}

template<typename T>
auto failure(ref<str> message) -> AcquisitionResult<T> {
    return failure<T>(String::make(message));
}

template<typename T>
auto io_failure(ref<str> operation, ref<rstd::path::Path> path, rstd::io::error::Error source)
    -> AcquisitionResult<T> {
    return Err(
        AcquisitionError::Io(String::make(operation), PathBuf::from(path), rstd::move(source)));
}

auto process_path(Vec<String>& arguments, ref<rstd::path::Path> path) -> AcquisitionResult<empty> {
    auto text = path.to_str();
    if (text.is_none()) {
        return failure<empty>(rstd::format("acquisition path '{}' is not valid UTF-8", path));
    }
    arguments.push(String::make(*text));
    return Ok(empty {});
}

auto archive_identity(const lito::parse::FetchUrl& url, const lito::crypto::Sha256Digest& sha256)
    -> String {
    return rstd::format("archive+{}#sha256:{}", url, sha256);
}

auto archive_fetch_key(const lito::parse::FetchUrl& url, const lito::crypto::Sha256Digest& sha256)
    -> String {
    auto identity = rstd::format("lito-fetch-v1\narchive\n{}\n{}", url, sha256);
    return lito::crypto::sha256_hex(identity.as_str());
}

auto ordinary_file_metadata(ref<rstd::path::Path> path)
    -> AcquisitionResult<Option<rstd::fs::Metadata>> {
    auto metadata = rstd::fs::symlink_metadata(path);
    if (metadata.is_ok()) {
        if (! metadata->is_file() || metadata->is_symlink()) {
            return failure<Option<rstd::fs::Metadata>>(
                rstd::format("acquisition file '{}' must be an ordinary file", path));
        }
        return Ok(Some(rstd::move(metadata).unwrap()));
    }
    auto error = rstd::move(metadata).unwrap_err();
    if (error.kind() == rstd::io::error::ErrorKind { rstd::io::error::ErrorKind::NotFound }) {
        return Ok(None());
    }
    return io_failure<Option<rstd::fs::Metadata>>(
        "inspect acquisition file"_str, path, rstd::move(error));
}

auto file_digest_matches(ref<rstd::path::Path> path, const lito::crypto::Sha256Digest& expected)
    -> AcquisitionResult<bool> {
    auto opened = rstd::fs::File::open(path);
    if (opened.is_err()) {
        return io_failure<bool>("open acquisition file"_str, path, rstd::move(opened).unwrap_err());
    }
    auto file   = rstd::move(opened).unwrap();
    auto state  = lito::crypto::Sha256::make();
    auto buffer = array<u8, 65536> {};
    while (true) {
        auto read = file.read(buffer.as_mut_slice());
        if (read.is_err()) {
            return io_failure<bool>(
                "read acquisition file"_str, path, rstd::move(read).unwrap_err());
        }
        if (*read == usize {}) break;
        state.update(slice<u8>::from_raw_parts(buffer.as_ptr(), *read));
    }
    return Ok(rstd::move(state).finalize_digest() == expected);
}

auto verified_file(ref<rstd::path::Path> path, const VerifiedArchiveRequest& request)
    -> AcquisitionResult<Option<VerifiedFile>> {
    auto metadata = rstd_try(ordinary_file_metadata(path));
    if (metadata.is_none()) return Ok(None());
    if (request.expected_size.is_some() && metadata->len() != *request.expected_size) {
        return Ok(None());
    }
    if (! rstd_try(file_digest_matches(path, request.sha256))) return Ok(None());
    return Ok(Some(VerifiedFile {
        .identity = archive_identity(request.url, request.sha256),
        .path     = PathBuf::from(path),
        .size     = metadata->len(),
    }));
}

auto reserve_staging_path(ref<rstd::path::Path> bucket) -> AcquisitionResult<PathBuf> {
    for (usize index {}; index < usize(64); ++index) {
        auto candidate = PathBuf::from(bucket).join(
            PathBuf::from(rstd::format("source.tmp.{}", index)).as_path());
        auto created = rstd::fs::File::create_new(candidate.as_path());
        if (created.is_ok()) return Ok(rstd::move(candidate));
        auto error = rstd::move(created).unwrap_err();
        if (error.kind() !=
            rstd::io::error::ErrorKind { rstd::io::error::ErrorKind::AlreadyExists }) {
            return io_failure<PathBuf>(
                "reserve acquisition staging file"_str, candidate.as_path(), rstd::move(error));
        }
    }
    return failure<PathBuf>("cannot reserve acquisition staging file"_str);
}

auto acquire_cached_file(VerifiedArchiveRequest            request,
                         const SourceCacheSession&         session,
                         const FileCacheLayout&            layout,
                         ref<rstd::path::Path>             curl,
                         const ResolvedProcessEnvironment& environment,
                         AcquisitionEventSink observer) -> AcquisitionResult<VerifiedFile> {
    (void)session;
    auto key     = archive_fetch_key(request.url, request.sha256);
    auto bucket  = layout.bucket(key.as_str());
    auto created = rstd::fs::create_dir_all(bucket.as_path());
    if (created.is_err()) {
        return io_failure<VerifiedFile>("create acquisition cache bucket"_str,
                                        bucket.as_path(),
                                        rstd::move(created).unwrap_err());
    }
    auto source = layout.source(key.as_str());
    auto cached = rstd_try(verified_file(source.as_path(), request));
    if (cached.is_some()) return Ok(rstd::move(cached).unwrap());

    auto existing  = rstd_try(ordinary_file_metadata(source.as_path()));
    auto staging   = rstd_try(reserve_staging_path(bucket.as_path()));
    auto arguments = Vec<String>::make();
    rstd_try(process_path(arguments, curl));
    arguments.push(String::make("--fail"_str));
    arguments.push(String::make("--location"_str));
    arguments.push(String::make("--silent"_str));
    arguments.push(String::make("--show-error"_str));
    arguments.push(String::make("--globoff"_str));
    arguments.push(String::make("--output"_str));
    rstd_try(process_path(arguments, staging.as_path()));
    arguments.push(String::make("--"_str));
    arguments.push(String::make(request.url.as_str()));
    if (observer.notify != nullptr) {
        observer.notify(observer.context,
                        AcquisitionEvent {
                            .kind        = AcquisitionEventKind::Fetch,
                            .label       = request.label.as_str(),
                            .source      = request.url.as_str(),
                            .destination = source.as_path(),
                        });
    }
    auto started    = rstd::time::Instant::now();
    auto downloaded = run_command(arguments, environment);
    if (observer.notify != nullptr) {
        observer.notify(observer.context,
                        AcquisitionEvent {
                            .kind        = AcquisitionEventKind::Fetch,
                            .label       = request.label.as_str(),
                            .source      = request.url.as_str(),
                            .destination = source.as_path(),
                            .elapsed     = started.elapsed(),
                            .completed   = true,
                        });
    }
    if (downloaded.is_err()) {
        (void)rstd::fs::remove_file(staging.as_path());
        return Err(AcquisitionError::System(rstd::format("download '{}'", request.url),
                                            rstd::move(downloaded).unwrap_err()));
    }
    if (downloaded->exit_code != i32 {}) {
        (void)rstd::fs::remove_file(staging.as_path());
        return failure<VerifiedFile>(rstd::format("download '{}' failed with exit code {}:\n{}{}",
                                                  request.url,
                                                  downloaded->exit_code,
                                                  downloaded->standard_output,
                                                  downloaded->standard_error));
    }
    auto staged_metadata = rstd_try(ordinary_file_metadata(staging.as_path()));
    if (staged_metadata.is_none()) {
        (void)rstd::fs::remove_file(staging.as_path());
        return failure<VerifiedFile>(
            rstd::format("download '{}' did not produce an ordinary file", request.url));
    }
    if (request.expected_size.is_some() && staged_metadata->len() != *request.expected_size) {
        auto actual = staged_metadata->len();
        (void)rstd::fs::remove_file(staging.as_path());
        return failure<VerifiedFile>(rstd::format(
            "download '{}' has size {}, expected {}", request.url, actual, *request.expected_size));
    }
    auto matches = file_digest_matches(staging.as_path(), request.sha256);
    if (matches.is_err()) {
        (void)rstd::fs::remove_file(staging.as_path());
        return Err(rstd::move(matches).unwrap_err());
    }
    if (! *matches) {
        (void)rstd::fs::remove_file(staging.as_path());
        return failure<VerifiedFile>(
            rstd::format("download '{}' does not match SHA-256 '{}'", request.url, request.sha256));
    }
    if (existing.is_some()) {
        auto removed = rstd::fs::remove_file(source.as_path());
        if (removed.is_err()) {
            (void)rstd::fs::remove_file(staging.as_path());
            return io_failure<VerifiedFile>("replace cached acquisition file"_str,
                                            source.as_path(),
                                            rstd::move(removed).unwrap_err());
        }
    }
    auto published = rstd::fs::rename(staging.as_path(), source.as_path());
    if (published.is_err()) {
        (void)rstd::fs::remove_file(staging.as_path());
        return io_failure<VerifiedFile>("publish cached acquisition file"_str,
                                        source.as_path(),
                                        rstd::move(published).unwrap_err());
    }
    return Ok(VerifiedFile {
        .identity = archive_identity(request.url, request.sha256),
        .path     = rstd::move(source),
        .size     = staged_metadata->len(),
    });
}

struct FetchTask {
    usize        request {};
    VerifiedFile file;
};

auto extractor_invocation(const ArchiveExtractor& extractor, ref<rstd::path::Path> archive)
    -> AcquisitionResult<Vec<String>> {
    auto arguments = Vec<String>::make();
    rstd_try(process_path(arguments, extractor.tool.executable.as_path()));
    if (extractor.kind == ArchiveExtractorKind::CMakeTar) {
        arguments.push(String::make("-E"_str));
        arguments.push(String::make("tar"_str));
    }
    arguments.push(String::make("xvf"_str));
    rstd_try(process_path(arguments, archive));
    return Ok(rstd::move(arguments));
}

auto clean_extraction_destination(ref<rstd::path::Path> destination) -> AcquisitionResult<empty> {
    auto metadata = rstd::fs::symlink_metadata(destination);
    if (metadata.is_ok()) {
        if (! metadata->is_dir() || metadata->is_symlink()) {
            return failure<empty>(rstd::format(
                "archive extraction destination '{}' must be a real directory", destination));
        }
        auto removed = rstd::fs::remove_dir_all(destination);
        if (removed.is_err()) {
            return io_failure<empty>(
                "reset archive extraction"_str, destination, rstd::move(removed).unwrap_err());
        }
    } else {
        auto error = rstd::move(metadata).unwrap_err();
        if (error.kind() != rstd::io::error::ErrorKind { rstd::io::error::ErrorKind::NotFound }) {
            return io_failure<empty>(
                "inspect archive extraction"_str, destination, rstd::move(error));
        }
    }
    auto created = rstd::fs::create_dir_all(destination);
    if (created.is_err()) {
        return io_failure<empty>(
            "create archive extraction"_str, destination, rstd::move(created).unwrap_err());
    }
    return Ok(empty {});
}

} // namespace lito::tools::acquisition

export namespace lito::tools::acquisition
{

auto acquire_verified_files(Vec<VerifiedArchiveRequest>       requests,
                            usize                             jobs,
                            lito::tools::ToolResolver&        resolver,
                            const ResolvedProcessEnvironment& environment,
                            bool                              offline,
                            AcquisitionEventSink observer) -> AcquisitionResult<Vec<VerifiedFile>> {
    if (jobs == usize {}) {
        return failure<Vec<VerifiedFile>>("acquisition jobs must be greater than zero"_str);
    }
    auto files   = Vec<Option<VerifiedFile>>::with_capacity(requests.len());
    auto pending = Vec<usize>::make();
    for (usize index {}; index < requests.len(); ++index) {
        if (requests[index].seed.is_some()) {
            auto seed = rstd_try(verified_file(requests[index].seed->as_path(), requests[index]));
            if (seed.is_none()) {
                return failure<Vec<VerifiedFile>>(
                    rstd::format("acquisition seed '{}' does not match '{}'",
                                 requests[index].seed->as_path(),
                                 requests[index].label));
            }
            resolver.report_not_required(requests[index].download_requirement,
                                         "verified fetch seed is available"_str);
            files.push(Some(rstd::move(seed).unwrap()));
        } else {
            files.push(None());
            pending.push(usize(index));
        }
    }
    if (! pending.is_empty()) {
        auto data = LitoDataRoot::resolve();
        if (data.is_err()) return Err(rstd::into<AcquisitionError>(rstd::move(data).unwrap_err()));
        auto session = data->acquire_source_cache();
        if (session.is_err()) {
            return Err(rstd::into<AcquisitionError>(rstd::move(session).unwrap_err()));
        }
        auto layout = session->open_file_cache();
        if (layout.is_err()) {
            return Err(rstd::into<AcquisitionError>(rstd::move(layout).unwrap_err()));
        }
        auto downloads = Vec<usize>::make();
        for (const auto index : pending) {
            auto key = archive_fetch_key(requests[index].url, requests[index].sha256);
            auto cached =
                rstd_try(verified_file(layout->source(key.as_str()).as_path(), requests[index]));
            if (cached.is_some()) {
                resolver.report_not_required(requests[index].download_requirement,
                                             "verified file cache entry is available"_str);
                files[index] = Some(rstd::move(cached).unwrap());
            } else {
                downloads.push(usize(index));
            }
        }
        if (! downloads.is_empty() && offline) {
            return failure<Vec<VerifiedFile>>(
                rstd::format("offline acquisition cannot fetch '{}'",
                             requests[downloads[usize {}]].url.as_str()));
        }
        if (! downloads.is_empty()) {
            auto curl = resolver.require(lito::tools::Tool::Curl,
                                         requests[downloads[usize {}]].download_requirement);
            if (curl.is_err()) {
                return Err(AcquisitionError::Tools(String::make("resolve curl executable"_str),
                                                   rstd::move(curl).unwrap_err()));
            }
            const auto workers = jobs < downloads.len() ? jobs : downloads.len();
            auto created = rstd::thread::BlockingTaskGroup<AcquisitionResult<FetchTask>>::make(
                workers, downloads.len());
            if (created.is_err()) {
                return failure<Vec<VerifiedFile>>("cannot create acquisition executor"_str);
            }
            auto group = rstd::move(created).unwrap_unchecked();
            for (const auto index : downloads) {
                auto request      = rstd::move(requests[index]);
                auto task_session = session->clone();
                auto task_layout  = layout->clone();
                auto task_curl    = curl->executable.clone();
                auto task_env     = environment.clone();
                auto submitted = group.submit([index,
                                               request     = rstd::move(request),
                                               session     = rstd::move(task_session),
                                               layout      = rstd::move(task_layout),
                                               curl        = rstd::move(task_curl),
                                               environment = rstd::move(task_env),
                                               observer]() mutable -> AcquisitionResult<FetchTask> {
                    auto file = acquire_cached_file(rstd::move(request),
                                                    session,
                                                    layout,
                                                    curl.as_path(),
                                                    environment,
                                                    observer);
                    if (file.is_err()) return Err(rstd::move(file).unwrap_err());
                    return Ok(FetchTask {
                        .request = index,
                        .file    = rstd::move(file).unwrap(),
                    });
                });
                if (submitted.is_err()) {
                    return failure<Vec<VerifiedFile>>("cannot submit acquisition task"_str);
                }
            }
            auto outcomes = rstd::move(group).join();
            for (auto& outcome : outcomes) {
                auto value = rstd::move(outcome).into_value();
                if (value.is_none()) {
                    return failure<Vec<VerifiedFile>>("acquisition task was cancelled"_str);
                }
                auto task = rstd::move(value).unwrap_unchecked();
                if (task.is_err()) return Err(rstd::move(task).unwrap_err());
                auto complete           = rstd::move(task).unwrap();
                files[complete.request] = Some(rstd::move(complete.file));
            }
        }
    }
    auto result = Vec<VerifiedFile>::with_capacity(files.len());
    for (auto& file : files) {
        if (file.is_none()) return failure<Vec<VerifiedFile>>("acquisition result is missing"_str);
        result.push(rstd::move(file).unwrap());
    }
    return Ok(rstd::move(result));
}

auto select_archive_extractor(lito::tools::ToolResolver&              resolver,
                              const lito::tools::HostToolRequirement& requirement)
    -> AcquisitionResult<ArchiveExtractor> {
    lito::tools::Tool first  = lito::tools::Tool::BsdTar;
    lito::tools::Tool second = lito::tools::Tool::Tar;
    if (resolver.tools().explicitly_configured(lito::tools::Tool::Tar) &&
        ! resolver.tools().explicitly_configured(lito::tools::Tool::BsdTar)) {
        first  = lito::tools::Tool::Tar;
        second = lito::tools::Tool::BsdTar;
    }
    const lito::tools::Tool candidates[] = { first, second };
    auto                    attempts     = String::make();
    for (const auto candidate : candidates) {
        if (candidate == second &&
            resolver.tools().requested(first).as_os_str().as_encoded_bytes() ==
                resolver.tools().requested(second).as_os_str().as_encoded_bytes()) {
            continue;
        }
        auto probed = resolver.probe(candidate);
        if (probed.is_err()) {
            return Err(AcquisitionError::Tools(
                rstd::format("probe {} archive extractor", lito::tools::tool_name(candidate)),
                rstd::move(probed).unwrap_err()));
        }
        if (probed->is_some()) {
            auto result = ArchiveExtractor {
                .kind = candidate == lito::tools::Tool::BsdTar ? ArchiveExtractorKind::BsdTar
                                                               : ArchiveExtractorKind::Tar,
                .tool = rstd::move(probed).unwrap().unwrap(),
            };
            resolver.report(requirement, result.provider_name(), result.tool);
            return Ok(rstd::move(result));
        }
        resolver.report_candidate_missing(
            requirement, lito::tools::tool_name(candidate), candidate);
        attempts.push_str(rstd::format("\n    {} '{}': not found",
                                       lito::tools::tool_name(candidate),
                                       resolver.tools().requested(candidate))
                              .as_str());
    }
    auto cmake = resolver.probe(lito::tools::Tool::CMake);
    if (cmake.is_err()) {
        return Err(AcquisitionError::Tools(String::make("probe CMake archive extractor"_str),
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
    resolver.report_candidate_missing(requirement, "cmake -E tar"_str, lito::tools::Tool::CMake);
    attempts.push_str(rstd::format("\n    cmake -E tar via '{}': not found",
                                   resolver.tools().requested(lito::tools::Tool::CMake))
                          .as_str());
    return failure<ArchiveExtractor>(
        rstd::format("cannot provide {} required by {}; tried:{}",
                     lito::tools::host_tool_capability_name(requirement.capability),
                     lito::tools::host_tool_requirement_origin_text(requirement.origin),
                     attempts.as_str()));
}

auto extract_verified_archive(VerifiedFile                      file,
                              ref<rstd::path::Path>             destination,
                              Option<ref<str>>                  expected_root,
                              const ArchiveExtractor&           extractor,
                              const ResolvedProcessEnvironment& environment,
                              AcquisitionEventSink              observer)
    -> AcquisitionResult<ExtractedArchive> {
    rstd_try(clean_extraction_destination(destination));
    auto arguments = rstd_try(extractor_invocation(extractor, file.path.as_path()));
    if (observer.notify != nullptr) {
        observer.notify(observer.context,
                        AcquisitionEvent {
                            .kind        = AcquisitionEventKind::Extract,
                            .label       = file.identity.as_str(),
                            .source      = file.identity.as_str(),
                            .destination = destination,
                        });
    }
    auto started = rstd::time::Instant::now();
    auto status  = run_command(arguments, environment, Some(destination));
    if (observer.notify != nullptr) {
        observer.notify(observer.context,
                        AcquisitionEvent {
                            .kind        = AcquisitionEventKind::Extract,
                            .label       = file.identity.as_str(),
                            .source      = file.identity.as_str(),
                            .destination = destination,
                            .elapsed     = started.elapsed(),
                            .completed   = true,
                        });
    }
    if (status.is_err()) {
        (void)rstd::fs::remove_dir_all(destination);
        return Err(AcquisitionError::System(String::make("extract verified archive"_str),
                                            rstd::move(status).unwrap_err()));
    }
    if (status->exit_code != i32 {}) {
        (void)rstd::fs::remove_dir_all(destination);
        return failure<ExtractedArchive>(
            rstd::format("archive extraction with {} failed with exit code {}:\n{}{}",
                         extractor.provider_name(),
                         status->exit_code,
                         status->standard_output,
                         status->standard_error));
    }

    auto root = PathBuf::from(destination);
    if (expected_root.is_some()) {
        root.push(PathBuf::from(*expected_root).as_path());
        auto opened = rstd::fs::read_dir(destination);
        if (opened.is_err()) {
            return io_failure<ExtractedArchive>(
                "enumerate archive"_str, destination, rstd::move(opened).unwrap_err());
        }
        auto entries = rstd::move(opened).unwrap();
        auto count   = usize {};
        for (auto next : entries) {
            if (next.is_err()) {
                return io_failure<ExtractedArchive>(
                    "enumerate archive"_str, destination, rstd::move(next).unwrap_err());
            }
            auto entry = rstd::move(next).unwrap();
            ++count;
            if (entry.file_name().as_os_str().as_encoded_bytes() !=
                PathBuf::from(*expected_root).as_path().as_os_str().as_encoded_bytes()) {
                return failure<ExtractedArchive>(rstd::format(
                    "archive contains unexpected top-level entry '{}'", entry.path().as_path()));
            }
        }
        if (count != usize(1)) {
            return failure<ExtractedArchive>(
                rstd::format("archive root '{}' is not the only top-level entry", *expected_root));
        }
    } else {
        auto opened = rstd::fs::read_dir(destination);
        if (opened.is_err()) {
            return io_failure<ExtractedArchive>(
                "enumerate archive"_str, destination, rstd::move(opened).unwrap_err());
        }
        auto entries = rstd::move(opened).unwrap();
        auto only    = Option<PathBuf> {};
        auto count   = usize {};
        for (auto next : entries) {
            if (next.is_err()) {
                return io_failure<ExtractedArchive>(
                    "enumerate archive"_str, destination, rstd::move(next).unwrap_err());
            }
            auto entry = rstd::move(next).unwrap();
            ++count;
            if (count == usize(1)) {
                auto type = entry.file_type();
                if (type.is_err()) {
                    auto path = entry.path();
                    return io_failure<ExtractedArchive>(
                        "inspect archive entry"_str, path.as_path(), rstd::move(type).unwrap_err());
                }
                if (type->is_dir()) only = Some(entry.path());
            }
        }
        if (count == usize(1) && only.is_some()) root = rstd::move(only).unwrap();
    }
    auto canonical = rstd::fs::canonicalize(root.as_path());
    if (canonical.is_err()) {
        return io_failure<ExtractedArchive>(
            "resolve archive root"_str, root.as_path(), rstd::move(canonical).unwrap_err());
    }
    auto canonical_destination = rstd::fs::canonicalize(destination);
    if (canonical_destination.is_err()) {
        return io_failure<ExtractedArchive>("resolve archive destination"_str,
                                            destination,
                                            rstd::move(canonical_destination).unwrap_err());
    }
    if (! canonical->as_path().starts_with(canonical_destination->as_path())) {
        return failure<ExtractedArchive>(rstd::format(
            "archive root '{}' is outside extraction destination", canonical->as_path()));
    }
    auto metadata = rstd::fs::metadata(canonical->as_path());
    if (metadata.is_err()) {
        return io_failure<ExtractedArchive>(
            "inspect archive root"_str, canonical->as_path(), rstd::move(metadata).unwrap_err());
    }
    if (! metadata->is_dir()) {
        return failure<ExtractedArchive>(
            rstd::format("archive root '{}' is not a directory", canonical->as_path()));
    }
    return Ok(ExtractedArchive {
        .root     = rstd::move(canonical).unwrap(),
        .identity = rstd::move(file.identity),
    });
}

} // namespace lito::tools::acquisition
