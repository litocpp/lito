module;
#include <rstd/macro.hpp>

export module lito.source;

import rstd;
import lito.model;
import lito.storage;
import lito.manifest;
import lito.process;
import lito.environment;
import lito.workspace;

using namespace rstd::prelude;
using namespace rstd::literals;
using IndexMap = rstd::collections::BTreeMap<String, usize>;

namespace lito
{

template<typename T>
auto source_failure(String message) -> Result<T> {
    return Err(Error::make(ErrorKind::Dependency, rstd::move(message)));
}

template<typename T>
auto source_failure(ref<str> message) -> Result<T> {
    return Err(Error::make(ErrorKind::Dependency, message));
}

auto path_components(ref<rstd::path::Path> path) -> Result<Vec<String>> {
    auto result     = Vec<String>::make();
    auto components = path.components();
    for (auto component = components.next(); component.is_some(); component = components.next()) {
        if (component->is_root_dir() || component->is_cur_dir()) continue;
        if (component->is_parent_dir()) {
            return source_failure<Vec<String>>(
                rstd::format("canonical path '{}' contains a parent component", path));
        }
        auto text = component->as_os_str().to_str();
        if (text.is_none()) {
            return source_failure<Vec<String>>(
                rstd::format("canonical path '{}' contains a non-UTF-8 component", path));
        }
        result.push(String::make(*text));
    }
    return Ok(rstd::move(result));
}

auto relative_path(ref<rstd::path::Path> root, ref<rstd::path::Path> target) -> Result<PathBuf> {
    auto root_components   = path_components(root);
    auto target_components = path_components(target);
    if (root_components.is_err()) return Err(rstd::move(root_components).unwrap_err());
    if (target_components.is_err()) {
        return Err(rstd::move(target_components).unwrap_err());
    }
    auto  roots   = rstd::move(root_components).unwrap();
    auto  targets = rstd::move(target_components).unwrap();
    usize common {};
    while (common < roots.len() && common < targets.len() && roots[common] == targets[common]) {
        ++common;
    }
    auto relative = PathBuf::make();
    for (auto index = common; index < roots.len(); ++index) {
        relative.push(PathBuf::from(".."_str).as_path());
    }
    for (auto index = common; index < targets.len(); ++index) {
        relative.push(PathBuf::from(targets[index].as_str()).as_path());
    }
    return Ok(relative.is_empty() ? PathBuf::from("."_str) : rstd::move(relative));
}

inline constexpr uint64_t FNV_OFFSET = 14695981039346656037ull;
inline constexpr uint64_t FNV_PRIME  = 1099511628211ull;

auto source_hash(ref<str> value) -> String {
    auto hash = FNV_OFFSET;
    for (auto byte : value) {
        hash ^= byte.to_primitive();
        hash *= FNV_PRIME;
    }
    static constexpr char digits[] = "0123456789abcdef";
    char                  result[16];
    for (size_t index = 0; index < 16; ++index) {
        result[15 - index] = digits[hash & 0xfu];
        hash >>= 4u;
    }
    return String::make(
        ref<str>::from_raw_parts_unchecked(reinterpret_cast<const byte*>(result), usize(16)));
}

auto push_path(Vec<String>& arguments, ref<rstd::path::Path> path) -> Result<empty> {
    auto text = path.to_str();
    if (text.is_none()) {
        return source_failure<empty>(rstd::format("Git cache path '{}' is not valid UTF-8", path));
    }
    arguments.push(String::make(*text));
    return Ok(empty {});
}

auto git_output(Vec<String>                       arguments,
                ref<str>                          operation,
                const ResolvedProcessEnvironment& environment) -> Result<String> {
    auto output = run_command(arguments, environment);
    if (output.is_err()) {
        auto error = rstd::move(output).unwrap_err();
        return source_failure<String>(rstd::format("{}: {}", operation, error.message.as_str()));
    }
    auto value = rstd::move(output).unwrap();
    if (value.exit_code != i32 {}) {
        return source_failure<String>(rstd::format("{} failed with exit code {}:\n{}",
                                                   operation,
                                                   value.exit_code,
                                                   value.standard_error.as_str()));
    }
    return Ok(trim_ascii(rstd::move(value.standard_output)));
}

auto git_status(Vec<String>                       arguments,
                ref<str>                          operation,
                const ResolvedProcessEnvironment& environment) -> Result<empty> {
    auto output = git_output(rstd::move(arguments), operation, environment);
    if (output.is_err()) return Err(rstd::move(output).unwrap_err());
    return Ok(empty {});
}

auto same_reference(const GitReference& left, const GitReference& right) -> bool {
    return left.kind == right.kind && left.value == right.value;
}

auto load_git_catalog(ref<rstd::path::Path> root) -> Result<WorkspaceCatalog> {
    auto document = load_manifest_document(root);
    if (document.is_err()) return Err(rstd::move(document).unwrap_err());
    auto loaded = rstd::move(document).unwrap();
    if (loaded.kind == ManifestKind::Workspace && loaded.workspace.is_some()) {
        return load_workspace_catalog(rstd::move(loaded.workspace).unwrap());
    }
    if (loaded.kind == ManifestKind::Package && loaded.package.is_some()) {
        return WorkspaceCatalog::single(rstd::move(loaded.package).unwrap());
    }
    return source_failure<WorkspaceCatalog>(
        "Git source root manifest has no package or workspace"_str);
}

struct SourceEntry {
    ResolvedPackageSource    source;
    Option<WorkspaceCatalog> catalog;
};

} // namespace lito

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

