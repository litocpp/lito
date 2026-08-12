module;
#include <rstd/macro.hpp>

export module lito.source:acquisition;

import rstd;
import lito.error;
import lito.source.contract;
import lito.system.environment;
import lito.system.storage;
import lito.system.process;
import :support;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito
{

auto path_source_identity(ref<rstd::path::Path> path) -> String {
    return rstd::format("path+{}", path);
}

auto git_source_identity(ref<str> url, ref<str> commit) -> String {
    return rstd::format("git+{}#{}", url, commit);
}

auto archive_source_identity(ref<str> url, ref<str> sha256) -> String {
    return rstd::format("archive+{}#sha256:{}", url, sha256);
}

struct SelectedSourcePackage {
    String          source_identity;
    PathBuf         manifest;
    PackageManifest package;
};

struct AcquiredSource {
    PathBuf root;
    String  identity;
    bool    cacheable { false };
};

struct AcquiredProjectSources {
    usize         primary;
    Option<usize> tests;
};

struct PackageSourceFetchRequest {
    PackageSourceRequirement source;
    PathBuf                  declaring_root;
};

struct ArchiveSourceFetchRequest {
    String url;
    String sha256;
};

struct ExternalSourceFetchOutcome {
    AcquiredSource             acquired;
    Vec<ResolvedPackageSource> sources;
};

auto acquire_archive_source(ref<str>                          url,
                            ref<str>                          sha256,
                            ref<rstd::path::Path>             cmake_executable,
                            const ResolvedProcessEnvironment& environment)
    -> Result<AcquiredSource> {
    auto executable = cmake_executable.to_str();
    if (executable.is_none()) {
        return source_failure<AcquiredSource>(rstd::format(
            "CMake executable '{}' for archive acquisition is not valid UTF-8", cmake_executable));
    }
    auto identity = archive_source_identity(url, sha256);
    auto cache    = lito_cache_directory(PathBuf::from("sources/archives"_str).as_path(),
                                         "archive sources"_str);
    if (cache.is_err()) return Err(rstd::move(cache).unwrap_err());
    auto bucket  = cache->join(PathBuf::from(source_hash(identity.as_str())).as_path());
    auto created = rstd::fs::create_dir_all(bucket.as_path());
    if (created.is_err()) {
        return source_failure<AcquiredSource>(
            rstd::format("cannot create archive source cache '{}': {}",
                         bucket.as_path(),
                         rstd::move(created).unwrap_err()));
    }
    auto lock_path = bucket.join(PathBuf::from("lock"_str).as_path());
    auto lock_file = rstd::fs::File::create(lock_path.as_path());
    if (lock_file.is_err()) {
        return source_failure<AcquiredSource>(
            rstd::format("cannot open archive source lock '{}': {}",
                         lock_path.as_path(),
                         rstd::move(lock_file).unwrap_err()));
    }
    auto locked = lock_file->lock();
    if (locked.is_err()) {
        return source_failure<AcquiredSource>(
            rstd::format("cannot lock archive source cache '{}': {}",
                         bucket.as_path(),
                         rstd::move(locked).unwrap_err()));
    }

    auto receipt = bucket.join(PathBuf::from("source-v1"_str).as_path());
    auto current = rstd::fs::read_to_string(receipt.as_path());
    if (current.is_ok()) {
        auto path      = PathBuf::from(current->as_str().trim_ascii());
        auto canonical = rstd::fs::canonicalize(path.as_path());
        if (canonical.is_ok() && canonical->as_path().starts_with(bucket.as_path())) {
            return Ok(AcquiredSource {
                .root      = rstd::move(canonical).unwrap(),
                .identity  = rstd::move(identity),
                .cacheable = true,
            });
        }
    }

    auto extracted = bucket.join(PathBuf::from("extracted"_str).as_path());
    auto exists    = rstd::fs::exists(extracted.as_path());
    if (exists.is_err()) {
        return source_failure<AcquiredSource>(
            rstd::format("cannot inspect archive extraction directory '{}': {}",
                         extracted.as_path(),
                         rstd::move(exists).unwrap_err()));
    }
    if (*exists) {
        auto removed = rstd::fs::remove_dir_all(extracted.as_path());
        if (removed.is_err()) {
            return source_failure<AcquiredSource>(
                rstd::format("cannot reset archive extraction directory '{}': {}",
                             extracted.as_path(),
                             rstd::move(removed).unwrap_err()));
        }
    }
    created = rstd::fs::create_dir_all(extracted.as_path());
    if (created.is_err()) {
        return source_failure<AcquiredSource>(
            rstd::format("cannot create archive extraction directory '{}': {}",
                         extracted.as_path(),
                         rstd::move(created).unwrap_err()));
    }

    auto archive     = bucket.join(PathBuf::from("source.archive"_str).as_path());
    auto script      = bucket.join(PathBuf::from("download-v1.cmake"_str).as_path());
    auto script_text = "file(DOWNLOAD \"${SOURCE_URL}\" \"${ARCHIVE_FILE}\"\n"
                       "     EXPECTED_HASH \"SHA256=${SOURCE_SHA256}\"\n"
                       "     STATUS download_status)\n"
                       "list(GET download_status 0 download_code)\n"
                       "list(GET download_status 1 download_message)\n"
                       "if(NOT download_code EQUAL 0)\n"
                       "  message(FATAL_ERROR \"archive download failed: ${download_message}\")\n"
                       "endif()\n"_str;
    auto written     = rstd::fs::write_atomic(script.as_path(), script_text.as_bytes());
    if (written.is_err()) {
        return source_failure<AcquiredSource>(
            rstd::format("cannot write archive download script '{}': {}",
                         script.as_path(),
                         rstd::move(written).unwrap_err()));
    }
    auto archive_text = archive.as_path().to_str();
    auto script_path  = script.as_path().to_str();
    if (archive_text.is_none() || script_path.is_none() || archive_text->contains(";"_str) ||
        script_path->contains(";"_str)) {
        return source_failure<AcquiredSource>(
            "archive source cache paths must be UTF-8 and cannot contain ';'"_str);
    }
    auto arguments = Vec<String>::make();
    arguments.push(String::make(*executable));
    arguments.push(rstd::format("-DSOURCE_URL={}", url));
    arguments.push(rstd::format("-DSOURCE_SHA256={}", sha256));
    arguments.push(rstd::format("-DARCHIVE_FILE={}", *archive_text));
    arguments.push(String::make("-P"_str));
    arguments.push(String::make(*script_path));
    auto downloaded = run_command(arguments, environment);
    if (downloaded.is_err()) {
        return source_failure<AcquiredSource>(
            rstd::format("archive source '{}' download could not execute: {}",
                         url,
                         rstd::move(downloaded).unwrap_err().message.as_str()));
    }
    if (downloaded->exit_code != i32 {}) {
        return source_failure<AcquiredSource>(
            rstd::format("archive source '{}' download failed with exit code {}:\n{}{}",
                         url,
                         downloaded->exit_code,
                         downloaded->standard_output.as_str(),
                         downloaded->standard_error.as_str()));
    }

    arguments = Vec<String>::make();
    arguments.push(String::make(*executable));
    arguments.push(String::make("-E"_str));
    arguments.push(String::make("tar"_str));
    arguments.push(String::make("xvf"_str));
    arguments.push(String::make(*archive_text));
    auto extracted_status = run_command(arguments, environment, Some(extracted.as_path()));
    if (extracted_status.is_err()) {
        return source_failure<AcquiredSource>(
            rstd::format("archive source '{}' extraction could not execute: {}",
                         url,
                         rstd::move(extracted_status).unwrap_err().message.as_str()));
    }
    if (extracted_status->exit_code != i32 {}) {
        return source_failure<AcquiredSource>(
            rstd::format("archive source '{}' extraction failed with exit code {}:\n{}{}",
                         url,
                         extracted_status->exit_code,
                         extracted_status->standard_output.as_str(),
                         extracted_status->standard_error.as_str()));
    }

    auto opened = rstd::fs::read_dir(extracted.as_path());
    if (opened.is_err()) {
        return source_failure<AcquiredSource>(
            rstd::format("cannot enumerate archive source '{}': {}",
                         extracted.as_path(),
                         rstd::move(opened).unwrap_err()));
    }
    auto root       = extracted.clone();
    auto only_entry = Option<PathBuf> {};
    auto count      = usize {};
    auto entries    = rstd::move(opened).unwrap();
    for (auto next = entries.next(); next.is_some(); next = entries.next()) {
        if (next->is_err()) {
            return source_failure<AcquiredSource>(
                rstd::format("cannot enumerate archive source '{}': {}",
                             extracted.as_path(),
                             rstd::move(*next).unwrap_err()));
        }
        auto entry = rstd::move(*next).unwrap();
        ++count;
        if (count == usize(1)) {
            auto type = entry.file_type();
            if (type.is_err()) {
                return source_failure<AcquiredSource>(
                    rstd::format("cannot inspect archive source entry '{}': {}",
                                 entry.path().as_path(),
                                 rstd::move(type).unwrap_err()));
            }
            if (type->is_dir()) only_entry = Some(entry.path());
        }
    }
    if (count == usize(1) && only_entry.is_some()) root = rstd::move(only_entry).unwrap();
    auto canonical = rstd::fs::canonicalize(root.as_path());
    if (canonical.is_err()) {
        return source_failure<AcquiredSource>(
            rstd::format("cannot resolve archive source root '{}': {}",
                         root.as_path(),
                         rstd::move(canonical).unwrap_err()));
    }
    auto root_text = canonical->as_path().to_str();
    if (root_text.is_none()) {
        return source_failure<AcquiredSource>(
            rstd::format("archive source root '{}' is not valid UTF-8", canonical->as_path()));
    }
    auto receipt_text = String::make(*root_text);
    receipt_text.push('\n');
    written = rstd::fs::write_atomic(receipt.as_path(), receipt_text.as_str().as_bytes());
    if (written.is_err()) {
        return source_failure<AcquiredSource>(
            rstd::format("cannot publish archive source receipt '{}': {}",
                         receipt.as_path(),
                         rstd::move(written).unwrap_err()));
    }
    return Ok(AcquiredSource {
        .root      = rstd::move(canonical).unwrap(),
        .identity  = rstd::move(identity),
        .cacheable = true,
    });
}

auto acquire_archive_frontier(Vec<ArchiveSourceFetchRequest>    requests,
                              usize                             jobs,
                              ref<rstd::path::Path>             cmake_executable,
                              const ResolvedProcessEnvironment& environment)
    -> Result<Vec<AcquiredSource>> {
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

    struct FetchedArchiveSource {
        usize          request {};
        AcquiredSource source;
    };
    auto worker_count = jobs < unique.len() ? jobs : unique.len();
    auto created      = rstd::thread::BlockingTaskGroup<Result<FetchedArchiveSource>>::make(
        worker_count, unique.len());
    if (created.is_err()) {
        return source_failure<Vec<AcquiredSource>>(
            rstd::format("cannot create archive source fetch executor: {}",
                         rstd::move(created).unwrap_err_unchecked()));
    }
    auto group = rstd::move(created).unwrap_unchecked();
    for (usize index {}; index < unique.len(); ++index) {
        auto request    = rstd::move(unique[index]);
        auto executable = PathBuf::from(cmake_executable);
        auto task_env   = environment.clone();
        auto submitted  = group.submit(
            [index,
             request    = rstd::move(request),
             executable = rstd::move(executable),
             task_env   = rstd::move(task_env)]() mutable -> Result<FetchedArchiveSource> {
                auto acquired = acquire_archive_source(
                    request.url.as_str(), request.sha256.as_str(), executable.as_path(), task_env);
                if (acquired.is_err()) return Err(rstd::move(acquired).unwrap_err());
                return Ok(FetchedArchiveSource {
                    .request = index,
                    .source  = rstd::move(acquired).unwrap(),
                });
            });
        if (submitted.is_err()) {
            return source_failure<Vec<AcquiredSource>>(
                "cannot submit archive source fetch task"_str);
        }
    }
    auto outcomes = rstd::move(group).join();
    auto fetched  = Vec<Option<AcquiredSource>>::with_capacity(unique.len());
    for (usize index {}; index < unique.len(); ++index) fetched.push(None());
    for (auto& outcome : outcomes) {
        auto value = rstd::move(outcome).into_value();
        if (value.is_none()) {
            return source_failure<Vec<AcquiredSource>>(
                "archive source fetch task was cancelled"_str);
        }
        auto task = rstd::move(value).unwrap_unchecked();
        if (task.is_err()) return Err(rstd::move(task).unwrap_err());
        auto source             = rstd::move(task).unwrap();
        fetched[source.request] = Some(rstd::move(source.source));
    }
    for (auto binding : bindings) {
        if (fetched[binding].is_none()) {
            return source_failure<Vec<AcquiredSource>>(
                "archive source fetch result is missing"_str);
        }
        const auto& source = *fetched[binding];
        result.push(AcquiredSource {
            .root      = source.root.clone(),
            .identity  = source.identity.clone(),
            .cacheable = source.cacheable,
        });
    }
    return Ok(rstd::move(result));
}

} // namespace lito
