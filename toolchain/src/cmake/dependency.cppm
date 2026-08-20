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
              (Find),
              (Directory, (PathBuf root; String identity; bool cacheable;)))
};

struct ResolvedCMakeDependencyRequirement {
    String                                        alias;
    String                                        package;
    Vec<String>                                   components;
    ResolvedCMakeDependencySource                 source;
    Option<PathBuf>                               adapter;
    String                                        adapter_identity;
    Option<PathBuf>                               config_directory;
    Vec<lito::dependency::CMakeCacheEntry>        cache;
    Vec<lito::dependency::CMakeTargetRequirement> targets;
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

enum class ExternalAssetDisposition
{
    Materialized,
    Provided,
};

struct ExternalAssetSet {
    String                   alias;
    String                   name;
    ExternalAssetDisposition disposition { ExternalAssetDisposition::Materialized };
    Vec<ExternalAssetEntry>  entries;

    auto clone() const -> ExternalAssetSet {
        auto copied = Vec<ExternalAssetEntry>::with_capacity(entries.len());
        for (const auto& entry : entries) copied.push(entry.clone());
        return ExternalAssetSet {
            .alias       = alias.clone(),
            .name        = name.clone(),
            .disposition = disposition,
            .entries     = rstd::move(copied),
        };
    }
};

} // namespace lito
