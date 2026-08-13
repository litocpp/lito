module;
#include <rstd/enum.hpp>

export module lito.source.contract;

import rstd;
import lito.error;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito
{

enum class PackageSourceKind
{
    Path,
    Git,
};

enum class GitReferenceKind
{
    DefaultBranch,
    Branch,
    Tag,
    Rev,
    Commit,
};

struct GitReference {
    GitReferenceKind kind { GitReferenceKind::DefaultBranch };
    String           value;
};

auto git_reference_kind_name(GitReferenceKind kind) noexcept -> ref<str> {
    switch (kind) {
    case GitReferenceKind::DefaultBranch: return "default"_str;
    case GitReferenceKind::Branch: return "branch"_str;
    case GitReferenceKind::Tag: return "tag"_str;
    case GitReferenceKind::Rev: return "rev"_str;
    case GitReferenceKind::Commit: return "commit"_str;
    }
    return "default"_str;
}

auto git_references_equal(const GitReference& left, const GitReference& right) noexcept -> bool {
    return left.kind == right.kind && left.value == right.value;
}

enum class GitResolutionMode
{
    ReuseLocked,
    Refresh,
};

enum class NetworkPolicy
{
    Allow,
    Offline,
};

auto git_commit_is_valid(ref<str> value) noexcept -> bool {
    if (value.len() != usize(40)) return false;
    for (const auto character : value) {
        const auto ascii = character.to_primitive();
        if (! ((ascii >= '0' && ascii <= '9') || (ascii >= 'a' && ascii <= 'f') ||
               (ascii >= 'A' && ascii <= 'F'))) {
            return false;
        }
    }
    return true;
}

class PackageSourceRequirement {
    RSTD_ENUM(PackageSourceRequirement,
              (Path, (PathBuf path;)),
              (Git, (String url; GitReference reference;)))
};

class FetchIdentity {
    RSTD_ENUM(FetchIdentity,
              (Git, (String url; String commit;)),
              (Archive, (String url; String sha256;)))
};

auto git_fetch_identity(ref<str> url, ref<str> commit) -> FetchIdentity {
    return FetchIdentity::Git(String::make(url), String::make(commit));
}

auto archive_fetch_identity(ref<str> url, ref<str> sha256) -> FetchIdentity {
    return FetchIdentity::Archive(String::make(url), String::make(sha256));
}

auto fetch_identity_text(const FetchIdentity& identity) -> String {
    if (identity.is_Git()) {
        return rstd::format("lito-fetch-v1\ngit\n{}\n{}",
                            identity.as_Git().url.as_str(),
                            identity.as_Git().commit.as_str());
    }
    return rstd::format("lito-fetch-v1\narchive\n{}\n{}",
                        identity.as_Archive().url.as_str(),
                        identity.as_Archive().sha256.as_str());
}

auto fetch_identity_stable_key(const FetchIdentity& identity) -> String {
    auto serialized = fetch_identity_text(identity);
    return rstd::crypto::sha256_hex(serialized.as_str());
}

struct GitSourcePatch {
    String  git;
    PathBuf path;
};

struct PackageSourceConfig {
    Vec<GitSourcePatch> patches;
    Vec<PathBuf>        fetch_seeds;
    NetworkPolicy       network { NetworkPolicy::Allow };

    auto clone() const -> PackageSourceConfig {
        auto copied = Vec<GitSourcePatch>::with_capacity(patches.len());
        for (const auto& patch : patches) {
            copied.push(GitSourcePatch {
                .git  = patch.git.clone(),
                .path = patch.path.clone(),
            });
        }
        auto seeds = Vec<PathBuf>::with_capacity(fetch_seeds.len());
        for (const auto& seed : fetch_seeds) seeds.push(seed.clone());
        return PackageSourceConfig {
            .patches     = rstd::move(copied),
            .fetch_seeds = rstd::move(seeds),
            .network     = network,
        };
    }
};

struct ResolvedPackageSource {
    String            identity;
    PackageSourceKind kind { PackageSourceKind::Path };
    PathBuf           root_directory;
    PathBuf           path;
    String            git;
    GitReference      reference;
    String            commit;
};

} // namespace lito
