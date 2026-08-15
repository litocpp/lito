module;
#include <rstd/enum.hpp>

export module lito.core:source.requirement;

import rstd;
import :source.git;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;

export namespace lito
{

enum class PackageSourceKind
{
    Path,
    Git,
};

class PackageSourceRequirement {
    RSTD_ENUM(PackageSourceRequirement,
              (Path, (PathBuf path;)),
              (Git, (String url; GitReference reference;)))
};

struct ResolvedPackageSource {
    String            identity;
    PackageSourceKind kind { PackageSourceKind::Path };
    PathBuf           root_directory;
    PathBuf           path;
    String            git;
    GitReference      reference;
    String            commit;

    auto clone() const -> ResolvedPackageSource {
        return ResolvedPackageSource {
            .identity       = identity.clone(),
            .kind           = kind,
            .root_directory = root_directory.clone(),
            .path           = path.clone(),
            .git            = git.clone(),
            .reference      = reference.clone(),
            .commit         = commit.clone(),
        };
    }
};

} // namespace lito
