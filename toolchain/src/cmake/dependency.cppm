module;
#include <rstd/enum.hpp>

export module lito.toolchain.cmake:dependency;

import rstd;
import lito.core;

using namespace rstd::prelude;

export namespace lito
{

class ResolvedCMakeDependencySource {
    RSTD_ENUM(ResolvedCMakeDependencySource,
              (Installed),
              (Directory, (PathBuf root; String identity; bool add_subdirectory; bool cacheable;)),
              (Archive, (String url; String sha256;)))
};

struct ResolvedCMakeDependencyRequirement {
    String                        alias;
    String                        package;
    ResolvedCMakeDependencySource source;
    CMakeIntegration              integration { CMakeIntegration::Install };
    bool                          add_subdirectory { true };
    Option<PathBuf>               adapter;
    String                        adapter_identity;
    Option<PathBuf>               config_directory;
    Vec<CMakeCacheEntry>          cache;
    Vec<CMakeTargetRequirement>   targets;
};

struct ExternalAssetEntry {
    PathBuf logical_path;
    PathBuf source;

    auto clone() const -> ExternalAssetEntry {
        return ExternalAssetEntry {
            .logical_path = logical_path.clone(),
            .source       = source.clone(),
        };
    }
};

struct ExternalAssetSet {
    String                  alias;
    String                  name;
    Vec<ExternalAssetEntry> entries;

    auto clone() const -> ExternalAssetSet {
        auto copied = Vec<ExternalAssetEntry>::with_capacity(entries.len());
        for (const auto& entry : entries) copied.push(entry.clone());
        return ExternalAssetSet {
            .alias   = alias.clone(),
            .name    = name.clone(),
            .entries = rstd::move(copied),
        };
    }
};

} // namespace lito
