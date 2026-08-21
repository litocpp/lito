module;
#include <rstd/enum.hpp>

export module lito.core:dependency.source;

import rstd;
import lito.system;
import :source.git;
import :parse;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;
using namespace lito::system;

export namespace lito::dependency
{

struct ExternalArchiveVariant {
    Architecture               architecture;
    lito::parse::FetchUrl      url;
    rstd::crypto::Sha256Digest sha256;
};

class ExternalSourceRequirement {
    RSTD_ENUM(ExternalSourceRequirement,
              (Path, (PathBuf path;)),
              (Git, (String url; lito::source::GitReference reference;)),
              (Archive, (lito::parse::FetchUrl url; rstd::crypto::Sha256Digest sha256;)),
              (ArchitectureArchives, (Vec<ExternalArchiveVariant> variants;)))

public:
    auto clone() const -> ExternalSourceRequirement {
        if (is_Path()) return ExternalSourceRequirement::Path(as_Path().path.clone());
        if (is_Git()) {
            return ExternalSourceRequirement::Git(as_Git().url.clone(), as_Git().reference.clone());
        }
        if (is_Archive()) {
            return ExternalSourceRequirement::Archive(as_Archive().url.clone(),
                                                      as_Archive().sha256.clone());
        }
        auto variants =
            Vec<ExternalArchiveVariant>::with_capacity(as_ArchitectureArchives().variants.len());
        for (const auto& variant : as_ArchitectureArchives().variants) {
            variants.push(ExternalArchiveVariant {
                .architecture = variant.architecture.clone(),
                .url          = variant.url.clone(),
                .sha256       = variant.sha256.clone(),
            });
        }
        return ExternalSourceRequirement::ArchitectureArchives(rstd::move(variants));
    }
};

class ResolvedExternalSource {
    RSTD_ENUM(ResolvedExternalSource,
              (Path, (PathBuf path;)),
              (Package, (PathBuf path;)),
              (Git, (String url; lito::source::GitReference reference; String commit;)),
              (Archive, (lito::parse::FetchUrl url; rstd::crypto::Sha256Digest sha256;)))
};

struct ResolvedExternalSourceRecord {
    String                 name;
    Vec<Architecture>      architectures;
    ResolvedExternalSource source;
};

} // namespace lito::dependency