class SourceManager {
    PathBuf                           graph_root_;
    PackageResolutionOptions          options_;
    Vec<SourceEntry>                  entries_;
    IndexMap                          roots_ { IndexMap::make() };
    IndexMap                          source_identities_ { IndexMap::make() };
    ToolResolver*                     resolver_ {};
    const ResolvedProcessEnvironment* environment_ {};
    Option<PathBuf>                   git_;

    struct FetchedPackageSource {
        usize       request {};
        SourceEntry entry;
    };

    struct FetchedExternalSource {
        usize                      request {};
        ExternalSourceFetchOutcome outcome;
    };

    auto source_request_key(const PackageSourceRequirement& source,
                            ref<rstd::path::Path>           declaring_root) -> Result<String> {
        if (source.is_Path()) {
            return Ok(
                rstd::format("path\n{}\n{}", declaring_root, source.as_Path().path.as_path()));
        }
        const auto& git   = source.as_Git();
        auto        patch = patched_path(git.url.as_str());
        if (patch.is_err()) return Err(rstd::move(patch).unwrap_err());
        if (patch->is_some()) return Ok(rstd::format("patch\n{}", **patch));
        auto kind = "default"_str;
        switch (git.reference.kind) {
        case GitReferenceKind::DefaultBranch: break;
        case GitReferenceKind::Branch: kind = "branch"_str; break;
        case GitReferenceKind::Tag: kind = "tag"_str; break;
        case GitReferenceKind::Rev: kind = "rev"_str; break;
        case GitReferenceKind::Commit: kind = "commit"_str; break;
        }
        return Ok(
            rstd::format("git\n{}\n{}\n{}", git.url.as_str(), kind, git.reference.value.as_str()));
    }

    auto take_entry(usize index) -> SourceEntry { return rstd::move(entries_[index]); }

    auto absorb_entry(SourceEntry entry) -> usize {
        auto existing = source_identities_.get(entry.source.identity.as_str());
        if (existing.is_some()) {
            if (entries_[**existing].catalog.is_none() && entry.catalog.is_some()) {
                entries_[**existing].catalog = rstd::move(entry.catalog);
            }
            return **existing;
        }
        auto root_text = entry.source.root_directory.as_path().to_str();
        if (root_text.is_some()) {
            auto by_root = roots_.get(*root_text);
            if (by_root.is_some()) {
                if (entries_[**by_root].catalog.is_none() && entry.catalog.is_some()) {
                    entries_[**by_root].catalog = rstd::move(entry.catalog);
                }
                return **by_root;
            }
        }
        auto index = entries_.len();
        if (root_text.is_some()) roots_.insert(String::make(*root_text), index);
        source_identities_.insert(entry.source.identity.clone(), index);
        entries_.push(rstd::move(entry));
        return index;
    }

    auto clone_source(const ResolvedPackageSource& source) -> ResolvedPackageSource {
        return ResolvedPackageSource {
            .identity       = source.identity.clone(),
            .kind           = source.kind,
            .root_directory = source.root_directory.clone(),
            .path           = source.path.clone(),
            .git            = source.git.clone(),
            .reference =
                GitReference {
                    .kind  = source.reference.kind,
                    .value = source.reference.value.clone(),
                },
            .commit = source.commit.clone(),
        };
    }

    auto clone_external_outcome(const ExternalSourceFetchOutcome& outcome)
        -> ExternalSourceFetchOutcome {
        auto sources = Vec<ResolvedPackageSource>::with_capacity(outcome.sources.len());
        for (const auto& source : outcome.sources) sources.push(clone_source(source));
        return ExternalSourceFetchOutcome {
            .acquired =
                AcquiredSource {
                    .root      = outcome.acquired.root.clone(),
                    .identity  = outcome.acquired.identity.clone(),
                    .cacheable = outcome.acquired.cacheable,
                },
            .sources = rstd::move(sources),
        };
    }

    auto git_command() -> Result<Vec<String>> {
        if (git_.is_none()) {
            auto resolved =
                resolver_->resolve(PathBuf::from("git"_str).as_path(), "Git executable"_str);
            if (resolved.is_err()) {
                auto error = rstd::move(resolved).unwrap_err();
                return source_failure<Vec<String>>(rstd::move(error.message));
            }
            git_ = Some(rstd::move(resolved).unwrap().executable);
        }
        auto arguments = Vec<String>::make();
        auto pushed    = push_path(arguments, git_->as_path());
        if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
        return Ok(rstd::move(arguments));
    }

    auto patched_path(ref<str> url) -> Result<Option<ref<rstd::path::Path>>> {
        auto matched = Option<ref<rstd::path::Path>> {};
        for (const auto& patch : options_.sources.patches) {
            if (patch.git.as_str() != url) continue;
            if (matched.is_some()) {
                return source_failure<Option<ref<rstd::path::Path>>>(rstd::format(
                    "source configuration contains more than one patch for '{}'", url));
            }
            matched = Some(patch.path.as_path());
        }
        return Ok(matched);
    }

