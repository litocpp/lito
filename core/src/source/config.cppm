module;
#include <rstd/enum.hpp>

export module lito.core:source.config;

import rstd;
import :source.git;
import :registry.version;

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

class BuiltinPackageSource {
    RSTD_ENUM(BuiltinPackageSource,
              (Registry,
               (Option<String> registry; lito::registry::VersionRequirement requirement;)),
              (Git, (String url; GitReference reference;)),
              (Path, (PathBuf path;)))
};

struct BuiltinPackageSourceEntry {
    String               id;
    BuiltinPackageSource source;
};

struct PackageSourceConfig {
    Vec<GitSourcePatch>            patches;
    Vec<BuiltinPackageSourceEntry> builtin_packages;
    Vec<PathBuf>                   source_bundles;
    NetworkPolicy                  network { NetworkPolicy::Allow };

    auto builtin_package(ref<str> id) const noexcept -> Option<ref<BuiltinPackageSource>> {
        for (const auto& package : builtin_packages) {
            if (package.id == id) {
                return Some(
                    ref<BuiltinPackageSource>::from_raw_parts(rstd::addressof(package.source)));
            }
        }
        return None();
    }

    auto clone() const -> PackageSourceConfig {
        auto copied = Vec<GitSourcePatch>::with_capacity(patches.len());
        for (const auto& patch : patches) {
            copied.push(GitSourcePatch {
                .git  = patch.git.clone(),
                .path = patch.path.clone(),
            });
        }
        auto builtins = Vec<BuiltinPackageSourceEntry>::with_capacity(builtin_packages.len());
        for (const auto& package : builtin_packages) {
            auto source = package.source.is_Registry()
                              ? BuiltinPackageSource::Registry(
                                    package.source.as_Registry().registry.clone(),
                                    package.source.as_Registry().requirement.clone())
                          : package.source.is_Git()
                              ? BuiltinPackageSource::Git(package.source.as_Git().url.clone(),
                                                          package.source.as_Git().reference.clone())
                              : BuiltinPackageSource::Path(package.source.as_Path().path.clone());
            builtins.push(BuiltinPackageSourceEntry {
                .id     = package.id.clone(),
                .source = rstd::move(source),
            });
        }
        auto bundles = Vec<PathBuf>::with_capacity(source_bundles.len());
        for (const auto& bundle : source_bundles) bundles.push(bundle.clone());
        return PackageSourceConfig {
            .patches          = rstd::move(copied),
            .builtin_packages = rstd::move(builtins),
            .source_bundles   = rstd::move(bundles),
            .network          = network,
        };
    }
};

} // namespace lito::source
