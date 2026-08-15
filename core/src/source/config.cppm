export module lito.core:source.config;

import rstd;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;

export namespace lito
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

} // namespace lito