    auto locked_source(ref<str> url, const GitReference& reference)
        -> Result<Option<ref<LockedGitSource>>> {
        auto matched = Option<ref<LockedGitSource>> {};
        for (const auto& source : options_.git_sources) {
            if (source.git.as_str() != url || ! same_reference(source.reference, reference)) {
                continue;
            }
            if (matched.is_some()) {
                return source_failure<Option<ref<LockedGitSource>>>(
                    rstd::format("lock contains more than one Git source for '{}'", url));
            }
            matched = Some(ref<LockedGitSource>::from_raw_parts(rstd::addressof(source)));
        }
        return Ok(matched);
    }

    auto repository(ref<rstd::path::Path> bucket, ref<str> url) -> Result<PathBuf> {
        auto repository = PathBuf::from(bucket).join(PathBuf::from("repository.git"_str).as_path());
        auto exists     = rstd::fs::exists(repository.as_path());
        if (exists.is_err()) {
            return source_failure<PathBuf>(
                rstd::format("cannot inspect Git cache repository '{}': {}",
                             repository.as_path(),
                             rstd::move(exists).unwrap_err()));
        }
        if (! *exists) {
            auto init = rstd_try(git_command());
            init.push(String::make("init"_str));
            init.push(String::make("--bare"_str));
            auto path = push_path(init, repository.as_path());
            if (path.is_err()) return Err(rstd::move(path).unwrap_err());
            auto initialized =
                git_status(rstd::move(init), "Git cache initialization"_str, *environment_);
            if (initialized.is_err()) return Err(rstd::move(initialized).unwrap_err());
        }

        auto remote = rstd_try(git_command());
        remote.push(String::make("--git-dir"_str));
        auto path = push_path(remote, repository.as_path());
        if (path.is_err()) return Err(rstd::move(path).unwrap_err());
        remote.push(String::make("config"_str));
        remote.push(String::make("--get"_str));
        remote.push(String::make("remote.origin.url"_str));
        auto inspected = run_command(remote, *environment_);
        if (inspected.is_err()) {
            auto error = rstd::move(inspected).unwrap_err();
            return source_failure<PathBuf>(
                rstd::format("Git cache remote inspection: {}", error.message.as_str()));
        }
        auto inspection = rstd::move(inspected).unwrap();
        auto configured = String::make();
        if (inspection.exit_code == i32 {}) {
            configured = trim_ascii(rstd::move(inspection.standard_output));
        } else {
            auto add = rstd_try(git_command());
            add.push(String::make("--git-dir"_str));
            path = push_path(add, repository.as_path());
            if (path.is_err()) return Err(rstd::move(path).unwrap_err());
            add.push(String::make("remote"_str));
            add.push(String::make("add"_str));
            add.push(String::make("origin"_str));
            add.push(String::make(url));
            auto added =
                git_status(rstd::move(add), "Git cache remote configuration"_str, *environment_);
            if (added.is_err()) return Err(rstd::move(added).unwrap_err());
            configured = String::make(url);
        }
        if (configured.as_str() != url) {
            return source_failure<PathBuf>(rstd::format(
                "Git cache key collision between '{}' and '{}'", configured.as_str(), url));
        }
        return Ok(rstd::move(repository));
    }

    auto object_exists(ref<rstd::path::Path> repository, ref<str> commit) -> Result<bool> {
        auto arguments = rstd_try(git_command());
        arguments.push(String::make("--git-dir"_str));
        auto path = push_path(arguments, repository);
        if (path.is_err()) return Err(rstd::move(path).unwrap_err());
        arguments.push(String::make("cat-file"_str));
        arguments.push(String::make("-e"_str));
        arguments.push(rstd::format("{}^{{commit}}", commit));
        auto output = run_command(arguments, *environment_);
        if (output.is_err()) {
            auto error = rstd::move(output).unwrap_err();
            return source_failure<bool>(
                rstd::format("Git object inspection: {}", error.message.as_str()));
        }
        return Ok(output->exit_code == i32 {});
    }

    auto fetch(ref<rstd::path::Path> repository, ref<str> revision) -> Result<empty> {
        auto arguments = rstd_try(git_command());
        arguments.push(String::make("--git-dir"_str));
        auto path = push_path(arguments, repository);
        if (path.is_err()) return Err(rstd::move(path).unwrap_err());
        arguments.push(String::make("fetch"_str));
        arguments.push(String::make("--force"_str));
        arguments.push(String::make("--no-tags"_str));
        arguments.push(String::make("origin"_str));
        arguments.push(String::make(revision));
        return git_status(rstd::move(arguments), "Git source fetch"_str, *environment_);
    }

    auto rev_parse(ref<rstd::path::Path> repository, ref<str> revision) -> Result<String> {
        auto arguments = rstd_try(git_command());
        arguments.push(String::make("--git-dir"_str));
        auto path = push_path(arguments, repository);
        if (path.is_err()) return Err(rstd::move(path).unwrap_err());
        arguments.push(String::make("rev-parse"_str));
        arguments.push(String::make("--verify"_str));
        arguments.push(rstd::format("{}^{{commit}}", revision));
        auto commit =
            git_output(rstd::move(arguments), "Git source revision resolution"_str, *environment_);
        if (commit.is_err()) return Err(rstd::move(commit).unwrap_err());
        if (commit->len() != usize(40)) {
            return source_failure<String>(rstd::format(
                "Git resolved '{}' to non-full object id '{}'", revision, commit->as_str()));
        }
        return commit;
    }

