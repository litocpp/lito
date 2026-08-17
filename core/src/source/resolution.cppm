export module lito.core:source.resolution;

import rstd;
import :source.git;
import :source.config;

using namespace rstd::prelude;

export namespace lito::source
{

struct SourceResolutionOptions {
    bool                locked { false };
    GitResolutionMode   git { GitResolutionMode::ReuseLocked };
    Vec<GitSourcePin>   git_sources;
    PackageSourceConfig sources;

    auto clone() const -> SourceResolutionOptions {
        auto pins = Vec<GitSourcePin>::with_capacity(git_sources.len());
        for (const auto& source : git_sources) {
            pins.push(GitSourcePin {
                .git = source.git.clone(),
                .reference =
                    GitReference {
                        .kind  = source.reference.kind,
                        .value = source.reference.value.clone(),
                    },
                .commit = source.commit.clone(),
            });
        }
        return SourceResolutionOptions {
            .locked      = locked,
            .git         = git,
            .git_sources = rstd::move(pins),
            .sources     = sources.clone(),
        };
    }
};

} // namespace lito::source
