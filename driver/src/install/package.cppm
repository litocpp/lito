module;
#include <rstd/enum.hpp>

export module lito.driver:install.package;

import rstd;
import lito.core;

using namespace rstd::prelude;

export namespace lito
{

struct InstallRuntimeDependency {
    String name;
    String source_identity;
};

struct InstallPackageIdentity {
    String id;
    String name;
    String source_identity;

    auto clone() const -> InstallPackageIdentity {
        return InstallPackageIdentity {
            .id              = id.clone(),
            .name            = name.clone(),
            .source_identity = source_identity.clone(),
        };
    }
};

class InstallSourceProvenance {
    RSTD_ENUM(InstallSourceProvenance,
              (Local, (PathBuf root; String identity;)),
              (Git,
               (String url; lito::source::GitReference reference; String commit; String identity;)),
              (Registry,
               (lito::registry::RegistryPackageId package; lito::registry::SemanticVersion version;
                lito::registry::ReleaseDigest                                              release;
                lito::registry::SourceDigest                                               source;
                lito::registry::ManifestDigest                                             manifest;
                lito::registry::BlobDigest                                                 blob;
                lito::registry::RegistryBlobSize      blob_size;
                lito::registry::RegistryArchiveFormat archive_format;
                String                                identity;)))

public:
    auto clone() const -> InstallSourceProvenance {
        if (is_Local()) {
            return InstallSourceProvenance::Local(as_Local().root.clone(),
                                                  as_Local().identity.clone());
        }
        if (is_Git()) {
            return InstallSourceProvenance::Git(as_Git().url.clone(),
                                                as_Git().reference.clone(),
                                                as_Git().commit.clone(),
                                                as_Git().identity.clone());
        }
        return InstallSourceProvenance::Registry(as_Registry().package.clone(),
                                                 as_Registry().version.clone(),
                                                 as_Registry().release.clone(),
                                                 as_Registry().source.clone(),
                                                 as_Registry().manifest.clone(),
                                                 as_Registry().blob.clone(),
                                                 as_Registry().blob_size.clone(),
                                                 as_Registry().archive_format.clone(),
                                                 as_Registry().identity.clone());
    }
};

} // namespace lito