    auto resolve_commit(ref<rstd::path::Path>        repository,
                        const GitReference&          reference,
                        Option<ref<LockedGitSource>> locked) -> Result<String> {
        if (locked.is_some()) {
            auto present = object_exists(repository, (*locked)->commit.as_str());
            if (present.is_err()) return Err(rstd::move(present).unwrap_err());
            if (! *present) {
                auto fetched = fetch(repository, (*locked)->commit.as_str());
                if (fetched.is_err()) return Err(rstd::move(fetched).unwrap_err());
            }
            return rev_parse(repository, (*locked)->commit.as_str());
        }

        auto revision = String::make("HEAD"_str);
        if (reference.kind == GitReferenceKind::Branch) {
            revision = rstd::format("refs/heads/{}", reference.value.as_str());
        } else if (reference.kind == GitReferenceKind::Tag) {
            revision = rstd::format("refs/tags/{}", reference.value.as_str());
        } else if (reference.kind == GitReferenceKind::Rev ||
                   reference.kind == GitReferenceKind::Commit) {
            revision = reference.value.clone();
        }
        auto fetched = fetch(repository, revision.as_str());
        if (fetched.is_err()) return Err(rstd::move(fetched).unwrap_err());
        return rev_parse(repository, "FETCH_HEAD"_str);
    }

    auto checkout(ref<rstd::path::Path> bucket, ref<rstd::path::Path> repository, ref<str> commit)
        -> Result<PathBuf> {
        auto checkouts = PathBuf::from(bucket).join(PathBuf::from("checkouts"_str).as_path());
        auto created   = rstd::fs::create_dir_all(checkouts.as_path());
        if (created.is_err()) {
            return source_failure<PathBuf>(rstd::format("cannot create Git checkout cache '{}': {}",
                                                        checkouts.as_path(),
                                                        rstd::move(created).unwrap_err()));
        }
        auto checkout = checkouts.join(PathBuf::from(commit).as_path());
        auto exists   = rstd::fs::exists(checkout.as_path());
        if (exists.is_err()) {
            return source_failure<PathBuf>(rstd::format("cannot inspect Git checkout '{}': {}",
                                                        checkout.as_path(),
                                                        rstd::move(exists).unwrap_err()));
        }
        if (*exists) {
            auto arguments = rstd_try(git_command());
            arguments.push(String::make("-C"_str));
            auto path = push_path(arguments, checkout.as_path());
            if (path.is_err()) return Err(rstd::move(path).unwrap_err());
            arguments.push(String::make("rev-parse"_str));
            arguments.push(String::make("--verify"_str));
            arguments.push(String::make("HEAD"_str));
            auto current =
                git_output(rstd::move(arguments), "Git checkout inspection"_str, *environment_);
            if (current.is_ok() && current->as_str() == commit) {
                return Ok(rstd::move(checkout));
            }
            auto removed = rstd::fs::remove_dir_all(checkout.as_path());
            if (removed.is_err()) {
                return source_failure<PathBuf>(rstd::format("cannot recover Git checkout '{}': {}",
                                                            checkout.as_path(),
                                                            rstd::move(removed).unwrap_err()));
            }
        }

        auto clone = rstd_try(git_command());
        clone.push(String::make("clone"_str));
        clone.push(String::make("--no-checkout"_str));
        clone.push(String::make("--shared"_str));
        auto path = push_path(clone, repository);
        if (path.is_err()) return Err(rstd::move(path).unwrap_err());
        path = push_path(clone, checkout.as_path());
        if (path.is_err()) return Err(rstd::move(path).unwrap_err());
        auto cloned =
            git_status(rstd::move(clone), "Git source checkout creation"_str, *environment_);
        if (cloned.is_err()) return Err(rstd::move(cloned).unwrap_err());

        auto detach = rstd_try(git_command());
        detach.push(String::make("-C"_str));
        path = push_path(detach, checkout.as_path());
        if (path.is_err()) return Err(rstd::move(path).unwrap_err());
        detach.push(String::make("checkout"_str));
        detach.push(String::make("--detach"_str));
        detach.push(String::make(commit));
        auto checked_out = git_status(rstd::move(detach), "Git source checkout"_str, *environment_);
        if (checked_out.is_err()) return Err(rstd::move(checked_out).unwrap_err());
        return Ok(rstd::move(checkout));
    }

