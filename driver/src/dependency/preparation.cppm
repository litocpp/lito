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
              (Find),
              (Directory, (PathBuf root; String identity; bool cacheable;)),
              (Archive, (String url; String sha256;)),
              (ArchitectureArchives, (Vec<lito::dependency::CMakeArchiveVariant> variants;)))
};

struct PreparedCMakeDependencyRequirement {
    String                                        alias;
    String                                        package;
    PreparedCMakeDependencySource                 source;
    Option<PathBuf>                               adapter;
    String                                        adapter_identity;
    Option<PathBuf>                               config_directory;
    Vec<lito::dependency::CMakeCacheEntry>        cache;
    Vec<lito::dependency::CMakeTargetRequirement> targets;
};

struct PreparedExternalDependency {
    usize                              package {};
    bool                               installed_override { false };
    PreparedCMakeDependencyRequirement requirement;
};

struct PreparedExternalDependencySources {
    Vec<PreparedExternalDependency> dependencies;
};

class SelectedCMakeDependencySource {
    RSTD_ENUM(SelectedCMakeDependencySource,
              (Find),
              (Directory, (PathBuf root; String identity; bool cacheable;)),
              (Archive, (String url; String sha256;)))
};

struct SelectedCMakeDependencyRequirement {
    String                                        alias;
    String                                        package;
    SelectedCMakeDependencySource                 source;
    Option<PathBuf>                               adapter;
    String                                        adapter_identity;
    Option<PathBuf>                               config_directory;
    Vec<lito::dependency::CMakeCacheEntry>        cache;
    Vec<lito::dependency::CMakeTargetRequirement> targets;
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
