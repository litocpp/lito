export module lito.core:registry.release;

import :registry.digest;
import :registry.identity;
import :registry.version;

export namespace lito::registry
{

struct RegistryReleaseId {
    RegistryPackageId package;
    SemanticVersion   version;

    auto clone() const -> RegistryReleaseId {
        return RegistryReleaseId {
            .package = package.clone(),
            .version = version.clone(),
        };
    }

    auto operator==(const RegistryReleaseId& other) const noexcept -> bool {
        return package == other.package && version == other.version;
    }
};

struct RegistryReleasePin {
    RegistryReleaseId release;
    PackageChecksum   checksum;

    auto clone() const -> RegistryReleasePin {
        return RegistryReleasePin {
            .release  = release.clone(),
            .checksum = checksum.clone(),
        };
    }

    auto operator==(const RegistryReleasePin& other) const noexcept -> bool {
        return release == other.release && checksum == other.checksum;
    }
};

} // namespace lito::registry
