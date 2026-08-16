module;
#include <rstd/enum.hpp>

export module lito.driver:dependency.preparation;

import rstd;
import lito.core;
import lito.toolchain.cmake;

using namespace rstd::prelude;

export namespace lito
{

class PreparedCMakeDependencySource {
    RSTD_ENUM(PreparedCMakeDependencySource,
              (Installed),
              (Directory, (PathBuf root; String identity; bool cacheable;)),
              (Archive, (String url; String sha256;)),
              (ArchitectureArchives, (Vec<CMakeArchiveVariant> variants;)))
};

struct PreparedCMakeDependencyRequirement {
    String                        alias;
    String                        package;
    PreparedCMakeDependencySource source;
    CMakeIntegration              integration { CMakeIntegration::Install };
    bool                          add_subdirectory { true };
    Option<PathBuf>               adapter;
    String                        adapter_identity;
    Option<PathBuf>               config_directory;
    Vec<CMakeCacheEntry>          cache;
    Vec<CMakeTargetRequirement>   targets;
};

struct PreparedExternalDependency {
    usize                              package {};
    PreparedCMakeDependencyRequirement requirement;
};

struct PreparedExternalDependencySources {
    Vec<PreparedExternalDependency> dependencies;
};

struct ExternalAssetCatalog {
    Vec<ExternalAssetSet> sets;

    auto clone() const -> ExternalAssetCatalog {
        auto copied = Vec<ExternalAssetSet>::with_capacity(sets.len());
        for (const auto& set : sets) copied.push(set.clone());
        return ExternalAssetCatalog { .sets = rstd::move(copied) };
    }
};

} // namespace lito
