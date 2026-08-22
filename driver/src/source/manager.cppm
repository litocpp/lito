module;
#include <rstd/macro.hpp>

export module lito.driver:source.manager;

import rstd;
import lito.crypto;
import lito.core;
import lito.tools;
import lito.system;
import :source.acquisition;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;
using namespace lito::system;
using namespace lito::tools;
using namespace rstd::literals;
using IndexMap = rstd::collections::BTreeMap<String, usize>;

using namespace lito::source;

auto path_components(ref<rstd::path::Path> path) -> SourceResult<Vec<String>> {
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

auto relative_path(ref<rstd::path::Path> root, ref<rstd::path::Path> target)
    -> SourceResult<PathBuf> {
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

template<typename T>
auto source_tool_result(ref<str> operation, lito::tools::ToolResult<T> result) -> SourceResult<T> {
    if (result.is_err()) {
        return Err(SourceError::Operation(
            String::make(operation),
            Box<dyn<rstd::error::Error>>::make(rstd::move(result).unwrap_err())));
    }
    return Ok(rstd::move(result).unwrap());
}

struct ManagedSourceEntry {
    ResolvedPackageSource source;
};

export namespace lito::source
{

class SourceManager {
    PathBuf                           graph_root_;
    SourceResolutionOptions           options_;
    Vec<ManagedSourceEntry>           entries_;
    IndexMap                          roots_ { IndexMap::make() };
    IndexMap                          source_identities_ { IndexMap::make() };
    IndexMap                          source_requests_ { IndexMap::make() };
    lito::tools::ToolResolver*        resolver_ {};
    const ResolvedProcessEnvironment* environment_ {};
    SourceEventSink                   observer_;
    Option<PathBuf>                   git_;
    Option<SourceCacheSession>        cache_session_;

    struct FetchedPackageSource {
        usize              request {};
        ManagedSourceEntry entry;
    };

    struct FetchedExternalSource {
        usize                      request {};
        ExternalSourceFetchOutcome outcome;
    };

    struct ResolvedGitSeed {
        enum class Kind
        {
            Patch,
            FetchSeed,
            Cache,
        };

        Kind    kind { Kind::Patch };
        String  commit;
        PathBuf path;
    };

    struct SourceWork {
        usize                     request {};
        PackageSourceFetchRequest source;
        Option<ResolvedGitSeed>   seed;
    };

    auto source_request_key(const PackageSourceRequirement& source,
                            ref<rstd::path::Path> declaring_root) -> SourceResult<String> {
        if (source.is_Path()) {
            return Ok(
                rstd::format("path\n{}\n{}", declaring_root, source.as_Path().path.as_path()));
        }
        if (source.is_Registry()) {
            return source_failure<String>(
                "Registry dependency requires RegistrySourceResolver materialization"_str);
        }
        const auto& git = source.as_Git();
        return Ok(git_requirement_identity(git.url.as_str(), git.reference));
    }

    auto source_work_groups(const Vec<PackageSourceFetchRequest>& requests) -> Vec<Vec<usize>> {
        auto groups     = Vec<Vec<usize>>::make();
        auto git_groups = rstd::collections::BTreeMap<String, usize>::make();
        for (usize index {}; index < requests.len(); ++index) {
            if (requests[index].source.is_Git()) {
                const auto& url      = requests[index].source.as_Git().url;
                auto        existing = git_groups.get(url.as_str());
                if (existing.is_some()) {
                    groups[**existing].push(usize(index));
                    continue;
                }
                git_groups.insert(url.clone(), groups.len());
            }
            auto group = Vec<usize>::make();
            group.push(usize(index));
            groups.push(rstd::move(group));
        }
        return groups;
    }

    auto take_entry(usize index) -> ManagedSourceEntry { return rstd::move(entries_[index]); }

    auto absorb_entry(ManagedSourceEntry entry) -> usize {
        auto existing = source_identities_.get(entry.source.identity.as_str());
        if (existing.is_some()) return **existing;
        auto root_text = entry.source.root_directory.as_path().to_str();
        if (root_text.is_some()) {
            auto by_root = roots_.get(*root_text);
            if (by_root.is_some()) {
                return **by_root;
            }
        }
        auto index = entries_.len();
        if (root_text.is_some()) roots_.insert(String::make(*root_text), index);
        source_identities_.insert(entry.source.identity.clone(), index);
        entries_.push(rstd::move(entry));
        return index;
    }

    auto clone_source(const ResolvedPackageSource& source) const -> ResolvedPackageSource {
        return source.clone();
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

    auto git_client(ref<str> source = "source resolution"_str, ref<str> owner = "Git"_str)
        -> SourceResult<lito::tools::GitClient> {
        if (git_.is_none()) {
            if (resolver_ == nullptr) {
                return source_failure<lito::tools::GitClient>(rstd::format(
                    "existing-only source resolution cannot resolve Git for '{}'", source));
            }
            const auto requirement = lito::tools::external_source_tool_requirement(
                lito::tools::HostToolCapability::GitCheckout, owner, source);
            auto resolved = resolver_->require(lito::tools::Tool::Git, requirement);
            if (resolved.is_err()) {
                return Err(SourceError::Operation(
                    String::make("resolve Git executable"_str),
                    Box<dyn<rstd::error::Error>>::make(rstd::move(resolved).unwrap_err())));
            }
            git_ = Some(rstd::move(resolved).unwrap().executable);
        }
        return Ok(lito::tools::GitClient(git_->clone(), *environment_));
    }

    auto emit_fetch(ref<str> source, ref<rstd::path::Path> destination) const noexcept -> void {
        if (observer_.notify == nullptr) return;
        observer_.notify(observer_.context,
                         SourceEvent { SourceEventKind::Fetch, source, destination });
    }

    auto seed_checkout(ref<str> url, ref<str> commit, ref<str> owner, ref<str> source)
        -> SourceResult<Option<PathBuf>> {
        auto identity = git_fetch_identity(url, commit);
        auto located  = rstd_try(locate_fetch_seed(options_.sources.fetch_seeds, identity));
        if (located.is_none()) return Ok(None());
        auto canonical = rstd::fs::canonicalize(located->as_path());
        if (canonical.is_err()) {
            return source_io_failure<Option<PathBuf>>("resolve Git fetch seed"_str,
                                                      located->as_path(),
                                                      rstd::move(canonical).unwrap_err());
        }
        auto git = rstd_try(
            git_client(source.is_empty() ? url : source, owner.is_empty() ? "Git"_str : owner));
        auto current = rstd_try(
            source_tool_result("inspect Git fetch seed"_str,
                               git.head(canonical->as_path(), "Git fetch seed inspection"_str)));
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

    auto git_checkout_receipt(ref<str> url, ref<str> commit) const -> String {
        return rstd::format("lito-git-checkout-v1\n{}\n{}\n", url, commit);
    }

    auto reusable_git_checkout(const GitCacheLayout& layout,
                               ref<str>              repository_key,
                               ref<str>              url,
                               ref<str>              commit) -> SourceResult<Option<PathBuf>> {
        auto receipt  = layout.checkout_receipt(repository_key, commit);
        auto metadata = rstd::fs::symlink_metadata(receipt.as_path());
        if (metadata.is_err()) {
            auto error = rstd::move(metadata).unwrap_err();
            if (error.kind() ==
                rstd::io::error::ErrorKind { rstd::io::error::ErrorKind::NotFound }) {
                return Ok(Option<PathBuf> {});
            }
            return source_io_failure<Option<PathBuf>>(
                "inspect Git checkout receipt"_str, receipt.as_path(), rstd::move(error));
        }
        if (! metadata->is_file()) {
            return source_failure<Option<PathBuf>>(rstd::format(
                "Git checkout receipt '{}' must be an ordinary file", receipt.as_path()));
        }
        auto contents = rstd::fs::read_to_string(receipt.as_path());
        if (contents.is_err()) {
            return source_io_failure<Option<PathBuf>>("read Git checkout receipt"_str,
                                                      receipt.as_path(),
                                                      rstd::move(contents).unwrap_err());
        }
        if (contents->as_str() != git_checkout_receipt(url, commit).as_str()) {
            return Ok(Option<PathBuf> {});
        }
        auto checkout          = layout.checkout(repository_key, commit);
        auto checkout_metadata = rstd::fs::symlink_metadata(checkout.as_path());
        if (checkout_metadata.is_err()) {
            auto error = rstd::move(checkout_metadata).unwrap_err();
            if (error.kind() ==
                rstd::io::error::ErrorKind { rstd::io::error::ErrorKind::NotFound }) {
                return Ok(Option<PathBuf> {});
            }
            return source_io_failure<Option<PathBuf>>(
                "inspect Git checkout"_str, checkout.as_path(), rstd::move(error));
        }
        if (! checkout_metadata->is_dir()) {
            return source_failure<Option<PathBuf>>(
                rstd::format("Git checkout '{}' must be a directory", checkout.as_path()));
        }
        auto canonical = rstd::fs::canonicalize(checkout.as_path());
        if (canonical.is_err()) {
            return source_io_failure<Option<PathBuf>>(
                "resolve Git checkout"_str, checkout.as_path(), rstd::move(canonical).unwrap_err());
        }
        return Ok(Some(rstd::move(canonical).unwrap()));
    }

    auto write_git_checkout_receipt(const GitCacheLayout& layout,
                                    ref<str>              repository_key,
                                    ref<str>              url,
                                    ref<str>              commit) -> SourceResult<empty> {
        auto receipt = layout.checkout_receipt(repository_key, commit);
        auto written = rstd::fs::write_atomic(
            receipt.as_path(), git_checkout_receipt(url, commit).as_str().as_bytes());
        if (written.is_err()) {
            return source_io_failure<empty>("write Git checkout receipt"_str,
                                            receipt.as_path(),
                                            rstd::move(written).unwrap_err());
        }
        return Ok(empty {});
    }

    auto cached_git_checkout(ref<str> url, ref<str> commit) -> SourceResult<Option<PathBuf>> {
        auto session = rstd_try(source_cache_session());
        auto cache   = session.open_git_cache();
        if (cache.is_err()) return Err(rstd::into<SourceError>(rstd::move(cache).unwrap_err()));
        auto layout         = rstd::move(cache).unwrap();
        auto repository_key = repository_directory_key(url);
        return reusable_git_checkout(layout, repository_key.as_str(), url, commit);
    }

    auto prepared_exact_git_input(ref<str>         url,
                                  Option<ref<str>> commit,
                                  ref<str>         owner  = "Git"_str,
                                  ref<str>         source = ""_str)
        -> SourceResult<Option<ResolvedGitSeed>> {
        if (commit.is_none()) return Ok(None());
        auto patch = rstd_try(patched_path(url));
        if (patch.is_some()) {
            return Ok(Some(ResolvedGitSeed {
                .kind   = ResolvedGitSeed::Kind::Patch,
                .commit = String::make(*commit),
                .path   = PathBuf::from(*patch),
            }));
        }
        auto seed = rstd_try(seed_checkout(url, *commit, owner, source));
        if (seed.is_some()) {
            return Ok(Some(ResolvedGitSeed {
                .kind   = ResolvedGitSeed::Kind::FetchSeed,
                .commit = String::make(*commit),
                .path   = rstd::move(seed).unwrap(),
            }));
        }
        auto cached = rstd_try(cached_git_checkout(url, *commit));
        if (cached.is_none()) return Ok(None());
        return Ok(Some(ResolvedGitSeed {
            .kind   = ResolvedGitSeed::Kind::Cache,
            .commit = String::make(*commit),
            .path   = rstd::move(cached).unwrap(),
        }));
    }

    auto prepared_git_seed(const PackageSourceFetchRequest& request)
        -> SourceResult<Option<ResolvedGitSeed>> {
        if (! request.source.is_Git()) return Ok(None());
        const auto& git = request.source.as_Git();
        auto selection  = rstd_try(select_git_source(options_, git.url.as_str(), git.reference));
        auto commit     = selection.exact_commit.is_some() ? Some(selection.exact_commit->as_str())
                                                           : Option<ref<str>> {};
        return prepared_exact_git_input(
            git.url.as_str(), commit, request.owner.as_str(), request.name.as_str());
    }

    auto prepare_frontier_inputs(const Vec<PackageSourceFetchRequest>& requests)
        -> SourceResult<Vec<Option<ResolvedGitSeed>>> {
        auto seeds       = Vec<Option<ResolvedGitSeed>>::with_capacity(requests.len());
        auto needs_cache = false;
        for (const auto& request : requests) {
            auto seed = rstd_try(prepared_git_seed(request));
            if (request.source.is_Git() && seed.is_none()) needs_cache = true;
            seeds.push(rstd::move(seed));
        }
        if (needs_cache) (void)rstd_try(source_cache_session());

        auto git_request = Option<usize> {};
        for (usize index {}; index < requests.len(); ++index) {
            const auto& request = requests[index];
            if (! request.source.is_Git()) continue;
            if (seeds[index].is_none()) {
                if (git_request.is_none()) git_request = Some(usize(index));
                continue;
            }
            if (seeds[index]->kind != ResolvedGitSeed::Kind::Cache &&
                seeds[index]->kind != ResolvedGitSeed::Kind::Patch) {
                continue;
            }
            const auto requirement = lito::tools::external_source_tool_requirement(
                lito::tools::HostToolCapability::GitCheckout,
                request.owner.is_empty() ? "Git"_str : request.owner.as_str(),
                request.name.is_empty() ? request.source.as_Git().url.as_str()
                                        : request.name.as_str());
            if (resolver_ != nullptr) {
                resolver_->report_not_required(requirement,
                                               seeds[index]->kind == ResolvedGitSeed::Kind::Cache
                                                   ? "Git checkout cache is reusable"_str
                                                   : "local source patch is selected"_str);
            }
        }
        if (git_request.is_some() && git_.is_none()) {
            const auto& request = requests[*git_request];
            (void)rstd_try(
                git_client(request.name.is_empty() ? request.source.as_Git().url.as_str()
                                                   : request.name.as_str(),
                           request.owner.is_empty() ? "Git"_str : request.owner.as_str()));
        }
        return Ok(rstd::move(seeds));
    }

    auto source_cache_session() -> SourceResult<SourceCacheSession> {
        if (cache_session_.is_none()) {
            auto root = LitoDataRoot::resolve();
            if (root.is_err()) {
                return Err(rstd::into<SourceError>(rstd::move(root).unwrap_err()));
            }
            auto acquired = options_.materialization == SourceMaterializationPolicy::ExistingOnly
                                ? root->open_source_cache()
                                : root->acquire_source_cache();
            if (acquired.is_err()) {
                return Err(rstd::into<SourceError>(rstd::move(acquired).unwrap_err()));
            }
            cache_session_ = Some(rstd::move(acquired).unwrap());
        }
        return Ok(cache_session_->clone());
    }

    auto clone_cache_session() const -> Option<SourceCacheSession> {
        if (cache_session_.is_none()) return None();
        return Some(cache_session_->clone());
    }

    auto repository_directory_key(ref<str> url) const -> String {
        auto source = url;
        auto end    = source.len();
        auto query  = source.find("?"_str);
        if (query.is_some() && *query < end) end = *query;
        auto fragment = source.find("#"_str);
        if (fragment.is_some() && *fragment < end) end = *fragment;
        source         = *source.get(usize {}, end);
        auto component = ""_str;
        auto scheme    = source.split_once("://"_str);
        if (scheme.is_some()) {
            auto path = scheme->get<1>().split_once("/"_str);
            if (path.is_some()) component = path->get<1>();
        } else {
            component = source;
        }
        while (component.ends_with("/"_str)) {
            component = *component.get(usize {}, component.len() - usize(1));
        }
        auto slash = component.rsplit_once("/"_str);
        if (slash.is_some()) component = slash->get<1>();
        if (slash.is_none() && scheme.is_none()) {
            auto colon = component.rsplit_once(":"_str);
            if (colon.is_some()) component = colon->get<1>();
        }
        auto without_suffix = component.strip_suffix(".git"_str);
        if (without_suffix.is_some()) component = *without_suffix;

        auto prefix = String::make();
        for (auto value : component) {
            if (prefix.len() == usize(48)) break;
            const auto ascii = value.to_primitive();
            const auto safe  = (ascii >= 'a' && ascii <= 'z') || (ascii >= 'A' && ascii <= 'Z') ||
                               (ascii >= '0' && ascii <= '9') || ascii == '-' || ascii == '_';
            prefix.push_ascii(safe ? static_cast<char>(ascii) : '-');
        }
        if (prefix.is_empty() || prefix.as_str() == "-"_str) {
            prefix = String::make("repository"_str);
        }
        prefix.push('-');
        prefix.push_str(lito::crypto::sha256_hex(url).as_str());
        return prefix;
    }

    auto repository(const GitCacheLayout& layout, ref<str> repository_key, ref<str> url)
        -> SourceResult<PathBuf> {
        auto database = PathBuf::from(layout.root()).join(PathBuf::from("db"_str).as_path());
        auto created  = rstd::fs::create_dir_all(database.as_path());
        if (created.is_err()) {
            return source_io_failure<PathBuf>("create Git database cache"_str,
                                              database.as_path(),
                                              rstd::move(created).unwrap_err());
        }
        auto repository = layout.repository(repository_key);
        auto exists     = rstd::fs::exists(repository.as_path());
        if (exists.is_err()) {
            return source_io_failure<PathBuf>("inspect Git cache repository"_str,
                                              repository.as_path(),
                                              rstd::move(exists).unwrap_err());
        }
        if (! *exists) {
            auto git = rstd_try(git_client(url));
            rstd_try(source_tool_result("initialize Git cache"_str,
                                        git.initialize_bare(repository.as_path())));
        }

        auto git        = rstd_try(git_client(url));
        auto configured = rstd_try(source_tool_result("inspect Git cache remote"_str,
                                                      git.remote_origin(repository.as_path())));
        if (configured.is_none()) {
            rstd_try(source_tool_result("configure Git cache remote"_str,
                                        git.set_remote_origin(repository.as_path(), url)));
            configured = Some(String::make(url));
        }
        if (configured->as_str() != url) {
            return source_failure<PathBuf>(rstd::format(
                "Git cache key collision between '{}' and '{}'", configured->as_str(), url));
        }
        return Ok(rstd::move(repository));
    }

    auto object_exists(ref<rstd::path::Path> repository, ref<str> commit) -> SourceResult<bool> {
        auto git = rstd_try(git_client());
        return source_tool_result("inspect Git object"_str, git.commit_exists(repository, commit));
    }

    auto fetch(ref<rstd::path::Path> repository, ref<str> url, ref<str> revision)
        -> SourceResult<String> {
        auto local_reference =
            rstd::format("refs/lito/fetch/{}", lito::crypto::sha256_hex(revision).as_str());
        auto source = rstd::format("{}#{}", url, revision);
        emit_fetch(source.as_str(), repository);
        auto git = rstd_try(git_client(url));
        rstd_try(source_tool_result("fetch Git source"_str,
                                    git.fetch(repository, revision, local_reference.as_str())));
        return Ok(rstd::move(local_reference));
    }

    auto rev_parse(ref<rstd::path::Path> repository, ref<str> revision) -> SourceResult<String> {
        auto git = rstd_try(git_client());
        return source_tool_result("resolve Git source revision"_str,
                                  git.rev_parse_commit(repository, revision));
    }

    auto resolve_commit(ref<rstd::path::Path>     repository,
                        ref<str>                  url,
                        const GitReference&       reference,
                        Option<ref<GitSourcePin>> locked) -> SourceResult<String> {
        if (locked.is_some()) {
            auto present = object_exists(repository, (*locked)->commit.as_str());
            if (present.is_err()) return Err(rstd::move(present).unwrap_err());
            if (! *present) {
                (void)rstd_try(fetch(repository, url, (*locked)->commit.as_str()));
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
        auto fetched = rstd_try(fetch(repository, url, revision.as_str()));
        return rev_parse(repository, fetched.as_str());
    }

    auto checkout(const GitCacheLayout& layout,
                  ref<str>              repository_key,
                  ref<rstd::path::Path> repository,
                  ref<str>              url,
                  ref<str>              commit) -> SourceResult<PathBuf> {
        auto checkout = layout.checkout(repository_key, commit);
        auto parent   = checkout.as_path().parent();
        if (parent.is_none()) {
            return source_failure<PathBuf>("Git checkout cache has no parent directory"_str);
        }
        auto created = rstd::fs::create_dir_all(*parent);
        if (created.is_err()) {
            return source_io_failure<PathBuf>(
                "create Git checkout cache"_str, *parent, rstd::move(created).unwrap_err());
        }
        auto exists = rstd::fs::exists(checkout.as_path());
        if (exists.is_err()) {
            return source_io_failure<PathBuf>(
                "inspect Git checkout"_str, checkout.as_path(), rstd::move(exists).unwrap_err());
        }
        if (*exists) {
            auto reusable = rstd_try(reusable_git_checkout(layout, repository_key, url, commit));
            if (reusable.is_some()) return Ok(rstd::move(reusable).unwrap());
            auto git     = rstd_try(git_client());
            auto current = rstd_try(source_tool_result(
                "inspect Git checkout"_str,
                git.try_head(checkout.as_path(), "Git checkout inspection"_str)));
            if (current.is_some() && current->as_str() == commit) {
                rstd_try(write_git_checkout_receipt(layout, repository_key, url, commit));
                return Ok(rstd::move(checkout));
            }
            auto removed = rstd::fs::remove_dir_all(checkout.as_path());
            if (removed.is_err()) {
                return source_io_failure<PathBuf>("recover Git checkout"_str,
                                                  checkout.as_path(),
                                                  rstd::move(removed).unwrap_err());
            }
        }

        auto git = rstd_try(git_client());
        rstd_try(source_tool_result("create Git source checkout"_str,
                                    git.clone_shared(repository, checkout.as_path())));
        rstd_try(source_tool_result("checkout Git source commit"_str,
                                    git.checkout_detached(checkout.as_path(), commit)));
        rstd_try(write_git_checkout_receipt(layout, repository_key, url, commit));
        return Ok(rstd::move(checkout));
    }

    auto acquire_path(ref<rstd::path::Path> requested) -> SourceResult<usize> {
        auto canonical = rstd::fs::canonicalize(requested);
        if (canonical.is_err()) {
            return source_io_failure<usize>(
                "resolve path source"_str, requested, rstd::move(canonical).unwrap_err());
        }
        auto source_root = rstd::move(canonical).unwrap();
        auto root_text   = source_root.as_path().to_str();
        if (root_text.is_none()) {
            return source_failure<usize>(rstd::format(
                "normalized source root '{}' is not valid UTF-8", source_root.as_path()));
        }
        auto existing = roots_.get(*root_text);
        if (existing.is_some()) return Ok(**existing);
        auto root_key = String::make(*root_text);
        auto relative = relative_path(graph_root_.as_path(), source_root.as_path());
        if (relative.is_err()) return Err(rstd::move(relative).unwrap_err());
        auto normalized = rstd::move(relative).unwrap();
        auto id         = path_source_identity(normalized.as_path());
        auto index      = entries_.len();
        entries_.push(ManagedSourceEntry {
            .source =
                ResolvedPackageSource {
                    .identity       = rstd::move(id),
                    .kind           = PackageSourceKind::Path,
                    .root_directory = rstd::move(source_root),
                    .path           = rstd::move(normalized),
                },
        });
        roots_.insert(rstd::move(root_key), index);
        source_identities_.insert(entries_[index].source.identity.clone(), index);
        return Ok(index);
    }

    auto register_git_source(ref<str>            url,
                             const GitReference& reference,
                             String              precise_commit,
                             PathBuf             checkout_root) -> SourceResult<usize> {
        auto patch = rstd_try(patched_path(url));
        if (patch.is_some()) {
            auto physical = rstd::fs::canonicalize(*patch);
            if (physical.is_err()) {
                return source_io_failure<usize>(
                    "resolve patched Git source"_str, *patch, rstd::move(physical).unwrap_err());
            }
            checkout_root = rstd::move(physical).unwrap();
        }

        auto id       = git_source_identity(url, precise_commit.as_str());
        auto existing = source_identities_.get(id.as_str());
        if (existing.is_some()) return Ok(**existing);

        auto root_text = checkout_root.as_path().to_str();
        if (root_text.is_none()) {
            return source_failure<usize>(
                rstd::format("Git checkout '{}' is not valid UTF-8", checkout_root.as_path()));
        }
        auto root_key = String::make(*root_text);
        auto index    = entries_.len();
        entries_.push(ManagedSourceEntry {
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
        });
        roots_.insert(rstd::move(root_key), index);
        source_identities_.insert(rstd::move(id), index);
        return Ok(index);
    }

    auto acquire_git(ref<str> url, const GitReference& reference) -> SourceResult<usize> {
        auto request_key = git_requirement_identity(url, reference);
        auto requested   = source_requests_.get(request_key.as_str());
        if (requested.is_some()) return Ok(**requested);
        auto selection = rstd_try(select_git_source(options_, url, reference));
        auto pin       = Option<ref<GitSourcePin>> {};
        if (selection.pin.is_some()) {
            pin = Some(ref<GitSourcePin>::from_raw_parts(
                rstd::addressof(options_.git_sources[*selection.pin])));
        }
        auto exact_commit = selection.exact_commit.is_some()
                                ? Some(selection.exact_commit->as_str())
                                : Option<ref<str>> {};
        auto input        = rstd_try(prepared_exact_git_input(url, exact_commit));
        if (input.is_some()) {
            auto resolved   = rstd::move(input).unwrap();
            auto registered = rstd_try(register_git_source(
                url, reference, rstd::move(resolved.commit), rstd::move(resolved.path)));
            source_requests_.insert(rstd::move(request_key), registered);
            return Ok(registered);
        }

        if (options_.materialization == SourceMaterializationPolicy::ExistingOnly) {
            return source_failure<usize>(
                rstd::format("existing-only source resolution cannot materialize Git source '{}'",
                             request_key.as_str()));
        }

        auto session = rstd_try(source_cache_session());
        auto cache   = session.open_git_cache();
        if (cache.is_err()) {
            return Err(rstd::into<SourceError>(rstd::move(cache).unwrap_err()));
        }
        auto layout          = rstd::move(cache).unwrap();
        auto repository_key  = repository_directory_key(url);
        auto repository_path = rstd_try(repository(layout, repository_key.as_str(), url));
        if (exact_commit.is_some()) {
            auto present = rstd_try(object_exists(repository_path.as_path(), *exact_commit));
            if (present) {
                auto precise_commit = rstd_try(rev_parse(repository_path.as_path(), *exact_commit));
                auto local          = rstd_try(checkout(layout,
                                                        repository_key.as_str(),
                                                        repository_path.as_path(),
                                                        url,
                                                        precise_commit.as_str()));
                auto canonical      = rstd::fs::canonicalize(local.as_path());
                if (canonical.is_err()) {
                    return source_io_failure<usize>("resolve Git checkout"_str,
                                                    local.as_path(),
                                                    rstd::move(canonical).unwrap_err());
                }
                auto registered = rstd_try(register_git_source(
                    url, reference, rstd::move(precise_commit), rstd::move(canonical).unwrap()));
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
        auto local     = rstd_try(checkout(layout,
                                           repository_key.as_str(),
                                           repository_path.as_path(),
                                           url,
                                           precise_commit.as_str()));
        auto canonical = rstd::fs::canonicalize(local.as_path());
        if (canonical.is_err()) {
            return source_io_failure<usize>(
                "resolve Git checkout"_str, local.as_path(), rstd::move(canonical).unwrap_err());
        }
        auto registered = rstd_try(register_git_source(
            url, reference, rstd::move(precise_commit), rstd::move(canonical).unwrap()));
        source_requests_.insert(rstd::move(request_key), registered);
        return Ok(registered);
    }

    auto acquire_seeded_git(const PackageSourceRequirement& requirement, ResolvedGitSeed seed)
        -> SourceResult<usize> {
        const auto& git         = requirement.as_Git();
        auto        registered  = rstd_try(register_git_source(
            git.url.as_str(), git.reference, rstd::move(seed.commit), rstd::move(seed.path)));
        auto        request_key = git_requirement_identity(git.url.as_str(), git.reference);
        source_requests_.insert(rstd::move(request_key), registered);
        return Ok(registered);
    }

    auto acquire_external_seeded(const PackageSourceRequirement& requirement, ResolvedGitSeed seed)
        -> SourceResult<AcquiredSource> {
        auto source = rstd_try(acquire_seeded_git(requirement, rstd::move(seed)));
        return Ok(AcquiredSource {
            .root      = entries_[source].source.root_directory.clone(),
            .identity  = entries_[source].source.identity.clone(),
            .cacheable = true,
        });
    }

    auto resolve_git(ref<str> url, const GitReference& reference)
        -> SourceResult<ResolvedPackageSource> {
        auto selection = rstd_try(select_git_source(options_, url, reference));
        auto pin       = Option<ref<GitSourcePin>> {};
        if (selection.pin.is_some()) {
            pin = Some(ref<GitSourcePin>::from_raw_parts(
                rstd::addressof(options_.git_sources[*selection.pin])));
        }
        auto precise_commit = String::make();
        auto exact_commit   = selection.exact_commit.is_some()
                                  ? Some(selection.exact_commit->as_str())
                                  : Option<ref<str>> {};
        if (exact_commit.is_some()) {
            auto input = rstd_try(prepared_exact_git_input(url, exact_commit));
            if (input.is_some()) precise_commit = String::make(*exact_commit);
        }
        if (precise_commit.is_empty()) {
            if (options_.materialization == SourceMaterializationPolicy::ExistingOnly) {
                return source_failure<ResolvedPackageSource>(rstd::format(
                    "existing-only source resolution cannot materialize Git source '{}'",
                    git_requirement_identity(url, reference).as_str()));
            }
            auto session = rstd_try(source_cache_session());
            auto cache   = session.open_git_cache();
            if (cache.is_err()) {
                return Err(rstd::into<SourceError>(rstd::move(cache).unwrap_err()));
            }
            auto layout          = rstd::move(cache).unwrap();
            auto repository_key  = repository_directory_key(url);
            auto repository_path = rstd_try(repository(layout, repository_key.as_str(), url));
            if (exact_commit.is_some()) {
                auto present = rstd_try(object_exists(repository_path.as_path(), *exact_commit));
                if (present) {
                    precise_commit = rstd_try(rev_parse(repository_path.as_path(), *exact_commit));
                }
            }
            if (precise_commit.is_empty() && options_.sources.network == NetworkPolicy::Offline) {
                auto requirement = git_requirement_identity(url, reference);
                return source_failure<ResolvedPackageSource>(
                    rstd::format("offline source resolution cannot fetch Git source '{}'",
                                 requirement.as_str()));
            }
            if (precise_commit.is_empty()) {
                precise_commit = rstd_try(
                    resolve_commit(repository_path.as_path(), url, reference, rstd::move(pin)));
            }
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
                           SourceResolutionOptions           options,
                           lito::tools::ToolResolver*        resolver,
                           const ResolvedProcessEnvironment& environment,
                           SourceEventSink                   observer      = {},
                           Option<SourceCacheSession>        cache_session = None(),
                           Option<PathBuf>                   git           = None())
        : graph_root_(PathBuf::from(graph_root)),
          options_(rstd::move(options)),
          resolver_(resolver),
          environment_(rstd::addressof(environment)),
          observer_(observer),
          git_(rstd::move(git)),
          cache_session_(rstd::move(cache_session)) {}

    explicit SourceManager(ref<rstd::path::Path>             graph_root,
                           SourceResolutionOptions           options,
                           lito::tools::ToolResolver&        resolver,
                           const ResolvedProcessEnvironment& environment,
                           SourceEventSink                   observer      = {},
                           Option<SourceCacheSession>        cache_session = None(),
                           Option<PathBuf>                   git           = None())
        : SourceManager(graph_root,
                        rstd::move(options),
                        rstd::addressof(resolver),
                        environment,
                        observer,
                        rstd::move(cache_session),
                        rstd::move(git)) {}

    auto resolve_external_source(const PackageSourceRequirement& requirement,
                                 ref<rstd::path::Path>           declaring_root)
        -> SourceResult<ResolvedPackageSource> {
        if (requirement.is_Registry()) {
            return source_failure<ResolvedPackageSource>(
                "Registry dependency requires RegistrySourceResolver materialization"_str);
        }
        if (requirement.is_Git()) {
            return resolve_git(requirement.as_Git().url.as_str(), requirement.as_Git().reference);
        }
        auto requested = PathBuf::from(declaring_root).join(requirement.as_Path().path.as_path());
        auto acquired  = acquire_path(requested.as_path());
        if (acquired.is_err()) return Err(rstd::move(acquired).unwrap_err());
        return Ok(clone_source(entries_[*acquired].source));
    }

    auto acquire_root(ref<rstd::path::Path> root) -> SourceResult<usize> {
        return acquire_path(root);
    }

    auto acquire(const PackageSourceRequirement& requirement, ref<rstd::path::Path> declaring_root)
        -> SourceResult<usize> {
        if (requirement.is_Registry()) {
            return source_failure<usize>(
                "Registry dependency requires RegistrySourceResolver materialization"_str);
        }
        if (requirement.is_Git()) {
            const auto& git = requirement.as_Git();
            return acquire_git(git.url.as_str(), git.reference);
        }
        auto requested = PathBuf::from(declaring_root).join(requirement.as_Path().path.as_path());
        return acquire_path(requested.as_path());
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

        auto seeds = rstd_try(prepare_frontier_inputs(unique));

        auto work_groups  = source_work_groups(unique);
        auto worker_count = jobs < work_groups.len() ? jobs : work_groups.len();
        auto created =
            rstd::thread::BlockingTaskGroup<SourceResult<Vec<FetchedPackageSource>>>::make(
                worker_count, work_groups.len());
        if (created.is_err()) {
            return Err(SourceError::System(
                String::make("create package source fetch executor"_str),
                SystemError::Io(String::make("create package source fetch executor"_str),
                                PathBuf::make(),
                                rstd::move(created).unwrap_err_unchecked())));
        }
        auto group = rstd::move(created).unwrap_unchecked();
        for (auto& indices : work_groups) {
            auto work = Vec<SourceWork>::with_capacity(indices.len());
            for (auto index : indices) {
                work.push(SourceWork {
                    .request = index,
                    .source  = rstd::move(unique[index]),
                    .seed    = rstd::move(seeds[index]),
                });
            }
            auto graph_root  = graph_root_.clone();
            auto options     = options_.clone();
            auto environment = environment_->clone();
            auto observer    = observer_;
            auto cache       = clone_cache_session();
            auto git         = git_.is_some() ? Some(git_->clone()) : None();
            auto submitted   = group.submit(
                [work        = rstd::move(work),
                 graph_root  = rstd::move(graph_root),
                 options     = rstd::move(options),
                 environment = rstd::move(environment),
                 observer,
                 cache = rstd::move(cache),
                 git   = rstd::move(git)]() mutable -> SourceResult<Vec<FetchedPackageSource>> {
                    auto results = Vec<FetchedPackageSource>::with_capacity(work.len());
                    for (auto& item : work) {
                        auto resolver   = lito::tools::ToolResolver(environment);
                        auto item_cache = cache.is_some() ? Some(cache->clone()) : None();
                        auto item_git   = git.is_some() ? Some(git->clone()) : None();
                        auto manager    = SourceManager(graph_root.as_path(),
                                                        options.clone(),
                                                        resolver,
                                                        environment,
                                                        observer,
                                                        rstd::move(item_cache),
                                                        rstd::move(item_git));
                        auto acquired =
                            item.seed.is_some()
                                ? manager.acquire_seeded_git(item.source.source,
                                                             rstd::move(item.seed).unwrap())
                                : manager.acquire(item.source.source,
                                                  item.source.declaring_root.as_path());
                        if (acquired.is_err()) return Err(rstd::move(acquired).unwrap_err());
                        results.push(FetchedPackageSource {
                            .request = item.request,
                            .entry   = manager.take_entry(*acquired),
                        });
                    }
                    return Ok(rstd::move(results));
                });
            if (submitted.is_err()) {
                return source_failure<Vec<usize>>("cannot submit package source fetch task"_str);
            }
        }
        auto outcomes = rstd::move(group).join();
        auto fetched  = Vec<Option<ManagedSourceEntry>>::with_capacity(unique.len());
        for (usize index {}; index < unique.len(); ++index) fetched.push(None());
        for (auto& outcome : outcomes) {
            auto value = rstd::move(outcome).into_value();
            if (value.is_none()) {
                return source_failure<Vec<usize>>("package source fetch task was cancelled"_str);
            }
            auto task = rstd::move(value).unwrap_unchecked();
            if (task.is_err()) return Err(rstd::move(task).unwrap_err());
            auto sources = rstd::move(task).unwrap();
            for (auto& source : sources) {
                fetched[source.request] = Some(rstd::move(source.entry));
            }
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

        auto seeds = rstd_try(prepare_frontier_inputs(unique));

        auto work_groups  = source_work_groups(unique);
        auto worker_count = jobs < work_groups.len() ? jobs : work_groups.len();
        auto created =
            rstd::thread::BlockingTaskGroup<SourceResult<Vec<FetchedExternalSource>>>::make(
                worker_count, work_groups.len());
        if (created.is_err()) {
            return Err(SourceError::System(
                String::make("create external source fetch executor"_str),
                SystemError::Io(String::make("create external source fetch executor"_str),
                                PathBuf::make(),
                                rstd::move(created).unwrap_err_unchecked())));
        }
        auto group = rstd::move(created).unwrap_unchecked();
        for (auto& indices : work_groups) {
            auto work = Vec<SourceWork>::with_capacity(indices.len());
            for (auto index : indices) {
                work.push(SourceWork {
                    .request = index,
                    .source  = rstd::move(unique[index]),
                    .seed    = rstd::move(seeds[index]),
                });
            }
            auto graph_root  = graph_root_.clone();
            auto options     = options_.clone();
            auto environment = environment_->clone();
            auto observer    = observer_;
            auto cache       = clone_cache_session();
            auto git         = git_.is_some() ? Some(git_->clone()) : None();
            auto submitted   = group.submit(
                [work        = rstd::move(work),
                 graph_root  = rstd::move(graph_root),
                 options     = rstd::move(options),
                 environment = rstd::move(environment),
                 observer,
                 cache = rstd::move(cache),
                 git   = rstd::move(git)]() mutable -> SourceResult<Vec<FetchedExternalSource>> {
                    auto results = Vec<FetchedExternalSource>::with_capacity(work.len());
                    for (auto& item : work) {
                        auto resolver   = lito::tools::ToolResolver(environment);
                        auto item_cache = cache.is_some() ? Some(cache->clone()) : None();
                        auto item_git   = git.is_some() ? Some(git->clone()) : None();
                        auto manager    = SourceManager(graph_root.as_path(),
                                                        options.clone(),
                                                        resolver,
                                                        environment,
                                                        observer,
                                                        rstd::move(item_cache),
                                                        rstd::move(item_git));
                        auto acquired =
                            item.seed.is_some()
                                ? manager.acquire_external_seeded(item.source.source,
                                                                  rstd::move(item.seed).unwrap())
                                : manager.acquire_external(item.source.source,
                                                           item.source.declaring_root.as_path());
                        if (acquired.is_err()) return Err(rstd::move(acquired).unwrap_err());
                        results.push(FetchedExternalSource {
                            .request = item.request,
                            .outcome =
                                ExternalSourceFetchOutcome {
                                    .acquired = rstd::move(acquired).unwrap(),
                                    .sources  = manager.finish(),
                                },
                        });
                    }
                    return Ok(rstd::move(results));
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
            auto sources = rstd::move(task).unwrap();
            for (auto& source : sources) {
                fetched[source.request] = Some(rstd::move(source.outcome));
            }
        }
        for (auto binding : bindings) {
            if (fetched[binding].is_none()) {
                return source_failure<Vec<ExternalSourceFetchOutcome>>(
                    "external source fetch result is missing"_str);
            }
            result.push(clone_external_outcome(*fetched[binding]));
        }
        cache_session_ = None();
        return Ok(rstd::move(result));
    }

    auto acquire_external(const PackageSourceRequirement& requirement,
                          ref<rstd::path::Path> declaring_root) -> SourceResult<AcquiredSource> {
        auto acquired = [&]() -> SourceResult<usize> {
            if (requirement.is_Registry()) {
                return source_failure<usize>(
                    "Registry dependency requires RegistrySourceResolver materialization"_str);
            }
            if (requirement.is_Git()) {
                const auto& git = requirement.as_Git();
                return acquire_git(git.url.as_str(), git.reference);
            }
            auto requested =
                PathBuf::from(declaring_root).join(requirement.as_Path().path.as_path());
            return acquire_path(requested.as_path());
        }();
        if (acquired.is_err()) return Err(rstd::move(acquired).unwrap_err());
        const auto index = rstd::move(acquired).unwrap();
        return Ok(AcquiredSource {
            .root      = entries_[index].source.root_directory.clone(),
            .identity  = entries_[index].source.identity.clone(),
            .cacheable = entries_[index].source.kind == PackageSourceKind::Git,
        });
    }

    auto source_identity(usize source) const noexcept -> ref<str> {
        return entries_[source].source.identity.as_str();
    }

    auto resolved_source(usize source) const -> ResolvedPackageSource {
        return clone_source(entries_[source].source);
    }

    auto source_root(usize source) const -> PathBuf {
        return entries_[source].source.root_directory.clone();
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

} // namespace lito::source
