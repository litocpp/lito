module;
#include <rstd/enum.hpp>

export module lito.lock.contract;

import rstd;
import lito.error;
import lito.source.contract;
export import lito.lock.error_contract;

using namespace rstd::prelude;

export namespace lito
{

struct LockConfig {
    PathBuf path;
};

struct LockedGitSource {
    String       git;
    GitReference reference;
    String       commit;
};

class LockedPackageSource {
    RSTD_ENUM(LockedPackageSource,
              (Path, (PathBuf path;)),
              (Git, (String url; GitReference reference; String commit;)))
};

class LockedExternalSource {
    RSTD_ENUM(LockedExternalSource,
              (Path, (PathBuf path;)),
              (Git, (String url; GitReference reference; String commit;)),
              (Archive, (String url; String sha256;)))
};

struct LockedPackage {
    String              name;
    Option<String>      version;
    LockedPackageSource source;
    PathBuf             manifest;
    Vec<String>         dependencies;
    Vec<String>         runtime_dependencies;
};

struct LockedExternal {
    String               package;
    String               alias;
    String               provider;
    Vec<String>          architectures;
    LockedExternalSource source;
};

struct LockedProject {
    Vec<LockedPackage>  packages;
    Vec<LockedExternal> externals;
};

struct PackageResolutionOptions {
    bool                 locked { false };
    GitResolutionMode    git { GitResolutionMode::ReuseLocked };
    Vec<LockedGitSource> git_sources;
    PackageSourceConfig  sources;

    auto clone() const -> PackageResolutionOptions {
        auto locked_sources = Vec<LockedGitSource>::with_capacity(git_sources.len());
        for (const auto& source : git_sources) {
            locked_sources.push(LockedGitSource {
                .git = source.git.clone(),
                .reference =
                    GitReference {
                        .kind  = source.reference.kind,
                        .value = source.reference.value.clone(),
                    },
                .commit = source.commit.clone(),
            });
        }
        return PackageResolutionOptions {
            .locked      = locked,
            .git         = git,
            .git_sources = rstd::move(locked_sources),
            .sources     = sources.clone(),
        };
    }
};

enum class LockStatus
{
    Unchanged,
    Updated,
};

} // namespace lito
