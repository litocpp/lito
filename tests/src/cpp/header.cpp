#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito.core;
import lito.cpp;

using namespace rstd::prelude;
using namespace rstd::literals;
using PathBuf = rstd::path::PathBuf;

namespace
{

auto target(ref<str> name) -> lito::package::PackageTargetId {
    return lito::package::PackageTargetId {
        .package = String::make("fixture"_str),
        .name    = String::make(name),
    };
}

auto root(ref<rstd::path::Path> path, lito::cpp::HeaderOwner owner, lito::cpp::HeaderAccess access)
    -> lito::cpp::ResolvedHeaderRoot {
    auto canonical = rstd::fs::canonicalize(path);
    EXPECT_TRUE(canonical.is_ok());
    return lito::cpp::ResolvedHeaderRoot {
        .root       = canonical.is_ok() ? rstd::move(canonical).unwrap() : PathBuf::from(path),
        .owner      = rstd::move(owner),
        .access     = rstd::move(access),
        .provenance = String::make("test"_str),
    };
}

} // namespace

TEST(HeaderOwnership, LongestRootControlsPrivateRetention) {
    auto temporary = rstd::test::TempDir::make();
    ASSERT_TRUE(temporary.is_ok());
    auto owner = rstd::move(temporary).unwrap();
    auto private_directory =
        PathBuf::from(owner.path()).join(PathBuf::from("private"_str).as_path());
    ASSERT_TRUE(rstd::fs::create_dir(private_directory.as_path()).is_ok());
    auto header = private_directory.join(PathBuf::from("value.hpp"_str).as_path());
    ASSERT_TRUE(rstd::fs::write(header.as_path(), "#pragma once\n"_str.as_bytes()).is_ok());
    auto roots = Vec<lito::cpp::ResolvedHeaderRoot>::make();
    roots.push(root(owner.path(),
                    lito::cpp::HeaderOwner::ProjectPackage(String::make("package"_str)),
                    lito::cpp::HeaderAccess::Public()));
    auto private_target = target("library"_str);
    roots.push(root(private_directory.as_path(),
                    lito::cpp::HeaderOwner::ProjectPackage(String::make("package"_str)),
                    lito::cpp::HeaderAccess::TargetPrivate(private_target.clone())));
    auto index          = lito::cpp::HeaderOwnershipIndex::make(rstd::move(roots));
    auto classification = index.classify(header.as_path());

    ASSERT_TRUE(classification.owner.is_ProjectPackage());
    ASSERT_TRUE(classification.access.is_TargetPrivate());
    EXPECT_EQ(classification.access.as_TargetPrivate().target, private_target);
    auto domain = lito::cpp::header_retention_domain(classification);
    ASSERT_TRUE(domain.is_some());
    EXPECT_TRUE(domain->as_str().starts_with("target:"_str));
}

TEST(HeaderOwnership, EqualOwnerClaimsBecomePublic) {
    auto temporary = rstd::test::TempDir::make();
    ASSERT_TRUE(temporary.is_ok());
    auto owner  = rstd::move(temporary).unwrap();
    auto header = PathBuf::from(owner.path()).join(PathBuf::from("value.hpp"_str).as_path());
    ASSERT_TRUE(rstd::fs::write(header.as_path(), "#pragma once\n"_str.as_bytes()).is_ok());
    auto roots = Vec<lito::cpp::ResolvedHeaderRoot>::make();
    roots.push(root(owner.path(),
                    lito::cpp::HeaderOwner::ProjectPackage(String::make("package"_str)),
                    lito::cpp::HeaderAccess::TargetPrivate(target("library"_str))));
    roots.push(root(owner.path(),
                    lito::cpp::HeaderOwner::ProjectPackage(String::make("package"_str)),
                    lito::cpp::HeaderAccess::Public()));
    auto index          = lito::cpp::HeaderOwnershipIndex::make(rstd::move(roots));
    auto classification = index.classify(header.as_path());

    EXPECT_TRUE(classification.owner.is_ProjectPackage());
    EXPECT_TRUE(classification.access.is_Public());
}

TEST(HeaderOwnership, DuplicateGlobalClaimsRemainGlobal) {
    auto temporary = rstd::test::TempDir::make();
    ASSERT_TRUE(temporary.is_ok());
    auto owner  = rstd::move(temporary).unwrap();
    auto header = PathBuf::from(owner.path()).join(PathBuf::from("value.hpp"_str).as_path());
    ASSERT_TRUE(rstd::fs::write(header.as_path(), "#pragma once\n"_str.as_bytes()).is_ok());
    auto roots = Vec<lito::cpp::ResolvedHeaderRoot>::make();
    roots.push(root(owner.path(),
                    lito::cpp::HeaderOwner::Toolchain(String::make("clang"_str)),
                    lito::cpp::HeaderAccess::Global()));
    roots.push(root(owner.path(),
                    lito::cpp::HeaderOwner::Toolchain(String::make("clang"_str)),
                    lito::cpp::HeaderAccess::Global()));
    auto index          = lito::cpp::HeaderOwnershipIndex::make(rstd::move(roots));
    auto classification = index.classify(header.as_path());

    EXPECT_TRUE(classification.owner.is_Toolchain());
    EXPECT_TRUE(classification.access.is_Global());
}

TEST(HeaderOwnership, ConflictingOwnersAreExplicitlyAmbiguous) {
    auto temporary = rstd::test::TempDir::make();
    ASSERT_TRUE(temporary.is_ok());
    auto owner  = rstd::move(temporary).unwrap();
    auto header = PathBuf::from(owner.path()).join(PathBuf::from("value.hpp"_str).as_path());
    ASSERT_TRUE(rstd::fs::write(header.as_path(), "#pragma once\n"_str.as_bytes()).is_ok());
    auto roots = Vec<lito::cpp::ResolvedHeaderRoot>::make();
    roots.push(root(owner.path(),
                    lito::cpp::HeaderOwner::ProjectPackage(String::make("package"_str)),
                    lito::cpp::HeaderAccess::Public()));
    roots.push(root(owner.path(),
                    lito::cpp::HeaderOwner::ExternalTarget(String::make("external"_str)),
                    lito::cpp::HeaderAccess::Public()));
    auto index          = lito::cpp::HeaderOwnershipIndex::make(rstd::move(roots));
    auto classification = index.classify(header.as_path());

    EXPECT_TRUE(classification.owner.is_Ambiguous());
    EXPECT_TRUE(classification.access.is_Global());
    EXPECT_TRUE(lito::cpp::header_retention_domain(classification).is_none());
}

TEST(HeaderOwnership, PhysicalClassificationConflictIsOrderIndependent) {
    auto left = lito::cpp::HeaderClassification {
        .owner  = lito::cpp::HeaderOwner::Unknown(),
        .access = lito::cpp::HeaderAccess::Global(),
    };
    auto right = lito::cpp::HeaderClassification {
        .owner  = lito::cpp::HeaderOwner::Toolchain(String::make("clang"_str)),
        .access = lito::cpp::HeaderAccess::Global(),
    };
    auto reversed_left  = as<Clone>(right).clone();
    auto reversed_right = as<Clone>(left).clone();

    lito::cpp::merge_header_classification(left, right);
    lito::cpp::merge_header_classification(reversed_left, reversed_right);

    EXPECT_TRUE(left.owner.is_Ambiguous());
    EXPECT_TRUE(reversed_left.owner.is_Ambiguous());
    EXPECT_TRUE(left.access.is_Global());
    EXPECT_TRUE(reversed_left.access.is_Global());
}
