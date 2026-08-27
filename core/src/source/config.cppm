export module lito.core:source.config;

import rstd;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;

export namespace lito::source
{

enum class NetworkPolicy
{
    Allow,
    Offline,
};

struct GitSourcePatch {
    String  git;
    PathBuf path;
};

struct PackageSourceConfig {
    Vec<GitSourcePatch> patches;
    Vec<PathBuf>        source_bundles;
    NetworkPolicy       network { NetworkPolicy::Allow };

    auto clone() const -> PackageSourceConfig {
        auto copied = Vec<GitSourcePatch>::with_capacity(patches.len());
        for (const auto& patch : patches) {
            copied.push(GitSourcePatch {
                .git  = patch.git.clone(),
                .path = patch.path.clone(),
            });
        }
        auto bundles = Vec<PathBuf>::with_capacity(source_bundles.len());
        for (const auto& bundle : source_bundles) bundles.push(bundle.clone());
        return PackageSourceConfig {
            .patches        = rstd::move(copied),
            .source_bundles = rstd::move(bundles),
            .network        = network,
        };
    }
};

} // namespace lito::source
