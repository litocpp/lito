export module lito.core:registry.cache;

import rstd;
import licrypto;
import :registry.identity;
import :registry.release;

using namespace rstd::prelude;
using namespace rstd::literals;
using PathBuf = rstd::path::PathBuf;

export namespace lito::registry
{

inline constexpr auto REGISTRY_CHECKSUM_PREFIX_LENGTH    = usize(16);
inline constexpr auto REGISTRY_CACHE_COMPONENT_MAX_BYTES = usize(240);

auto registry_key(const RegistryId& registry) -> String {
    return licrypto::sha256_hex(registry.as_str());
}

auto registry_archive_stem(const RegistryReleasePin& pin) -> String {
    auto version  = pin.release.version.text();
    auto checksum = pin.checksum.text();
    checksum.truncate(REGISTRY_CHECKSUM_PREFIX_LENGTH);

    const auto fixed =
        pin.release.package.name.as_str().len() + usize(1) + usize(1) + checksum.len();
    const auto version_limit = REGISTRY_CACHE_COMPONENT_MAX_BYTES - ".tar.zst"_str.len() - fixed;
    if (version.len() > version_limit) version.truncate(version_limit);

    return rstd::format(
        "{}-{}-{}", pin.release.package.name.as_str(), version.as_str(), checksum.as_str());
}

auto registry_archive_filename(const RegistryReleasePin& pin) -> String {
    return rstd::format("{}.tar.zst", registry_archive_stem(pin).as_str());
}

class RegistryCacheLayout {
    PathBuf root_;

    auto registry_root() const -> PathBuf {
        return root_.join(PathBuf::from("registry"_str).as_path());
    }

public:
    explicit RegistryCacheLayout(PathBuf root): root_(rstd::move(root)) {}

    auto index(const RegistryPackageId& package) const -> PathBuf {
        return registry_root()
            .join(PathBuf::from("index"_str).as_path())
            .join(PathBuf::from(registry_key(package.registry)).as_path())
            .join(PathBuf::from(rstd::format("{}.json", package.name.as_str())).as_path());
    }

    auto index_lock(const RegistryPackageId& package) const -> PathBuf {
        return registry_root()
            .join(PathBuf::from("index"_str).as_path())
            .join(PathBuf::from(registry_key(package.registry)).as_path())
            .join(PathBuf::from(rstd::format("{}.lock", package.name.as_str())).as_path());
    }

    auto archive(const RegistryReleasePin& pin) const -> PathBuf {
        return registry_root()
            .join(PathBuf::from("source"_str).as_path())
            .join(PathBuf::from(registry_archive_filename(pin)).as_path());
    }

    auto tree(const RegistryReleasePin& pin) const -> PathBuf {
        return registry_root()
            .join(PathBuf::from("tree"_str).as_path())
            .join(PathBuf::from(registry_archive_stem(pin)).as_path());
    }

    auto tree_marker(const RegistryReleasePin& pin) const -> PathBuf {
        return registry_root()
            .join(PathBuf::from("tree"_str).as_path())
            .join(PathBuf::from(rstd::format("{}.ok", registry_archive_stem(pin).as_str()))
                      .as_path());
    }

    auto release_lock(const RegistryReleasePin& pin) const -> PathBuf {
        return registry_root()
            .join(PathBuf::from("lock"_str).as_path())
            .join(PathBuf::from(rstd::format("{}.lock", registry_archive_stem(pin).as_str()))
                      .as_path());
    }
};

} // namespace lito::registry
