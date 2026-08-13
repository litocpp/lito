module;
#include <rstd/macro.hpp>

export module lito.source:manager;

import rstd;
import lito.error;
import lito.lock.contract;
import lito.package.graph_contract;
import lito.workspace.contract;
import lito.build.profile_contract;
import lito.build.contract;
import lito.source.contract;
import lito.source.error_contract;
import lito.system.storage;
import lito.manifest;
import lito.system.process;
import lito.system.environment;
import lito.workspace;
import :support;
import :acquisition;
import :seed;

using namespace rstd::prelude;
using namespace rstd::literals;
using IndexMap = rstd::collections::BTreeMap<String, usize>;

export namespace lito
{

class SourceManager {
    PathBuf                           graph_root_;
    PackageResolutionOptions          options_;
    Vec<SourceEntry>                  entries_;
    IndexMap                          roots_ { IndexMap::make() };
    IndexMap                          source_identities_ { IndexMap::make() };
    IndexMap                          source_requests_ { IndexMap::make() };
    ToolResolver*                     resolver_ {};
    const ResolvedProcessEnvironment* environment_ {};
    BuildObserver                     observer_;
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
                            ref<rstd::path::Path> declaring_root) -> SourceResult<String> {
        if (source.is_Path()) {
            return Ok(
                rstd::format("path\n{}\n{}", declaring_root, source.as_Path().path.as_path()));
        }
        const auto& git = source.as_Git();
        return Ok(git_requirement_identity(git.url.as_str(), git.reference));
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

    auto git_command() -> SourceResult<Vec<String>> {
        if (git_.is_none()) {
            auto resolved =
                resolver_->resolve(PathBuf::from("git"_str).as_path(), "Git executable"_str);
            if (resolved.is_err()) {
                return Err(SourceError::System(String::make("resolve Git executable"_str),
                                               rstd::move(resolved).unwrap_err()));
            }
            git_ = Some(rstd::move(resolved).unwrap().executable);
        }
        auto arguments = Vec<String>::make();
        auto pushed    = push_path(arguments, git_->as_path());
        if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
        return Ok(rstd::move(arguments));
    }

    auto emit_fetch(ref<str> source, ref<rstd::path::Path> destination) const noexcept -> void {
        if (observer_.notify == nullptr) return;
        observer_.notify(observer_.context,
                         BuildEvent { BuildEventKind::Fetch, source, destination });
    }

    auto seed_checkout(ref<str> url, ref<str> commit) -> SourceResult<Option<PathBuf>> {
        auto identity = git_fetch_identity(url, commit);
        auto located  = rstd_try(locate_fetch_seed(options_.sources.fetch_seeds, identity));
        if (located.is_none()) return Ok(None());
        auto canonical = rstd::fs::canonicalize(located->as_path());
        if (canonical.is_err()) {
            return source_io_failure<Option<PathBuf>>("resolve Git fetch seed"_str,
                                                      located->as_path(),
                                                      rstd::move(canonical).unwrap_err());
        }
        auto arguments = rstd_try(git_command());
        arguments.push(String::make("-C"_str));
        rstd_try(push_path(arguments, canonical->as_path()));
        arguments.push(String::make("rev-parse"_str));
        arguments.push(String::make("--verify"_str));
        arguments.push(String::make("HEAD"_str));
        auto current = rstd_try(
            git_output(rstd::move(arguments), "Git fetch seed inspection"_str, *environment_));
        if (current.as_str() != commit) {
            return source_failure<Option<PathBuf>>(
                rstd::format("Git fetch seed HEAD '{}' does not match locked commit '{}'",
                             current.as_str(),
                             commit));
        }
        return Ok(Some(rstd::move(canonical).unwrap()));
    }

    auto patched_path(ref<str> url) -> SourceResult<Option<ref<rstd::path::Path>>> {
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
        -> SourceResult<Option<ref<LockedGitSource>>> {
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

    auto exact_git_commit(const GitReference& reference, Option<ref<LockedGitSource>> locked)
        -> Option<ref<str>> {
        if (locked.is_some()) return Some((*locked)->commit.as_str());
        if (reference.kind == GitReferenceKind::Commit) return Some(reference.value.as_str());
        return None();
    }

    auto repository(ref<rstd::path::Path> bucket, ref<str> url) -> SourceResult<PathBuf> {
        auto repository = PathBuf::from(bucket).join(PathBuf::from("repository.git"_str).as_path());
        auto exists     = rstd::fs::exists(repository.as_path());
        if (exists.is_err()) {
            return source_io_failure<PathBuf>("inspect Git cache repository"_str,
                                              repository.as_path(),
                                              rstd::move(exists).unwrap_err());
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
            return Err(SourceError::System(String::make("Git cache remote inspection"_str),
                                           rstd::move(inspected).unwrap_err()));
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

    auto object_exists(ref<rstd::path::Path> repository, ref<str> commit) -> SourceResult<bool> {
        auto arguments = rstd_try(git_command());
        arguments.push(String::make("--git-dir"_str));
        auto path = push_path(arguments, repository);
        if (path.is_err()) return Err(rstd::move(path).unwrap_err());
        arguments.push(String::make("cat-file"_str));
        arguments.push(String::make("-e"_str));
        arguments.push(rstd::format("{}^{{commit}}", commit));
        auto output = run_command(arguments, *environment_);
        if (output.is_err()) {
            return Err(SourceError::System(String::make("Git object inspection"_str),
                                           rstd::move(output).unwrap_err()));
        }
        return Ok(output->exit_code == i32 {});
    }

    auto fetch(ref<rstd::path::Path> repository, ref<str> url, ref<str> revision)
        -> SourceResult<empty> {
        auto arguments = rstd_try(git_command());
        arguments.push(String::make("--git-dir"_str));
        auto path = push_path(arguments, repository);
        if (path.is_err()) return Err(rstd::move(path).unwrap_err());
        arguments.push(String::make("fetch"_str));
        arguments.push(String::make("--force"_str));
        arguments.push(String::make("--no-tags"_str));
        arguments.push(String::make("origin"_str));
        arguments.push(String::make(revision));
        auto source = rstd::format("{}#{}", url, revision);
        emit_fetch(source.as_str(), repository);
        return git_status(rstd::move(arguments), "Git source fetch"_str, *environment_);
    }

    auto rev_parse(ref<rstd::path::Path> repository, ref<str> revision) -> SourceResult<String> {
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
                        ref<str>                     url,
                        const GitReference&          reference,
                        Option<ref<LockedGitSource>> locked) -> SourceResult<String> {
        if (locked.is_some()) {
            auto present = object_exists(repository, (*locked)->commit.as_str());
            if (present.is_err()) return Err(rstd::move(present).unwrap_err());
            if (! *present) {
                auto fetched = fetch(repository, url, (*locked)->commit.as_str());
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
        auto fetched = fetch(repository, url, revision.as_str());
        if (fetched.is_err()) return Err(rstd::move(fetched).unwrap_err());
        return rev_parse(repository, "FETCH_HEAD"_str);
    }

    auto checkout(ref<rstd::path::Path> bucket, ref<rstd::path::Path> repository, ref<str> commit)
        -> SourceResult<PathBuf> {
        auto checkouts = PathBuf::from(bucket).join(PathBuf::from("checkouts"_str).as_path());
        auto created   = rstd::fs::create_dir_all(checkouts.as_path());
        if (created.is_err()) {
            return source_io_failure<PathBuf>("create Git checkout cache"_str,
                                              checkouts.as_path(),
                                              rstd::move(created).unwrap_err());
        }
        auto checkout = checkouts.join(PathBuf::from(commit).as_path());
        auto exists   = rstd::fs::exists(checkout.as_path());
        if (exists.is_err()) {
            return source_io_failure<PathBuf>(
                "inspect Git checkout"_str, checkout.as_path(), rstd::move(exists).unwrap_err());
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
                return source_io_failure<PathBuf>("recover Git checkout"_str,
                                                  checkout.as_path(),
                                                  rstd::move(removed).unwrap_err());
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

    auto acquire_path(ref<rstd::path::Path>    requested,
                      bool                     package_source,
                      const WorkspaceCatalog*  associated_primary = nullptr,
                      Option<WorkspaceCatalog> preloaded          = None()) -> SourceResult<usize> {
        auto canonical = rstd::fs::canonicalize(requested);
        if (canonical.is_err()) {
            return source_io_failure<usize>(
                "resolve path source"_str, requested, rstd::move(canonical).unwrap_err());
        }
        auto requested_root = rstd::move(canonical).unwrap();
        auto catalog        = Option<WorkspaceCatalog> {};
        auto source_root    = requested_root.clone();
        if (package_source) {
            if (preloaded.is_some()) {
                source_root = PathBuf::from(preloaded->root());
                catalog     = Some(rstd::move(preloaded).unwrap());
            } else {
                auto document = load_manifest_document(requested_root.as_path());
                if (document.is_err()) {
                    return Err(rstd::into<SourceError>(rstd::move(document).unwrap_err()));
                }
                auto loaded         = rstd::move(document).unwrap();
                auto loaded_catalog = WorkspaceCatalog {};
                if (loaded.kind == ManifestKind::Workspace && loaded.workspace.is_some()) {
                    auto workspace = load_workspace_catalog(rstd::move(loaded.workspace).unwrap());
                    if (workspace.is_err()) {
                        return Err(rstd::into<SourceError>(rstd::move(workspace).unwrap_err()));
                    }
                    loaded_catalog = rstd::move(workspace).unwrap();
                } else if (loaded.kind == ManifestKind::Package && loaded.package.is_some()) {
                    auto package = rstd::move(loaded.package).unwrap();
                    if (associated_primary != nullptr) {
                        auto associated = WorkspaceCatalog::associated_package(rstd::move(package),
                                                                               *associated_primary);
                        if (associated.is_err()) {
                            return Err(
                                rstd::into<SourceError>(rstd::move(associated).unwrap_err()));
                        }
                        loaded_catalog = rstd::move(associated).unwrap();
                    } else {
                        auto containing = try_containing_workspace(package);
                        if (containing.is_err()) {
                            return Err(
                                rstd::into<SourceError>(rstd::move(containing).unwrap_err()));
                        }
                        if (containing->is_some()) {
                            auto workspace =
                                load_workspace_catalog(rstd::move(containing).unwrap().unwrap(),
                                                       Some(rstd::move(package)));
                            if (workspace.is_err()) {
                                return Err(
                                    rstd::into<SourceError>(rstd::move(workspace).unwrap_err()));
                            }
                            loaded_catalog = rstd::move(workspace).unwrap();
                        } else {
                            loaded_catalog =
                                rstd_try(WorkspaceCatalog::single(rstd::move(package)));
                        }
                    }
                } else {
                    return source_failure<usize>("source manifest has no package or workspace"_str);
                }
                source_root = PathBuf::from(loaded_catalog.root());
                catalog     = Some(rstd::move(loaded_catalog));
            }
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
        -> SourceResult<Option<usize>> {
        auto root    = PathBuf::from(entries_[primary_source].catalog->root())
                           .join(PathBuf::from(directory).as_path());
        auto located = try_locate_manifest(root.as_path());
        if (located.is_err()) {
            return Err(
                SourceError::Manifest(ManifestError::Locate(rstd::move(located).unwrap_err())));
        }
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
        if (validated.is_err()) {
            return Err(rstd::into<SourceError>(rstd::move(validated).unwrap_err()));
        }
        return Ok(Some(source_index));
    }

    auto register_git_source(ref<str>            url,
                             const GitReference& reference,
                             String              precise_commit,
                             PathBuf             checkout_root,
                             bool                package_source) -> SourceResult<usize> {
        auto patch = rstd_try(patched_path(url));
        if (patch.is_some()) {
            auto physical = rstd::fs::canonicalize(*patch);
            if (physical.is_err()) {
                return source_io_failure<usize>(
                    "resolve patched Git source"_str, *patch, rstd::move(physical).unwrap_err());
            }
            checkout_root = rstd::move(physical).unwrap();
        }

        auto catalog = Option<WorkspaceCatalog> {};
        if (package_source) {
            auto loaded_catalog = rstd_try(load_git_catalog(checkout_root.as_path()));
            if (! (loaded_catalog.root().starts_with(checkout_root.as_path()) &&
                   checkout_root.as_path().starts_with(loaded_catalog.root()))) {
                return source_failure<usize>(
                    rstd::format("Git source manifest root '{}' does not match checkout root '{}'",
                                 loaded_catalog.root(),
                                 checkout_root.as_path()));
            }
            catalog = Some(rstd::move(loaded_catalog));
        }

        auto id       = git_source_identity(url, precise_commit.as_str());
        auto existing = source_identities_.get(id.as_str());
        if (existing.is_some()) {
            if (package_source && entries_[**existing].catalog.is_none()) {
                entries_[**existing].catalog = rstd::move(catalog);
            }
            return Ok(**existing);
        }

        auto root_text = checkout_root.as_path().to_str();
        if (root_text.is_none()) {
            return source_failure<usize>(
                rstd::format("Git checkout '{}' is not valid UTF-8", checkout_root.as_path()));
        }
        auto root_key = String::make(*root_text);
        auto index    = entries_.len();
        entries_.push(SourceEntry {
            .source =
                ResolvedPackageSource {
                    .identity       = id.clone(),
                    .kind           = PackageSourceKind::Git,
                    .root_directory = rstd::move(checkout_root),
                    .git            = String::make(url),
                    .reference =
                        GitReference {
                            .kind  = reference.kind,
                            .value = reference.value.clone(),
                        },
                    .commit = rstd::move(precise_commit),
                },
            .catalog = rstd::move(catalog),
        });
        roots_.insert(rstd::move(root_key), index);
        source_identities_.insert(rstd::move(id), index);
        return Ok(index);
    }

    auto acquire_git(ref<str> url, const GitReference& reference, bool package_source)
        -> SourceResult<usize> {
        auto request_key = git_requirement_identity(url, reference);
        auto requested   = source_requests_.get(request_key.as_str());
        if (requested.is_some()) {
            if (! package_source || entries_[**requested].catalog.is_some()) return Ok(**requested);
        }
        auto locked = locked_source(url, reference);
        if (locked.is_err()) return Err(rstd::move(locked).unwrap_err());
        auto pin = rstd::move(locked).unwrap();
        if (options_.git == GitResolutionMode::Refresh &&
            options_.sources.network == NetworkPolicy::Allow &&
            reference.kind != GitReferenceKind::Commit) {
            pin = Option<ref<LockedGitSource>> {};
        }
        if (options_.locked && pin.is_none()) {
            return source_failure<usize>(
                rstd::format("--locked has no source matching Git dependency '{}'", url));
        }

        auto cache = lito_cache_directory(PathBuf::from("git"_str).as_path(), "Git sources"_str);
        if (cache.is_err()) {
            return Err(rstd::into<SourceError>(rstd::move(cache).unwrap_err()));
        }
        auto cache_directory = rstd::move(cache).unwrap();
        auto created         = rstd::fs::create_dir_all(cache_directory.as_path());
        if (created.is_err()) {
            return source_io_failure<usize>("create Git source cache"_str,
                                            cache_directory.as_path(),
                                            rstd::move(created).unwrap_err());
        }
        auto bucket = cache_directory.join(PathBuf::from(source_hash(url)).as_path());
        created     = rstd::fs::create_dir_all(bucket.as_path());
        if (created.is_err()) {
            return source_io_failure<usize>(
                "create Git source bucket"_str, bucket.as_path(), rstd::move(created).unwrap_err());
        }
        auto lock_path = bucket.join(PathBuf::from("lock"_str).as_path());
        auto lock_file = rstd::fs::File::create(lock_path.as_path());
        if (lock_file.is_err()) {
            return source_io_failure<usize>("open Git source lock"_str,
                                            lock_path.as_path(),
                                            rstd::move(lock_file).unwrap_err());
        }
        auto locked_cache = lock_file->lock();
        if (locked_cache.is_err()) {
            return source_io_failure<usize>("lock Git source cache"_str,
                                            lock_path.as_path(),
                                            rstd::move(locked_cache).unwrap_err());
        }

        auto repository_path = rstd_try(repository(bucket.as_path(), url));
        auto exact_commit    = exact_git_commit(reference, pin);
        if (exact_commit.is_some()) {
            auto present = rstd_try(object_exists(repository_path.as_path(), *exact_commit));
            if (present) {
                auto precise_commit = rstd_try(rev_parse(repository_path.as_path(), *exact_commit));
                auto local          = rstd_try(
                    checkout(bucket.as_path(), repository_path.as_path(), precise_commit.as_str()));
                auto canonical = rstd::fs::canonicalize(local.as_path());
                if (canonical.is_err()) {
                    return source_io_failure<usize>("resolve Git checkout"_str,
                                                    local.as_path(),
                                                    rstd::move(canonical).unwrap_err());
                }
                auto registered = rstd_try(register_git_source(url,
                                                               reference,
                                                               rstd::move(precise_commit),
                                                               rstd::move(canonical).unwrap(),
                                                               package_source));
                source_requests_.insert(rstd::move(request_key), registered);
                return Ok(registered);
            }
            auto seed = rstd_try(seed_checkout(url, *exact_commit));
            if (seed.is_some()) {
                auto registered = rstd_try(register_git_source(url,
                                                               reference,
                                                               String::make(*exact_commit),
                                                               rstd::move(seed).unwrap(),
                                                               package_source));
                source_requests_.insert(rstd::move(request_key), registered);
                return Ok(registered);
            }
        }
        if (options_.sources.network == NetworkPolicy::Offline) {
            auto requirement = git_requirement_identity(url, reference);
            return source_failure<usize>(rstd::format(
                "offline source resolution cannot fetch Git source '{}'", requirement.as_str()));
        }
        auto precise_commit =
            rstd_try(resolve_commit(repository_path.as_path(), url, reference, pin));
        auto local = rstd_try(
            checkout(bucket.as_path(), repository_path.as_path(), precise_commit.as_str()));
        auto canonical = rstd::fs::canonicalize(local.as_path());
        if (canonical.is_err()) {
            return source_io_failure<usize>(
                "resolve Git checkout"_str, local.as_path(), rstd::move(canonical).unwrap_err());
        }
        auto registered = rstd_try(register_git_source(url,
                                                       reference,
                                                       rstd::move(precise_commit),
                                                       rstd::move(canonical).unwrap(),
                                                       package_source));
        source_requests_.insert(rstd::move(request_key), registered);
        return Ok(registered);
    }

    auto resolve_git(ref<str> url, const GitReference& reference)
        -> SourceResult<ResolvedPackageSource> {
        auto locked = rstd_try(locked_source(url, reference));
        auto pin    = locked;
        if (options_.git == GitResolutionMode::Refresh &&
            options_.sources.network == NetworkPolicy::Allow &&
            reference.kind != GitReferenceKind::Commit) {
            pin = None();
        }
        if (options_.locked && pin.is_none()) {
            return source_failure<ResolvedPackageSource>(
                rstd::format("--locked has no source matching Git dependency '{}'", url));
        }

        auto cache = lito_cache_directory(PathBuf::from("git"_str).as_path(), "Git sources"_str);
        if (cache.is_err()) {
            return Err(rstd::into<SourceError>(rstd::move(cache).unwrap_err()));
        }
        auto cache_directory = rstd::move(cache).unwrap();
        auto created         = rstd::fs::create_dir_all(cache_directory.as_path());
        if (created.is_err()) {
            return source_io_failure<ResolvedPackageSource>("create Git source cache"_str,
                                                            cache_directory.as_path(),
                                                            rstd::move(created).unwrap_err());
        }
        auto bucket = cache_directory.join(PathBuf::from(source_hash(url)).as_path());
        created     = rstd::fs::create_dir_all(bucket.as_path());
        if (created.is_err()) {
            return source_io_failure<ResolvedPackageSource>(
                "create Git source bucket"_str, bucket.as_path(), rstd::move(created).unwrap_err());
        }
        auto lock_path = bucket.join(PathBuf::from("lock"_str).as_path());
        auto lock_file = rstd::fs::File::create(lock_path.as_path());
        if (lock_file.is_err()) {
            return source_io_failure<ResolvedPackageSource>("open Git source lock"_str,
                                                            lock_path.as_path(),
                                                            rstd::move(lock_file).unwrap_err());
        }
        auto locked_cache = lock_file->lock();
        if (locked_cache.is_err()) {
            return source_io_failure<ResolvedPackageSource>("lock Git source cache"_str,
                                                            lock_path.as_path(),
                                                            rstd::move(locked_cache).unwrap_err());
        }

        auto repository_path = rstd_try(repository(bucket.as_path(), url));
        auto precise_commit  = String::make();
        auto exact_commit    = exact_git_commit(reference, pin);
        if (exact_commit.is_some()) {
            auto present = rstd_try(object_exists(repository_path.as_path(), *exact_commit));
            if (present) {
                precise_commit = rstd_try(rev_parse(repository_path.as_path(), *exact_commit));
            } else {
                auto seed = rstd_try(seed_checkout(url, *exact_commit));
                if (seed.is_some()) precise_commit = String::make(*exact_commit);
            }
        }
        if (precise_commit.is_empty()) {
            if (options_.sources.network == NetworkPolicy::Offline) {
                auto requirement = git_requirement_identity(url, reference);
                return source_failure<ResolvedPackageSource>(
                    rstd::format("offline source resolution cannot fetch Git source '{}'",
                                 requirement.as_str()));
            }
            precise_commit = rstd_try(
                resolve_commit(repository_path.as_path(), url, reference, rstd::move(pin)));
        }
        return Ok(ResolvedPackageSource {
            .identity = git_source_identity(url, precise_commit.as_str()),
            .kind     = PackageSourceKind::Git,
            .git      = String::make(url),
            .reference =
                GitReference {
                    .kind  = reference.kind,
                    .value = reference.value.clone(),
                },
            .commit = rstd::move(precise_commit),
        });
    }

public:
    explicit SourceManager(ref<rstd::path::Path>             graph_root,
                           PackageResolutionOptions          options,
                           ToolResolver&                     resolver,
                           const ResolvedProcessEnvironment& environment,
                           BuildObserver                     observer = {})
        : graph_root_(PathBuf::from(graph_root)),
          options_(rstd::move(options)),
          resolver_(rstd::addressof(resolver)),
          environment_(rstd::addressof(environment)),
          observer_(observer) {}

    auto resolve_external_source(const PackageSourceRequirement& requirement,
                                 ref<rstd::path::Path>           declaring_root)
        -> SourceResult<ResolvedPackageSource> {
        if (requirement.is_Git()) {
            return resolve_git(requirement.as_Git().url.as_str(), requirement.as_Git().reference);
        }
        auto requested = PathBuf::from(declaring_root).join(requirement.as_Path().path.as_path());
        auto acquired  = acquire_path(requested.as_path(), false);
        if (acquired.is_err()) return Err(rstd::move(acquired).unwrap_err());
        return Ok(clone_source(entries_[*acquired].source));
    }

    auto acquire_root(ref<rstd::path::Path> root, Option<WorkspaceCatalog> catalog = None())
        -> SourceResult<AcquiredProjectSources> {
        auto primary = acquire_path(root, true, nullptr, rstd::move(catalog));
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
        -> SourceResult<usize> {
        if (requirement.is_Git()) {
            const auto& git = requirement.as_Git();
            return acquire_git(git.url.as_str(), git.reference, true);
        }
        auto requested = PathBuf::from(declaring_root).join(requirement.as_Path().path.as_path());
        return acquire_path(requested.as_path(), true);
    }

    auto acquire_frontier(Vec<PackageSourceFetchRequest> requests, usize jobs)
        -> SourceResult<Vec<usize>> {
        if (jobs == usize {}) {
            return source_failure<Vec<usize>>("source fetch jobs must be greater than zero"_str);
        }
        auto result = Vec<usize>::with_capacity(requests.len());
        if (requests.is_empty()) return Ok(rstd::move(result));

        auto unique       = Vec<PackageSourceFetchRequest>::make();
        auto unique_keys  = Vec<String>::make();
        auto request_keys = rstd::collections::BTreeMap<String, usize>::make();
        struct RequestBinding {
            bool  existing { false };
            usize index {};
        };
        auto bindings = Vec<RequestBinding>::with_capacity(requests.len());
        for (auto& request : requests) {
            auto key =
                rstd_try(source_request_key(request.source, request.declaring_root.as_path()));
            auto resolved = source_requests_.get(key.as_str());
            if (resolved.is_some()) {
                bindings.push(RequestBinding { .existing = true, .index = **resolved });
                continue;
            }
            auto existing = request_keys.get(key.as_str());
            if (existing.is_some()) {
                bindings.push(RequestBinding { .index = **existing });
                continue;
            }
            auto index = unique.len();
            request_keys.insert(rstd::move(key), index);
            unique_keys.push(
                rstd_try(source_request_key(request.source, request.declaring_root.as_path())));
            bindings.push(RequestBinding { .index = index });
            unique.push(rstd::move(request));
        }
        if (unique.is_empty()) {
            for (const auto binding : bindings) result.push(usize(binding.index));
            return Ok(rstd::move(result));
        }

        auto worker_count = jobs < unique.len() ? jobs : unique.len();
        auto created = rstd::thread::BlockingTaskGroup<SourceResult<FetchedPackageSource>>::make(
            worker_count, unique.len());
        if (created.is_err()) {
            return Err(SourceError::System(
                String::make("create package source fetch executor"_str),
                SystemError::Io(String::make("create package source fetch executor"_str),
                                PathBuf::make(),
                                rstd::move(created).unwrap_err_unchecked())));
        }
        auto group = rstd::move(created).unwrap_unchecked();
        for (usize index {}; index < unique.len(); ++index) {
            auto request     = rstd::move(unique[index]);
            auto graph_root  = graph_root_.clone();
            auto options     = options_.clone();
            auto environment = environment_->clone();
            auto observer    = observer_;
            auto submitted =
                group.submit([index,
                              request     = rstd::move(request),
                              graph_root  = rstd::move(graph_root),
                              options     = rstd::move(options),
                              environment = rstd::move(environment),
                              observer]() mutable -> SourceResult<FetchedPackageSource> {
                    auto resolver = ToolResolver(environment);
                    auto manager  = SourceManager(
                        graph_root.as_path(), rstd::move(options), resolver, environment, observer);
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
        for (usize index {}; index < unique_keys.len(); ++index) {
            source_requests_.insert(rstd::move(unique_keys[index]), unique_indices[index]);
        }
        for (const auto binding : bindings) {
            result.push(binding.existing ? usize(binding.index)
                                         : usize(unique_indices[binding.index]));
        }
        return Ok(rstd::move(result));
    }

    auto acquire_external_frontier(Vec<PackageSourceFetchRequest> requests, usize jobs)
        -> SourceResult<Vec<ExternalSourceFetchOutcome>> {
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
        auto created = rstd::thread::BlockingTaskGroup<SourceResult<FetchedExternalSource>>::make(
            worker_count, unique.len());
        if (created.is_err()) {
            return Err(SourceError::System(
                String::make("create external source fetch executor"_str),
                SystemError::Io(String::make("create external source fetch executor"_str),
                                PathBuf::make(),
                                rstd::move(created).unwrap_err_unchecked())));
        }
        auto group = rstd::move(created).unwrap_unchecked();
        for (usize index {}; index < unique.len(); ++index) {
            auto request     = rstd::move(unique[index]);
            auto graph_root  = graph_root_.clone();
            auto options     = options_.clone();
            auto environment = environment_->clone();
            auto observer    = observer_;
            auto submitted =
                group.submit([index,
                              request     = rstd::move(request),
                              graph_root  = rstd::move(graph_root),
                              options     = rstd::move(options),
                              environment = rstd::move(environment),
                              observer]() mutable -> SourceResult<FetchedExternalSource> {
                    auto resolver = ToolResolver(environment);
                    auto manager  = SourceManager(
                        graph_root.as_path(), rstd::move(options), resolver, environment, observer);
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
                          ref<rstd::path::Path> declaring_root) -> SourceResult<AcquiredSource> {
        auto acquired = [&]() -> SourceResult<usize> {
            if (requirement.is_Git()) {
                const auto& git = requirement.as_Git();
                return acquire_git(git.url.as_str(), git.reference, false);
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

    auto take_package(usize source, ref<str> name) -> SourceResult<SelectedSourcePackage> {
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
            .source          = clone_source(entries_[source].source),
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