    auto acquire_path(ref<rstd::path::Path>   requested,
                      bool                    package_source,
                      const WorkspaceCatalog* associated_primary = nullptr) -> Result<usize> {
        auto canonical = rstd::fs::canonicalize(requested);
        if (canonical.is_err()) {
            return source_failure<usize>(rstd::format("cannot resolve path source '{}': {}",
                                                      requested,
                                                      rstd::move(canonical).unwrap_err()));
        }
        auto requested_root = rstd::move(canonical).unwrap();
        auto catalog        = Option<WorkspaceCatalog> {};
        auto source_root    = requested_root.clone();
        if (package_source) {
            auto document = load_manifest_document(requested_root.as_path());
            if (document.is_err()) return Err(rstd::move(document).unwrap_err());
            auto loaded         = rstd::move(document).unwrap();
            auto loaded_catalog = WorkspaceCatalog {};
            if (loaded.kind == ManifestKind::Workspace && loaded.workspace.is_some()) {
                auto workspace = load_workspace_catalog(rstd::move(loaded.workspace).unwrap());
                if (workspace.is_err()) return Err(rstd::move(workspace).unwrap_err());
                loaded_catalog = rstd::move(workspace).unwrap();
            } else if (loaded.kind == ManifestKind::Package && loaded.package.is_some()) {
                auto package = rstd::move(loaded.package).unwrap();
                if (associated_primary != nullptr) {
                    auto associated = WorkspaceCatalog::associated_package(rstd::move(package),
                                                                           *associated_primary);
                    if (associated.is_err()) return Err(rstd::move(associated).unwrap_err());
                    loaded_catalog = rstd::move(associated).unwrap();
                } else {
                    auto containing = try_containing_workspace(package);
                    if (containing.is_err()) return Err(rstd::move(containing).unwrap_err());
                    if (containing->is_some()) {
                        auto workspace = load_workspace_catalog(
                            rstd::move(containing).unwrap().unwrap(), Some(rstd::move(package)));
                        if (workspace.is_err()) return Err(rstd::move(workspace).unwrap_err());
                        loaded_catalog = rstd::move(workspace).unwrap();
                    } else {
                        loaded_catalog = rstd_try(WorkspaceCatalog::single(rstd::move(package)));
                    }
                }
            } else {
                return source_failure<usize>("source manifest has no package or workspace"_str);
            }
            source_root = PathBuf::from(loaded_catalog.root());
            catalog     = Some(rstd::move(loaded_catalog));
        }

        auto root_text = source_root.as_path().to_str();
        if (root_text.is_none()) {
            return source_failure<usize>(rstd::format(
                "normalized source root '{}' is not valid UTF-8", source_root.as_path()));
        }
        auto existing = roots_.get(*root_text);
        if (existing.is_some()) {
            if (package_source && entries_[**existing].catalog.is_none()) {
                entries_[**existing].catalog = rstd::move(catalog);
            }
            return Ok(**existing);
        }
        if (package_source) {
            for (usize index {}; index < entries_.len(); ++index) {
                if (entries_[index].catalog.is_some() &&
                    entries_[index].catalog->contains_package_root(requested_root.as_path())) {
                    return Ok(index);
                }
            }
        }
        auto root_key = String::make(*root_text);
        auto relative = relative_path(graph_root_.as_path(), source_root.as_path());
        if (relative.is_err()) return Err(rstd::move(relative).unwrap_err());
        auto normalized = rstd::move(relative).unwrap();
        auto id         = path_source_identity(normalized.as_path());
        auto index      = entries_.len();
        entries_.push(SourceEntry {
            .source =
                ResolvedPackageSource {
                    .identity       = rstd::move(id),
                    .kind           = PackageSourceKind::Path,
                    .root_directory = rstd::move(source_root),
                    .path           = rstd::move(normalized),
                },
            .catalog = rstd::move(catalog),
        });
        roots_.insert(rstd::move(root_key), index);
        source_identities_.insert(entries_[index].source.identity.clone(), index);
        return Ok(index);
    }

    auto acquire_associated_catalog(usize primary_source, ref<str> directory, ProjectRootRole role)
        -> Result<Option<usize>> {
        auto root    = PathBuf::from(entries_[primary_source].catalog->root())
                           .join(PathBuf::from(directory).as_path());
        auto located = try_locate_manifest(root.as_path());
        if (located.is_err()) return Err(rstd::move(located).unwrap_err());
        if (located->is_none()) return Ok(None());
        const auto& location = **located;
        if (! (location.directory.as_path() == root.as_path())) {
            return source_failure<Option<usize>>(rstd::format(
                "associated manifest directory '{}' must be the exact project directory '{}'",
                location.directory.as_path(),
                root.as_path()));
        }
        if (entries_[primary_source].catalog->contains_package_root(location.directory.as_path())) {
            return Ok(None());
        }

        auto source = acquire_path(
            location.directory.as_path(), true, rstd::addressof(*entries_[primary_source].catalog));
        if (source.is_err()) return Err(rstd::move(source).unwrap_err());
        auto source_index = *source;
        auto validated    = validate_associated_catalog(
            *entries_[primary_source].catalog, *entries_[source_index].catalog, role);
        if (validated.is_err()) return Err(rstd::move(validated).unwrap_err());
        return Ok(Some(source_index));
    }

