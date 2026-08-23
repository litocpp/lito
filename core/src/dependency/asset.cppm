export module lito.core:dependency.asset;

import rstd;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;

export namespace lito::dependency
{

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

} // namespace lito::dependency
