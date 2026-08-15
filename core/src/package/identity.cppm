export module lito.core:package.identity;

import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito
{

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
    String            package;
    PackageTargetKind kind { PackageTargetKind::Library };
    String            name;

    auto clone() const -> PackageTargetId {
        return PackageTargetId {
            .package = package.clone(),
            .kind    = kind,
            .name    = name.clone(),
        };
    }

    auto operator==(const PackageTargetId& other) const noexcept -> bool {
        return package == other.package && kind == other.kind && name == other.name;
    }
};

auto package_target_id_text(const PackageTargetId& id) -> String {
    return rstd::format(
        "{}::{}::{}", id.package.as_str(), package_target_kind_name(id.kind), id.name.as_str());
}

} // namespace lito
