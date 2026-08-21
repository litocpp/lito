module;
#include <rstd/enum.hpp>

export module lito.driver:dependency.preparation;

import rstd;
import lito.core;
import :dependency.cmake;
import :source.acquisition;

using namespace rstd::prelude;

export namespace lito
{

class PreparedCMakeDependencySource {
    RSTD_ENUM(PreparedCMakeDependencySource,
              (Find),
              (Directory, (PathBuf root; String identity; bool cacheable;)),
              (Archive, (lito::parse::FetchUrl url; rstd::crypto::Sha256Digest sha256;)),
              (ArchitectureArchives, (Vec<lito::dependency::ExternalArchiveVariant> variants;)))
};

struct PreparedCMakeDependencyRequirement {
    String                                          alias;
    String                                          package;
    Vec<String>                                     components;
    Option<String>                                  source_name;
    PreparedCMakeDependencySource                   source;
    Option<PathBuf>                                 adapter;
    String                                          adapter_identity;
    Option<PathBuf>                                 config_directory;
    Vec<lito::dependency::CMakeCacheEntry>          cache;
    Vec<lito::dependency::CMakeTargetRequirement>   targets;
    Vec<lito::dependency::CMakeHostToolRequirement> host_tools;
};

struct PreparedExternalDependency {
    usize                              package {};
    bool                               installed_override { false };
    PreparedCMakeDependencyRequirement requirement;
};

struct PreparedPackageExternalSource {
    usize                                       package {};
    String                                      name;
    lito::dependency::ExternalSourceRequirement source;
    Option<lito::source::AcquiredSource>        acquired;
};

struct PreparedExternalDependencySources {
    Vec<PreparedExternalDependency>    dependencies;
    Vec<PreparedPackageExternalSource> sources;
};

struct ExternalSourceProvenance {
    String  package;
    String  name;
    PathBuf materialized_root;
    String  stable_source_identity;

    auto clone() const -> ExternalSourceProvenance {
        return ExternalSourceProvenance {
            .package                = package.clone(),
            .name                   = name.clone(),
            .materialized_root      = materialized_root.clone(),
            .stable_source_identity = stable_source_identity.clone(),
        };
    }
};

class SelectedCMakeDependencySource {
    RSTD_ENUM(SelectedCMakeDependencySource,
              (Find),
              (Directory, (PathBuf root; String identity; bool cacheable;)),
              (Archive, (lito::parse::FetchUrl url; rstd::crypto::Sha256Digest sha256;)))
};

struct SelectedCMakeDependencyRequirement {
    String                                          alias;
    String                                          package;
    Vec<String>                                     components;
    SelectedCMakeDependencySource                   source;
    Option<PathBuf>                                 adapter;
    String                                          adapter_identity;
    Option<PathBuf>                                 config_directory;
    Vec<lito::dependency::CMakeCacheEntry>          cache;
    Vec<lito::dependency::CMakeTargetRequirement>   targets;
    Vec<lito::dependency::CMakeHostToolRequirement> host_tools;
};

struct DeclaredExternalDependencySources {
    lito::source::SourceResolutionOptions                             options;
    rstd::collections::BTreeMap<String, lito::source::AcquiredSource> package_owned;
};

struct ExternalAssetCatalog {
    Vec<ExternalAssetSet> sets;

    auto resolve(ref<str> dependency, ref<str> name) const
        -> Result<const ExternalAssetSet*, String> {
        const ExternalAssetSet* result = nullptr;
        for (const auto& set : sets) {
            if (set.alias != dependency || set.name != name) continue;
            if (result != nullptr) {
                return Err(
                    rstd::format("external asset set '{}:{}' is ambiguous", dependency, name));
            }
            result = rstd::addressof(set);
        }
        if (result == nullptr) {
            return Err(rstd::format("external asset set '{}:{}' is unavailable", dependency, name));
        }
        return Ok(result);
    }

    auto clone() const -> ExternalAssetCatalog {
        auto copied = Vec<ExternalAssetSet>::with_capacity(sets.len());
        for (const auto& set : sets) copied.push(set.clone());
        return ExternalAssetCatalog { .sets = rstd::move(copied) };
    }
};

} // namespace lito
