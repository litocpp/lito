module;
#include <rstd/macro.hpp>

export module lito.driver:source.acquisition;

import rstd;
import lito.crypto;
import lito.core;
import lito.tools;
import lito.system;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;
using namespace lito::system;
using namespace lito::tools;
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

auto archive_source_identity(const lito::parse::FetchUrl&      url,
                             const lito::crypto::Sha256Digest& sha256) -> String {
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
    String                     owner;
    String                     name;
    lito::parse::FetchUrl      url;
    lito::crypto::Sha256Digest sha256;
};

struct ExternalSourceFetchOutcome {
    AcquiredSource             acquired;
    Vec<ResolvedPackageSource> sources;
};

} // namespace lito::source

using namespace lito::source;

auto acquisition_source_error(lito::tools::acquisition::AcquisitionError error) -> SourceError {
    auto operation = rstd::format("{}", error);
    return SourceError::Operation(rstd::move(operation),
                                  Box<dyn<rstd::error::Error>>::make(rstd::move(error)));
}

auto archive_tool_requirement(const ArchiveSourceFetchRequest& request,
                              lito::tools::HostToolCapability  capability)
    -> lito::tools::HostToolRequirement {
    return lito::tools::external_source_tool_requirement(
        capability,
        request.owner.is_empty() ? "archive"_str : request.owner.as_str(),
        request.name.is_empty() ? request.url.as_str() : request.name.as_str());
}

struct SourceAcquisitionObserver {
    SourceEventSink sink;
};

void observe_acquisition(void*                                             raw,
                         const lito::tools::acquisition::AcquisitionEvent& event) noexcept {
    auto& observer = *static_cast<SourceAcquisitionObserver*>(raw);
    if (observer.sink.notify == nullptr) return;
    observer.sink.notify(
        observer.sink.context,
        SourceEvent {
            .kind        = event.kind == lito::tools::acquisition::AcquisitionEventKind::Fetch
                               ? SourceEventKind::Fetch
                               : SourceEventKind::Extract,
            .source      = event.source,
            .destination = event.destination,
        });
}

auto acquisition_sink(SourceAcquisitionObserver& observer)
    -> lito::tools::acquisition::AcquisitionEventSink {
    return lito::tools::acquisition::AcquisitionEventSink {
        .context = rstd::addressof(observer),
        .notify  = observe_acquisition,
    };
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
    auto area = archives.join(PathBuf::from(lito::crypto::sha256_hex(identity)).as_path());
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

auto materialize_archive(lito::tools::acquisition::VerifiedFile            file,
                         ref<rstd::path::Path>                             materialization_root,
                         const lito::tools::acquisition::ArchiveExtractor& extractor,
                         const ResolvedProcessEnvironment&                 environment,
                         lito::tools::acquisition::AcquisitionEventSink    observer)
    -> SourceResult<AcquiredSource> {
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

    auto extracted = lito::tools::acquisition::extract_verified_archive(
        rstd::move(file), layout.extracted.as_path(), None(), extractor, environment, observer);
    if (extracted.is_err()) {
        return Err(acquisition_source_error(rstd::move(extracted).unwrap_err()));
    }
    auto relative = extracted->root.as_path().strip_prefix(layout.extracted.as_path());
    if (relative.is_none()) {
        return source_failure<AcquiredSource>(rstd::format(
            "archive root '{}' is outside materialization area", extracted->root.as_path()));
    }
    auto relative_text = relative->is_empty() ? "."_str : relative->to_str().unwrap_or(""_str);
    if (relative_text.is_empty()) {
        return source_failure<AcquiredSource>(
            rstd::format("archive root '{}' is not valid UTF-8", extracted->root.as_path()));
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
    auto archive = rstd::move(extracted).unwrap();
    return Ok(AcquiredSource {
        .root      = rstd::move(archive.root),
        .identity  = rstd::move(archive.identity),
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
                              lito::tools::ToolResolver&        resolver,
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
        auto key      = archive_source_identity(request.url, request.sha256);
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
        auto identity = archive_source_identity(unique[index].url, unique[index].sha256);
        auto reused =
            rstd_try(inspect_archive_materialization(materialization_root, identity.as_str()));
        materialized.push(rstd::move(reused));
        if (materialized[index].is_some()) {
            resolver.report_not_required(
                archive_tool_requirement(unique[index],
                                         lito::tools::HostToolCapability::ArchiveExtraction),
                "archive materialization is reusable"_str);
            resolver.report_not_required(
                archive_tool_requirement(unique[index],
                                         lito::tools::HostToolCapability::HttpDownload),
                "archive materialization is reusable"_str);
            continue;
        }
        pending_indices.push(usize(index));
        pending.push(rstd::move(unique[index]));
    }

    if (! pending.is_empty()) {
        const auto& first                  = pending[usize {}];
        auto        extraction_requirement = lito::tools::external_source_tool_requirement(
            lito::tools::HostToolCapability::ArchiveExtraction,
            first.owner.is_empty() ? "archive"_str : first.owner.as_str(),
            first.name.is_empty() ? first.url.as_str() : first.name.as_str());
        auto requests =
            Vec<lito::tools::acquisition::VerifiedArchiveRequest>::with_capacity(pending.len());
        for (auto& request : pending) {
            auto identity = archive_fetch_identity(request.url.clone(), request.sha256.clone());
            auto seed     = rstd_try(locate_fetch_seed(source_config.fetch_seeds, identity));
            requests.push(lito::tools::acquisition::VerifiedArchiveRequest {
                .label                = request.name.is_empty() ? String::make(request.url.as_str())
                                                                : request.name.clone(),
                .url                  = request.url.clone(),
                .sha256               = request.sha256.clone(),
                .seed                 = rstd::move(seed),
                .download_requirement = archive_tool_requirement(
                    request, lito::tools::HostToolCapability::HttpDownload),
                .extraction_requirement = archive_tool_requirement(
                    request, lito::tools::HostToolCapability::ArchiveExtraction),
            });
        }
        auto source_observer = SourceAcquisitionObserver { .sink = observer };
        auto files_result    = lito::tools::acquisition::acquire_verified_files(
            rstd::move(requests),
            jobs,
            resolver,
            environment,
            source_config.network == NetworkPolicy::Offline,
            acquisition_sink(source_observer));
        if (files_result.is_err()) {
            auto error = rstd::move(files_result).unwrap_err();
            if (source_config.network == NetworkPolicy::Offline) {
                auto operation = rstd::format("offline source resolution failed: {}", error);
                return Err(SourceError::Operation(
                    rstd::move(operation), Box<dyn<rstd::error::Error>>::make(rstd::move(error))));
            }
            return Err(acquisition_source_error(rstd::move(error)));
        }
        auto files              = rstd::move(files_result).unwrap();
        auto extraction_files   = Vec<lito::tools::acquisition::VerifiedFile>::make();
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
            auto extractor_result = lito::tools::acquisition::select_archive_extractor(
                resolver, extraction_requirement);
            if (extractor_result.is_err()) {
                return Err(acquisition_source_error(rstd::move(extractor_result).unwrap_err()));
            }
            auto extractor    = rstd::move(extractor_result).unwrap();
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
                auto task_observer  = acquisition_sink(source_observer);
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
