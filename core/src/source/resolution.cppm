export module lito.core:source.resolution;

import rstd;
import :source.git;
import :source.config;
import :source.error;

using namespace rstd::prelude;

export namespace lito::source
{

enum class SourceMaterializationPolicy
{
    Materialize,
    ExistingOnly,
};

struct SourceResolutionOptions {
    bool                        locked { false };
    GitResolutionMode           git { GitResolutionMode::ReuseLocked };
    SourceMaterializationPolicy materialization { SourceMaterializationPolicy::Materialize };
    Vec<GitSourcePin>           git_sources;
    PackageSourceConfig         sources;

    auto clone() const -> SourceResolutionOptions {
        auto pins = Vec<GitSourcePin>::with_capacity(git_sources.len());
        for (const auto& source : git_sources) {
            pins.push(GitSourcePin {
                .git    = source.git.clone(),
                .commit = source.commit.clone(),
            });
        }
        return SourceResolutionOptions {
            .locked          = locked,
            .git             = git,
            .materialization = materialization,
            .git_sources     = rstd::move(pins),
            .sources         = sources.clone(),
        };
    }
};

struct GitSourceSelection {
    Option<usize>  pin;
    Option<String> exact_commit;
};

auto select_git_source(const SourceResolutionOptions& options,
                       ref<str>                       url,
                       const GitReference& reference) -> SourceResult<GitSourceSelection> {
    auto matched = Option<usize> {};
    for (usize index {}; index < options.git_sources.len(); ++index) {
        const auto& source = options.git_sources[index];
        if (source.git.as_str() != url) continue;
        if (reference.kind == GitReferenceKind::Commit &&
            source.commit.as_str() != reference.value.as_str())
            continue;
        if (matched.is_some()) {
            return source_failure<GitSourceSelection>(
                rstd::format("lock contains more than one Git commit for '{}'", url));
        }
        matched = Some(index);
    }
    if (options.git == GitResolutionMode::Refresh &&
        options.sources.network == NetworkPolicy::Allow &&
        reference.kind != GitReferenceKind::Commit) {
        matched = None();
    }
    if (options.locked && matched.is_none()) {
        return source_failure<GitSourceSelection>(
            rstd::format("--locked has no source matching Git dependency '{}'", url));
    }
    auto exact_commit = Option<String> {};
    if (matched.is_some()) {
        exact_commit = Some(options.git_sources[*matched].commit.clone());
    } else if (reference.kind == GitReferenceKind::Commit) {
        exact_commit = Some(reference.value.clone());
    }
    return Ok(GitSourceSelection {
        .pin          = rstd::move(matched),
        .exact_commit = rstd::move(exact_commit),
    });
}

} // namespace lito::source
