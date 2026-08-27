export module lito.core:source.resolution;

import rstd;
import :source.git;
import :source.config;
import :source.error;
import :registry.digest;
import :registry.identity;
import :registry.version;

using namespace rstd::prelude;

export namespace lito::source
{

enum class SourceMaterializationPolicy
{
    Materialize,
    ExistingOnly,
};

struct RegistrySourcePin {
    lito::registry::RegistryPackageId package;
    lito::registry::SemanticVersion   version;
    lito::registry::PackageChecksum   checksum;

    auto clone() const -> RegistrySourcePin {
        return RegistrySourcePin {
            .package  = package.clone(),
            .version  = version.clone(),
            .checksum = checksum.clone(),
        };
    }
};

struct SourceResolutionOptions {
    bool                        locked { false };
    GitResolutionMode           git { GitResolutionMode::ReuseLocked };
    SourceMaterializationPolicy materialization { SourceMaterializationPolicy::Materialize };
    Vec<GitSourcePin>           git_sources;
    Vec<RegistrySourcePin>      registry_sources;
    PackageSourceConfig         sources;

    auto clone() const -> SourceResolutionOptions {
        auto pins = Vec<GitSourcePin>::with_capacity(git_sources.len());
        for (const auto& source : git_sources) {
            pins.push(GitSourcePin {
                .git    = source.git.clone(),
                .commit = source.commit.clone(),
            });
        }
        auto registry_pins = Vec<RegistrySourcePin>::with_capacity(registry_sources.len());
        for (const auto& source : registry_sources) registry_pins.push(source.clone());
        return SourceResolutionOptions {
            .locked           = locked,
            .git              = git,
            .materialization  = materialization,
            .git_sources      = rstd::move(pins),
            .registry_sources = rstd::move(registry_pins),
            .sources          = sources.clone(),
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