    auto acquire_git(ref<str> url, const GitReference& reference, bool package_source)
        -> Result<usize> {
        auto locked = locked_source(url, reference);
        if (locked.is_err()) return Err(rstd::move(locked).unwrap_err());
        auto pin = rstd::move(locked).unwrap();
        if (options_.git == GitResolutionMode::Refresh &&
            reference.kind != GitReferenceKind::Commit) {
            pin = Option<ref<LockedGitSource>> {};
        }
        if (options_.locked && pin.is_none()) {
            return source_failure<usize>(
                rstd::format("--locked has no source matching Git dependency '{}'", url));
        }

        auto cache = lito_cache_directory(PathBuf::from("git"_str).as_path(), "Git sources"_str);
        if (cache.is_err()) return Err(rstd::move(cache).unwrap_err());
        auto cache_directory = rstd::move(cache).unwrap();
        auto created         = rstd::fs::create_dir_all(cache_directory.as_path());
        if (created.is_err()) {
            return source_failure<usize>(rstd::format("cannot create Git source cache '{}': {}",
                                                      cache_directory.as_path(),
                                                      rstd::move(created).unwrap_err()));
        }
        auto bucket = cache_directory.join(PathBuf::from(source_hash(url)).as_path());
        created     = rstd::fs::create_dir_all(bucket.as_path());
        if (created.is_err()) {
            return source_failure<usize>(rstd::format("cannot create Git source bucket '{}': {}",
                                                      bucket.as_path(),
                                                      rstd::move(created).unwrap_err()));
        }
        auto lock_path = bucket.join(PathBuf::from("lock"_str).as_path());
        auto lock_file = rstd::fs::File::create(lock_path.as_path());
        if (lock_file.is_err()) {
            return source_failure<usize>(rstd::format("cannot open Git source lock '{}': {}",
                                                      lock_path.as_path(),
                                                      rstd::move(lock_file).unwrap_err()));
        }
        auto locked_cache = lock_file->lock();
        if (locked_cache.is_err()) {
            return source_failure<usize>(rstd::format("cannot lock Git source cache '{}': {}",
                                                      lock_path.as_path(),
                                                      rstd::move(locked_cache).unwrap_err()));
        }

        auto repo = repository(bucket.as_path(), url);
        if (repo.is_err()) return Err(rstd::move(repo).unwrap_err());
        auto repository_path = rstd::move(repo).unwrap();
        auto commit          = resolve_commit(repository_path.as_path(), reference, pin);
        if (commit.is_err()) return Err(rstd::move(commit).unwrap_err());
        auto precise_commit = rstd::move(commit).unwrap();
        auto id             = git_source_identity(url, precise_commit.as_str());
        auto existing       = source_identities_.get(id.as_str());
        if (existing.is_some()) {
            if (package_source && entries_[**existing].catalog.is_none()) {
                auto catalog =
                    load_git_catalog(entries_[**existing].source.root_directory.as_path());
                if (catalog.is_err()) return Err(rstd::move(catalog).unwrap_err());
                entries_[**existing].catalog = Some(rstd::move(catalog).unwrap());
            }
            return Ok(**existing);
        }

        auto local = checkout(bucket.as_path(), repository_path.as_path(), precise_commit.as_str());
        if (local.is_err()) return Err(rstd::move(local).unwrap_err());
        auto canonical = rstd::fs::canonicalize(local->as_path());
        if (canonical.is_err()) {
            return source_failure<usize>(rstd::format("cannot resolve Git checkout '{}': {}",
                                                      local->as_path(),
                                                      rstd::move(canonical).unwrap_err()));
        }
        auto checkout_root = rstd::move(canonical).unwrap();
        auto catalog       = Option<WorkspaceCatalog> {};
        if (package_source) {
            auto loaded = load_git_catalog(checkout_root.as_path());
            if (loaded.is_err()) return Err(rstd::move(loaded).unwrap_err());
            auto loaded_catalog = rstd::move(loaded).unwrap();
            if (! (loaded_catalog.root().starts_with(checkout_root.as_path()) &&
                   checkout_root.as_path().starts_with(loaded_catalog.root()))) {
                return source_failure<usize>(
                    rstd::format("Git source manifest root '{}' does not match checkout root '{}'",
                                 loaded_catalog.root(),
                                 checkout_root.as_path()));
            }
            catalog = Some(rstd::move(loaded_catalog));
        }

        auto root_text = checkout_root.as_path().to_str();
        if (root_text.is_none()) {
            return source_failure<usize>(
                rstd::format("Git checkout '{}' is not valid UTF-8", checkout_root.as_path()));
        }
        auto root_key         = String::make(*root_text);
        auto copied_reference = GitReference {
            .kind  = reference.kind,
            .value = reference.value.clone(),
        };
        auto index = entries_.len();
        entries_.push(SourceEntry {
            .source =
                ResolvedPackageSource {
                    .identity       = id.clone(),
                    .kind           = PackageSourceKind::Git,
                    .root_directory = rstd::move(checkout_root),
                    .git            = String::make(url),
                    .reference      = rstd::move(copied_reference),
                    .commit         = rstd::move(precise_commit),
                },
            .catalog = rstd::move(catalog),
        });
        roots_.insert(rstd::move(root_key), index);
        source_identities_.insert(rstd::move(id), index);
        return Ok(index);
    }

public:
    explicit SourceManager(ref<rstd::path::Path>             graph_root,
                           PackageResolutionOptions          options,
                           ToolResolver&                     resolver,
                           const ResolvedProcessEnvironment& environment)
        : graph_root_(PathBuf::from(graph_root)),
          options_(rstd::move(options)),
          resolver_(rstd::addressof(resolver)),
          environment_(rstd::addressof(environment)) {}

    auto acquire_root(ref<rstd::path::Path> root) -> Result<AcquiredProjectSources> {
        auto primary = acquire_path(root, true);
        if (primary.is_err()) return Err(rstd::move(primary).unwrap_err());
        const auto primary_source = *primary;
        auto       tests          = rstd_try(acquire_associated_catalog(
            primary_source, "tests"_str, ProjectRootRole::AssociatedTest));
        return Ok(AcquiredProjectSources {
            .primary = primary_source,
            .tests   = rstd::move(tests),
        });
    }

    auto acquire(const PackageSourceRequirement& requirement, ref<rstd::path::Path> declaring_root)
        -> Result<usize> {
        if (requirement.is_Git()) {
            const auto& git   = requirement.as_Git();
            auto        patch = patched_path(git.url.as_str());
            if (patch.is_err()) return Err(rstd::move(patch).unwrap_err());
            if (patch->is_some()) return acquire_path(**patch, true);
            return acquire_git(git.url.as_str(), git.reference, true);
        }
        auto requested = PathBuf::from(declaring_root).join(requirement.as_Path().path.as_path());
        return acquire_path(requested.as_path(), true);
    }

