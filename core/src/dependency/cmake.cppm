module;
#include <rstd/enum.hpp>

export module lito.core:dependency.cmake;

import rstd;
import :dependency.visibility;
import lito.system;
import :source.git;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;
using namespace lito::system;

export namespace lito
{

struct CMakeCacheEntry {
    String name;
    String value;
};

struct CMakeArchiveVariant {
    Architecture architecture;
    String       url;
    String       sha256;
};

class CMakeDependencySource {
    RSTD_ENUM(CMakeDependencySource,
              (Installed),
              (Path, (PathBuf path;)),
              (Git, (String url; GitReference reference;)),
              (Archive, (String url; String sha256;)),
              (ArchitectureArchives, (Vec<CMakeArchiveVariant> variants;)))

public:
    auto clone() const -> CMakeDependencySource {
        if (is_Path()) return CMakeDependencySource::Path(as_Path().path.clone());
        if (is_Git()) {
            return CMakeDependencySource::Git(as_Git().url.clone(),
                                              GitReference {
                                                  .kind  = as_Git().reference.kind,
                                                  .value = as_Git().reference.value.clone(),
                                              });
        }
        if (is_Archive()) {
            return CMakeDependencySource::Archive(as_Archive().url.clone(),
                                                  as_Archive().sha256.clone());
        }
        if (is_ArchitectureArchives()) {
            auto variants =
                Vec<CMakeArchiveVariant>::with_capacity(as_ArchitectureArchives().variants.len());
            for (const auto& variant : as_ArchitectureArchives().variants) {
                variants.push(CMakeArchiveVariant {
                    .architecture = variant.architecture.clone(),
                    .url          = variant.url.clone(),
                    .sha256       = variant.sha256.clone(),
                });
            }
            return CMakeDependencySource::ArchitectureArchives(rstd::move(variants));
        }
        return CMakeDependencySource::Installed();
    }
};

enum class CMakeIntegration
{
    Install,
    BuildTree,
};

struct CMakeTargetRequirement {
    String               name;
    DependencyVisibility visibility { DependencyVisibility::Private };
};

struct CMakeDependencyRequirement {
    String                      alias;
    String                      package;
    CMakeDependencySource       source;
    CMakeIntegration            integration { CMakeIntegration::Install };
    Option<PathBuf>             adapter;
    Option<PathBuf>             config_directory;
    Vec<CMakeCacheEntry>        cache;
    Vec<CMakeTargetRequirement> targets;
    Option<PathBuf>             declaration_root;
    Option<PathBuf>             adapter_root;

    auto clone() const -> CMakeDependencyRequirement {
        auto cache_copy = Vec<CMakeCacheEntry>::with_capacity(cache.len());
        for (const auto& entry : cache) {
            cache_copy.push(CMakeCacheEntry {
                .name  = entry.name.clone(),
                .value = entry.value.clone(),
            });
        }
        auto target_copy = Vec<CMakeTargetRequirement>::with_capacity(targets.len());
        for (const auto& target : targets) {
            target_copy.push(CMakeTargetRequirement {
                .name       = target.name.clone(),
                .visibility = target.visibility,
            });
        }
        auto result = CMakeDependencyRequirement {
            .alias       = alias.clone(),
            .package     = package.clone(),
            .source      = source.clone(),
            .integration = integration,
            .cache       = rstd::move(cache_copy),
            .targets     = rstd::move(target_copy),
        };
        if (adapter.is_some()) result.adapter = Some(adapter->clone());
        if (config_directory.is_some()) result.config_directory = Some(config_directory->clone());
        if (declaration_root.is_some()) result.declaration_root = Some(declaration_root->clone());
        if (adapter_root.is_some()) result.adapter_root = Some(adapter_root->clone());
        return result;
    }
};

struct CMakeProviderConfig {
    PathBuf      executable;
    String       generator;
    String       identity;
    Vec<PathBuf> search_paths;

    auto clone() const -> CMakeProviderConfig {
        return CMakeProviderConfig {
            .executable   = executable.clone(),
            .generator    = generator.clone(),
            .identity     = identity.clone(),
            .search_paths = as<Clone>(search_paths).clone(),
        };
    }
};

} // namespace lito
