export module lito.core:source.bundle;

import rstd;
import lito.crypto;
import :source.fetch;
import :source.error;
import :registry.digest;
import :registry.identity;
import :registry.version;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;
using namespace rstd::literals;

export namespace lito::source
{

inline constexpr auto SOURCE_BUNDLE_FORMAT_VERSION = u64(1);

class SourceBundleLayout {
    PathBuf root_;

    auto version_root() const -> PathBuf {
        return root_.join(
            PathBuf::from(rstd::format("v{}", SOURCE_BUNDLE_FORMAT_VERSION)).as_path());
    }

public:
    explicit SourceBundleLayout(PathBuf root): root_(rstd::move(root)) {}

    auto root() const noexcept -> ref<rstd::path::Path> { return root_.as_path(); }

    auto git(const FetchIdentity& identity) const -> PathBuf {
        auto key = fetch_identity_stable_key(identity);
        return version_root()
            .join(PathBuf::from("git"_str).as_path())
            .join(PathBuf::from(key.as_str()).as_path());
    }

    auto archive(const FetchIdentity& identity) const -> PathBuf {
        auto key = fetch_identity_stable_key(identity);
        return version_root()
            .join(PathBuf::from("archives"_str).as_path())
            .join(PathBuf::from(key.as_str()).as_path())
            .join(PathBuf::from("source.archive"_str).as_path());
    }

    auto registry_package(const lito::registry::PackageChecksum& checksum) const -> PathBuf {
        return version_root()
            .join(PathBuf::from("registry"_str).as_path())
            .join(PathBuf::from("packages"_str).as_path())
            .join(PathBuf::from(checksum.text().as_str()).as_path())
            .join(PathBuf::from("source.archive"_str).as_path());
    }

    auto cargo(ref<str> source_identity, ref<rstd::path::Path> manifest, ref<str> target) const
        -> PathBuf {
        auto identity = rstd::format(
            "lito-source-bundle-cargo-v1\n{}\n{}\n{}", source_identity, manifest, target);
        auto key = lito::crypto::sha256_hex(identity.as_str());
        return version_root()
            .join(PathBuf::from("cargo"_str).as_path())
            .join(PathBuf::from(key.as_str()).as_path());
    }

    auto cargo_config(ref<str>              source_identity,
                      ref<rstd::path::Path> manifest,
                      ref<str>              target) const -> PathBuf {
        return cargo(source_identity, manifest, target)
            .join(PathBuf::from("config.toml"_str).as_path());
    }

    auto fetch(const FetchIdentity& identity) const -> PathBuf {
        if (identity.is_Git()) return git(identity);
        if (identity.is_Archive()) return archive(identity);
        return registry_package(identity.as_RegistryPackage().checksum);
    }
};

auto locate_source_bundle(const Vec<PathBuf>& roots, const FetchIdentity& identity)
    -> SourceResult<Option<PathBuf>>;

} // namespace lito::source

export namespace lito::source
{

auto locate_source_bundle(const Vec<PathBuf>& roots, const FetchIdentity& identity)
    -> SourceResult<Option<PathBuf>> {
    for (const auto& root : roots) {
        auto path   = SourceBundleLayout(root.clone()).fetch(identity);
        auto exists = rstd::fs::exists(path.as_path());
        if (exists.is_err()) {
            return source_io_failure<Option<PathBuf>>(
                "inspect source bundle entry"_str, path.as_path(), rstd::move(exists).unwrap_err());
        }
        if (*exists) return Ok(Some(rstd::move(path)));
    }
    return Ok(None());
}

} // namespace lito::source
