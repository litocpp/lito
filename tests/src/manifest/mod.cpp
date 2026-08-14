#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito;
import lito.lock;
import lito.package;
import lito.package.graph_contract;
import lito.workspace.contract;
import lito.workspace.resolver;
import lito.platform;
import lito.dependency;
import lito.dependency.cmake;
import lito.source;
import lito.manifest;
import lito.toolchain;
import lito.build.discovery;
import lito.build.layout;
import lito.system.environment;
import lito.system.process;
import lito.system.storage;
import lito.test.support;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito_test;
using PathBuf = rstd::path::PathBuf;

TEST(Manifest, InvalidManifestDocumentsAreRejectedByManifestOwner) {
    for (const auto path : INVALID_MANIFESTS) {
        auto loaded = lito::load_manifest_document(fixture_path(path).as_path());
        if (loaded.is_ok()) rstd::io::eprintln("unexpected valid manifest: {}", path);
        EXPECT_TRUE(loaded.is_err());
    }
}

TEST(Manifest, ManifestSchemaErrorRetainsFileAndNodeOwnership) {
    auto directory = fixture_path("manifest/discovery-field"_str);
    auto loaded    = lito::load_manifest_document(directory.as_path());
    ASSERT_TRUE(loaded.is_err());
    auto error = rstd::move(loaded).unwrap_err();
    ASSERT_TRUE(error.is_File());
    const auto& file = error.as_File().source;
    EXPECT_EQ(file.path.as_path(),
              directory.join(PathBuf::from("lito.toml"_str).as_path()).as_path());
    ASSERT_TRUE(file.cause.is_Schema());
    const auto& schema = file.cause.as_Schema().source;
    ASSERT_TRUE(schema.is_UnknownField());
    EXPECT_EQ(schema.as_UnknownField().node.value.as_str(), "manifest.lib"_str);
    EXPECT_EQ(schema.as_UnknownField().field.as_str(), "discovery"_str);

    auto manifest_source = rstd::as<rstd::error::Error>(error).source();
    ASSERT_TRUE(manifest_source.is_some());
    EXPECT_TRUE(rstd::error::is<lito::ManifestFileError>(*manifest_source));
    auto file_source = (*manifest_source)->source();
    ASSERT_TRUE(file_source.is_some());
    EXPECT_TRUE(rstd::error::is<lito::ManifestFileCause>(*file_source));
    auto cause_source = (*file_source)->source();
    ASSERT_TRUE(cause_source.is_some());
    EXPECT_TRUE(rstd::error::is<lito::ManifestSchemaError>(*cause_source));
}

TEST(Manifest, PackageManifestOwnsTypedTargetCollection) {
    auto loaded = lito::load_package_manifest(fixture_path("project/multi-target"_str).as_path());
    ASSERT_TRUE(loaded.is_ok());
    ASSERT_EQ(loaded->targets.len(), usize(7));

    auto libraries  = usize {};
    auto binaries   = usize {};
    auto tests      = usize {};
    auto benchmarks = usize {};
    auto no_stdlib  = usize {};
    for (const auto& target : loaded->targets) {
        switch (lito::package_target_kind(target)) {
        case lito::PackageTargetKind::Library: ++libraries; break;
        case lito::PackageTargetKind::Binary: ++binaries; break;
        case lito::PackageTargetKind::Test: ++tests; break;
        case lito::PackageTargetKind::Benchmark: ++benchmarks; break;
        case lito::PackageTargetKind::TestAttachment:
        case lito::PackageTargetKind::CompileTest: break;
        }
        if (! lito::package_target_links_stdlib(target)) ++no_stdlib;
    }
    EXPECT_EQ(libraries, usize(1));
    EXPECT_EQ(binaries, usize(2));
    EXPECT_EQ(tests, usize(2));
    EXPECT_EQ(benchmarks, usize(2));
    EXPECT_EQ(no_stdlib, usize(3));

    for (const auto& target : loaded->targets) {
        EXPECT_EQ(lito::package_target_source(target).discovery,
                  lito::SourceDiscoveryMode::Explicit);
    }

    auto module = lito::load_package_manifest(
        fixture_path("manifest/toml-module/directory-markers"_str).as_path());
    ASSERT_TRUE(module.is_ok());
    ASSERT_EQ(module->targets.len(), usize(1));
    EXPECT_EQ(lito::package_target_source(module->targets[usize {}]).discovery,
              lito::SourceDiscoveryMode::Module);

    auto groups = lito::load_package_manifest(fixture_path("project/compile-lib"_str).as_path());
    ASSERT_TRUE(groups.is_ok());
    ASSERT_EQ(groups->targets.len(), usize(1));
    EXPECT_EQ(lito::package_target_source(groups->targets[usize {}]).discovery,
              lito::SourceDiscoveryMode::Explicit);
}

TEST(Manifest, ManifestLocatorPrefersLitoAndAcceptsLegacyTenon) {
    auto legacy = lito::load_package_manifest(fixture_path("manifest/name/legacy"_str).as_path());
    ASSERT_TRUE(legacy.is_ok());
    EXPECT_EQ(legacy->name.as_str(), "legacy-manifest"_str);

    auto preferred =
        lito::load_package_manifest(fixture_path("manifest/name/preferred"_str).as_path());
    ASSERT_TRUE(preferred.is_ok());
    EXPECT_EQ(preferred->name.as_str(), "preferred-manifest"_str);
}

TEST(Manifest, PackageSourceRootIsIndependentFromWorkspaceMemberDirectory) {
    auto loaded = lito::load_package_manifest(
        fixture_path("workspace/shared-source-root/packages/library"_str).as_path());
    ASSERT_TRUE(loaded.is_ok());
    EXPECT_NE(loaded->root.as_path(), loaded->source_root.as_path());
    EXPECT_EQ(loaded->source_root.as_path(),
              fixture_path("workspace/shared-source-root"_str).as_path());
}

TEST(Manifest, ManifestGitCommitIsTypedAndValidated) {
    auto loaded = lito::load_package_manifest(fixture_path("manifest/git/commit"_str).as_path());
    ASSERT_TRUE(loaded.is_ok());
    ASSERT_EQ(loaded->dependencies.len(), usize(1));
    const auto& source = loaded->dependencies[usize {}].source;
    ASSERT_TRUE(source.is_Git());
    EXPECT_EQ(source.as_Git().reference.kind, lito::GitReferenceKind::Commit);
    EXPECT_EQ(source.as_Git().reference.value.as_str(),
              "0123456789abcdef0123456789abcdef01234567"_str);
}
