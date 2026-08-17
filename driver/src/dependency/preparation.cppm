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
              (ArchitectureArchives, (Vec<lito::dependency::CMakeArchiveVariant> variants;)))
};

struct PreparedCMakeDependencyRequirement {
    String                             alias;
    String                             package;
    PreparedCMakeDependencySource      source;
    bool                               installed_override { false };
    lito::dependency::CMakeIntegration integration { lito::dependency::CMakeIntegration::Install };
    bool                               add_subdirectory { true };
    Option<PathBuf>                    adapter;
    String                             adapter_identity;
    Option<PathBuf>                    config_directory;
    Vec<lito::dependency::CMakeCacheEntry>        cache;
    Vec<lito::dependency::CMakeTargetRequirement> targets;
};

struct PreparedExternalDependency {
    usize                              package {};
    PreparedCMakeDependencyRequirement requirement;
};

struct PreparedExternalDependencySources {
    Vec<PreparedExternalDependency> dependencies;
};

struct DeclaredExternalDependencySources {
    lito::source::SourceResolutionOptions                             options;
    rstd::collections::BTreeMap<String, lito::source::AcquiredSource> package_owned;
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
