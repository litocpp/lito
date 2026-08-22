module;
#include <rstd/enum.hpp>

export module lito.core:package.identity;

import rstd;
import lito.crypto;
import :registry.digest;
import :registry.identity;
import :registry.version;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::package
{

class ResolvedPackageInstanceId {
    RSTD_ENUM(ResolvedPackageInstanceId,
              (Path, (rstd::path::PathBuf path; String package;)),
              (Git, (String url; String commit; String package;)),
              (Builtin, (String id; String digest; String package;)),
              (Registry,
               (lito::registry::RegistryPackageId package; lito::registry::SemanticVersion version;
                lito::registry::ReleaseDigest release;)))
};

auto clone_package_instance_id(const ResolvedPackageInstanceId& id) -> ResolvedPackageInstanceId;
auto package_instance_id_text(const ResolvedPackageInstanceId& id) -> String;

class PackageInstanceKey : public DefaultInClass<PackageInstanceKey, Clone> {
    String value_;

public:
    PackageInstanceKey() = default;
    explicit PackageInstanceKey(String value): value_(rstd::move(value)) {}

    static auto from(const ResolvedPackageInstanceId& id) -> PackageInstanceKey {
        auto text = package_instance_id_text(id);
        return PackageInstanceKey(
            rstd::format("lito-pkg-{}", lito::crypto::sha256_hex(text.as_str())));
    }

    auto as_str() const noexcept -> ref<str> { return value_.as_str(); }
    auto is_empty() const noexcept -> bool { return value_.is_empty(); }
    auto clone() const -> PackageInstanceKey { return PackageInstanceKey(value_.clone()); }

    friend auto operator==(const PackageInstanceKey& left, const PackageInstanceKey& right) noexcept
        -> bool {
        return left.value_ == right.value_;
    }
};

enum class PackageTargetKind
{
    Library,
    Binary,
    Test,
    Benchmark,
    TestAttachment,
    CompileTest,
};

auto package_target_kind_name(PackageTargetKind kind) noexcept -> ref<str> {
    switch (kind) {
    case PackageTargetKind::Library: return "lib"_str;
    case PackageTargetKind::Binary: return "bin"_str;
    case PackageTargetKind::Test: return "test"_str;
    case PackageTargetKind::Benchmark: return "bench"_str;
    case PackageTargetKind::TestAttachment: return "test-attachment"_str;
    case PackageTargetKind::CompileTest: return "compile-test"_str;
    }
    return "unknown"_str;
}

struct PackageTargetId {
    PackageInstanceKey package_instance;
    String             package;
    PackageTargetKind  kind { PackageTargetKind::Library };
    String             name;

    auto clone() const -> PackageTargetId {
        return PackageTargetId {
            .package_instance = package_instance.clone(),
            .package          = package.clone(),
            .kind             = kind,
            .name             = name.clone(),
        };
    }

    auto operator==(const PackageTargetId& other) const noexcept -> bool {
        return package_instance == other.package_instance && package == other.package &&
               kind == other.kind && name == other.name;
    }
};

auto package_target_id_text(const PackageTargetId& id) -> String {
    if (! id.package_instance.is_empty()) {
        return rstd::format("{}::{}::{}",
                            id.package_instance.as_str(),
                            package_target_kind_name(id.kind),
                            id.name.as_str());
    }
    return rstd::format(
        "{}::{}::{}", id.package.as_str(), package_target_kind_name(id.kind), id.name.as_str());
}

auto package_target_matches_selector(const PackageTargetId& target,
                                     const PackageTargetId& selector) noexcept -> bool {
    if (! selector.package_instance.is_empty() &&
        target.package_instance != selector.package_instance) {
        return false;
    }
    return target.package == selector.package && target.kind == selector.kind &&
           target.name == selector.name;
}

auto clone_package_instance_id(const ResolvedPackageInstanceId& id) -> ResolvedPackageInstanceId {
    if (id.is_Path()) {
        return ResolvedPackageInstanceId::Path(id.as_Path().path.clone(),
                                               id.as_Path().package.clone());
    }
    if (id.is_Git()) {
        return ResolvedPackageInstanceId::Git(
            id.as_Git().url.clone(), id.as_Git().commit.clone(), id.as_Git().package.clone());
    }
    if (id.is_Builtin()) {
        return ResolvedPackageInstanceId::Builtin(id.as_Builtin().id.clone(),
                                                  id.as_Builtin().digest.clone(),
                                                  id.as_Builtin().package.clone());
    }
    return ResolvedPackageInstanceId::Registry(id.as_Registry().package.clone(),
                                               id.as_Registry().version.clone(),
                                               id.as_Registry().release.clone());
}

auto package_instance_id_text(const ResolvedPackageInstanceId& id) -> String {
    if (id.is_Path()) {
        return rstd::format("lito-package-instance-v1\npath\n{}\n{}",
                            id.as_Path().path.as_path(),
                            id.as_Path().package.as_str());
    }
    if (id.is_Git()) {
        return rstd::format("lito-package-instance-v1\ngit\n{}\n{}\n{}",
                            id.as_Git().url.as_str(),
                            id.as_Git().commit.as_str(),
                            id.as_Git().package.as_str());
    }
    if (id.is_Builtin()) {
        return rstd::format("lito-package-instance-v1\nbuiltin\n{}\n{}\n{}",
                            id.as_Builtin().id.as_str(),
                            id.as_Builtin().digest.as_str(),
                            id.as_Builtin().package.as_str());
    }
    return rstd::format("lito-package-instance-v1\nregistry\n{}\n{}\n{}\n{}",
                        id.as_Registry().package.registry.as_str(),
                        id.as_Registry().package.name.as_str(),
                        id.as_Registry().version.text(),
                        id.as_Registry().release.text());
}

} // namespace lito::package