    auto acquire_frontier(Vec<PackageSourceFetchRequest> requests, usize jobs)
        -> Result<Vec<usize>> {
        if (jobs == usize {}) {
            return source_failure<Vec<usize>>("source fetch jobs must be greater than zero"_str);
        }
        auto result = Vec<usize>::with_capacity(requests.len());
        if (requests.is_empty()) return Ok(rstd::move(result));

        auto unique       = Vec<PackageSourceFetchRequest>::make();
        auto request_keys = rstd::collections::BTreeMap<String, usize>::make();
        auto bindings     = Vec<usize>::with_capacity(requests.len());
        for (auto& request : requests) {
            auto key =
                rstd_try(source_request_key(request.source, request.declaring_root.as_path()));
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

        auto worker_count = jobs < unique.len() ? jobs : unique.len();
        auto created      = rstd::thread::BlockingTaskGroup<Result<FetchedPackageSource>>::make(
            worker_count, unique.len());
        if (created.is_err()) {
            return source_failure<Vec<usize>>(
                rstd::format("cannot create package source fetch executor: {}",
                             rstd::move(created).unwrap_err_unchecked()));
        }
        auto group = rstd::move(created).unwrap_unchecked();
        for (usize index {}; index < unique.len(); ++index) {
            auto request     = rstd::move(unique[index]);
            auto graph_root  = graph_root_.clone();
            auto options     = options_.clone();
            auto environment = environment_->clone();
            auto submitted   = group.submit(
                [index,
                 request     = rstd::move(request),
                 graph_root  = rstd::move(graph_root),
                 options     = rstd::move(options),
                 environment = rstd::move(environment)]() mutable -> Result<FetchedPackageSource> {
                    auto resolver = ToolResolver(environment);
                    auto manager  = SourceManager(
                        graph_root.as_path(), rstd::move(options), resolver, environment);
                    auto acquired =
                        manager.acquire(request.source, request.declaring_root.as_path());
                    if (acquired.is_err()) return Err(rstd::move(acquired).unwrap_err());
                    return Ok(FetchedPackageSource {
                        .request = index,
                        .entry   = manager.take_entry(*acquired),
                    });
                });
            if (submitted.is_err()) {
                return source_failure<Vec<usize>>("cannot submit package source fetch task"_str);
            }
        }
        auto outcomes = rstd::move(group).join();
        auto fetched  = Vec<Option<SourceEntry>>::with_capacity(unique.len());
        for (usize index {}; index < unique.len(); ++index) fetched.push(None());
        for (auto& outcome : outcomes) {
            auto value = rstd::move(outcome).into_value();
            if (value.is_none()) {
                return source_failure<Vec<usize>>("package source fetch task was cancelled"_str);
            }
            auto task = rstd::move(value).unwrap_unchecked();
            if (task.is_err()) return Err(rstd::move(task).unwrap_err());
            auto source             = rstd::move(task).unwrap();
            fetched[source.request] = Some(rstd::move(source.entry));
        }
        auto unique_indices = Vec<usize>::with_capacity(unique.len());
        for (auto& source : fetched) {
            if (source.is_none()) {
                return source_failure<Vec<usize>>("package source fetch result is missing"_str);
            }
            unique_indices.push(absorb_entry(rstd::move(source).unwrap()));
        }
        for (auto binding : bindings) result.push(usize(unique_indices[binding]));
        return Ok(rstd::move(result));
    }

    auto acquire_external_frontier(Vec<PackageSourceFetchRequest> requests, usize jobs)
        -> Result<Vec<ExternalSourceFetchOutcome>> {
        if (jobs == usize {}) {
            return source_failure<Vec<ExternalSourceFetchOutcome>>(
                "source fetch jobs must be greater than zero"_str);
        }
        auto result = Vec<ExternalSourceFetchOutcome>::with_capacity(requests.len());
        if (requests.is_empty()) return Ok(rstd::move(result));

        auto unique       = Vec<PackageSourceFetchRequest>::make();
        auto request_keys = rstd::collections::BTreeMap<String, usize>::make();
        auto bindings     = Vec<usize>::with_capacity(requests.len());
        for (auto& request : requests) {
            auto key =
                rstd_try(source_request_key(request.source, request.declaring_root.as_path()));
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

        auto worker_count = jobs < unique.len() ? jobs : unique.len();
        auto created      = rstd::thread::BlockingTaskGroup<Result<FetchedExternalSource>>::make(
            worker_count, unique.len());
        if (created.is_err()) {
            return source_failure<Vec<ExternalSourceFetchOutcome>>(
                rstd::format("cannot create external source fetch executor: {}",
                             rstd::move(created).unwrap_err_unchecked()));
        }
        auto group = rstd::move(created).unwrap_unchecked();
        for (usize index {}; index < unique.len(); ++index) {
            auto request     = rstd::move(unique[index]);
            auto graph_root  = graph_root_.clone();
            auto options     = options_.clone();
            auto environment = environment_->clone();
            auto submitted   = group.submit(
                [index,
                 request     = rstd::move(request),
                 graph_root  = rstd::move(graph_root),
                 options     = rstd::move(options),
                 environment = rstd::move(environment)]() mutable -> Result<FetchedExternalSource> {
                    auto resolver = ToolResolver(environment);
                    auto manager  = SourceManager(
                        graph_root.as_path(), rstd::move(options), resolver, environment);
                    auto acquired =
                        manager.acquire_external(request.source, request.declaring_root.as_path());
                    if (acquired.is_err()) return Err(rstd::move(acquired).unwrap_err());
                    return Ok(FetchedExternalSource {
                        .request = index,
                        .outcome =
                            ExternalSourceFetchOutcome {
                                .acquired = rstd::move(acquired).unwrap(),
                                .sources  = manager.finish(),
                            },
                    });
                });
            if (submitted.is_err()) {
                return source_failure<Vec<ExternalSourceFetchOutcome>>(
                    "cannot submit external source fetch task"_str);
            }
        }
        auto outcomes = rstd::move(group).join();
        auto fetched  = Vec<Option<ExternalSourceFetchOutcome>>::with_capacity(unique.len());
        for (usize index {}; index < unique.len(); ++index) fetched.push(None());
        for (auto& outcome : outcomes) {
            auto value = rstd::move(outcome).into_value();
            if (value.is_none()) {
                return source_failure<Vec<ExternalSourceFetchOutcome>>(
                    "external source fetch task was cancelled"_str);
            }
            auto task = rstd::move(value).unwrap_unchecked();
            if (task.is_err()) return Err(rstd::move(task).unwrap_err());
            auto source             = rstd::move(task).unwrap();
            fetched[source.request] = Some(rstd::move(source.outcome));
        }
        for (auto binding : bindings) {
            if (fetched[binding].is_none()) {
                return source_failure<Vec<ExternalSourceFetchOutcome>>(
                    "external source fetch result is missing"_str);
            }
            result.push(clone_external_outcome(*fetched[binding]));
        }
        return Ok(rstd::move(result));
    }

    auto acquire_external(const PackageSourceRequirement& requirement,
                          ref<rstd::path::Path> declaring_root) -> Result<AcquiredSource> {
        auto acquired = [&]() -> Result<usize> {
            if (requirement.is_Git()) {
                const auto& git   = requirement.as_Git();
                auto        patch = patched_path(git.url.as_str());
                if (patch.is_err()) return Err(rstd::move(patch).unwrap_err());
                return patch->is_some() ? acquire_path(**patch, false)
                                        : acquire_git(git.url.as_str(), git.reference, false);
            }
            auto requested =
                PathBuf::from(declaring_root).join(requirement.as_Path().path.as_path());
            return acquire_path(requested.as_path(), false);
        }();
        if (acquired.is_err()) return Err(rstd::move(acquired).unwrap_err());
        const auto index = rstd::move(acquired).unwrap();
        return Ok(AcquiredSource {
            .root      = entries_[index].source.root_directory.clone(),
            .identity  = entries_[index].source.identity.clone(),
            .cacheable = entries_[index].source.kind == PackageSourceKind::Git,
        });
    }

    auto package_names(usize source) const -> Vec<String> {
        auto result = Vec<String>::with_capacity(entries_[source].catalog->names().len());
        for (const auto& name : entries_[source].catalog->names()) result.push(name.clone());
        return result;
    }

    auto source_name(usize source) const noexcept -> ref<str> {
        return entries_[source].catalog->name();
    }

    auto source_identity(usize source) const noexcept -> ref<str> {
        return entries_[source].source.identity.as_str();
    }

    auto source_manifest(usize source) const -> PathBuf {
        return PathBuf::from(entries_[source].catalog->manifest_path());
    }

    auto source_is_workspace(usize source) const noexcept -> bool {
        return entries_[source].catalog->is_workspace();
    }

    auto source_profile(usize source) const -> ProjectProfile {
        return entries_[source].catalog->profile();
    }

    auto source_root(usize source) const -> PathBuf {
        return entries_[source].source.root_directory.clone();
    }

    auto take_package(usize source, ref<str> name) -> Result<SelectedSourcePackage> {
        auto manifest = entries_[source].catalog->take_package(name);
        if (manifest.is_none()) {
            if (entries_[source].catalog->names().len() == usize(1)) {
                return source_failure<SelectedSourcePackage>(
                    rstd::format("dependency '{}' resolves to package '{}' from source '{}'",
                                 name,
                                 entries_[source].catalog->names()[usize {}].as_str(),
                                 entries_[source].source.identity.as_str()));
            }
            return source_failure<SelectedSourcePackage>(
                rstd::format("source '{}' has no package named '{}'",
                             entries_[source].source.identity.as_str(),
                             name));
        }
        auto package  = rstd::move(manifest).unwrap();
        auto relative = package.manifest_path.as_path().strip_prefix(
            entries_[source].source.root_directory.as_path());
        if (relative.is_none()) {
            return source_failure<SelectedSourcePackage>(
                rstd::format("package manifest '{}' is outside source '{}'",
                             package.manifest_path.as_path(),
                             entries_[source].source.identity.as_str()));
        }
        return Ok(SelectedSourcePackage {
            .source_identity = entries_[source].source.identity.clone(),
            .manifest        = PathBuf::from(*relative),
            .package         = rstd::move(package),
        });
    }

    auto finish() -> Vec<ResolvedPackageSource> {
        auto sources = Vec<ResolvedPackageSource>::with_capacity(entries_.len());
        for (auto& entry : entries_) sources.push(rstd::move(entry.source));
        rstd::slice_::sort_unstable_by(
            sources.as_mut_slice().as_mut_ref(),
            [](const ResolvedPackageSource& left, const ResolvedPackageSource& right) {
                return left.identity < right.identity;
            });
        return sources;
    }
};

} // namespace lito
