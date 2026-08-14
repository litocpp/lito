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

TEST(CMake, CMakeManifestIsTypedAndSourceIsResolvedByExternalOwner) {
    auto loaded =
        lito::load_package_manifest(fixture_path("dependency/cmake/manifest/valid"_str).as_path());
    ASSERT_TRUE(loaded.is_ok());
    EXPECT_TRUE(loaded->dependencies.is_empty());
    ASSERT_EQ(loaded->cmake_external_dependencies.len(), usize(2));

    const auto installed_index =
        loaded->cmake_external_dependencies[usize {}].alias.as_str() == "vulkan"_str ? usize {}
                                                                                     : usize(1);
    const auto  source_index          = installed_index == usize {} ? usize(1) : usize {};
    const auto& installed_requirement = loaded->cmake_external_dependencies[installed_index];
    EXPECT_EQ(installed_requirement.package.as_str(), "Vulkan"_str);
    ASSERT_EQ(installed_requirement.targets.len(), usize(1));
    EXPECT_EQ(installed_requirement.targets[usize {}].name.as_str(), "Vulkan::Vulkan"_str);
    EXPECT_TRUE(installed_requirement.source.is_Installed());

    const auto& source_requirement = loaded->cmake_external_dependencies[source_index];
    EXPECT_EQ(source_requirement.package.as_str(), "LitoFixture"_str);
    ASSERT_EQ(source_requirement.targets.len(), usize(3));
    EXPECT_EQ(source_requirement.targets[usize {}].name.as_str(), "LitoFixture::fixture"_str);
    EXPECT_EQ(source_requirement.targets[usize(1)].name.as_str(), "LitoFixture::headers"_str);
    EXPECT_EQ(source_requirement.targets[usize(2)].name.as_str(), "LitoFixture::order"_str);
    EXPECT_TRUE(source_requirement.source.is_Path());
    ASSERT_EQ(source_requirement.cache.len(), usize(1));
    EXPECT_EQ(source_requirement.cache[usize {}].name.as_str(), "LITO_FIXTURE_OPTION"_str);
    EXPECT_EQ(source_requirement.cache[usize {}].value.as_str(), "ON"_str);
    ASSERT_TRUE(source_requirement.config_directory.is_some());
    EXPECT_EQ(source_requirement.config_directory->as_path().to_str().unwrap(),
              "lib/cmake/LitoFixture"_str);

    auto graph =
        lito::resolve_package_graph(fixture_path("dependency/cmake/manifest/valid"_str).as_path());
    ASSERT_TRUE(graph.is_ok());
    ASSERT_EQ(graph->packages.len(), usize(1));
    EXPECT_TRUE(graph->packages[usize {}].cmake_external_dependencies.is_empty());
    auto external = lito::prepare_external_dependency_sources(*graph, {});
    ASSERT_TRUE(external.is_ok());
    ASSERT_EQ(graph->packages[usize {}].cmake_external_dependencies.len(), usize(2));
    const auto resolved_index =
        graph->packages[usize {}].cmake_external_dependencies[usize {}].alias.as_str() ==
                "fixture"_str
            ? usize {}
            : usize(1);
    const auto& resolved = graph->packages[usize {}].cmake_external_dependencies[resolved_index];
    ASSERT_TRUE(resolved.source.is_Directory());
    EXPECT_FALSE(resolved.source.as_Directory().identity.is_empty());
    EXPECT_FALSE(resolved.source.as_Directory().root.as_path().to_str().unwrap().is_empty());
}

TEST(CMake, CMakeBuildTreeManifestIsTypedAndAdapterIsResolvedByPackageOwner) {
    auto directory = fixture_path("dependency/cmake/manifest/build-tree"_str);
    auto loaded    = lito::load_package_manifest(directory.as_path());
    ASSERT_TRUE(loaded.is_ok());
    ASSERT_EQ(loaded->cmake_external_dependencies.len(), usize(1));
    const auto& declared = loaded->cmake_external_dependencies[usize {}];
    EXPECT_EQ(declared.integration, lito::CMakeIntegration::BuildTree);
    ASSERT_TRUE(declared.source.is_Path());
    ASSERT_TRUE(declared.adapter.is_some());
    EXPECT_EQ(declared.adapter->as_path().to_str().unwrap(), "adapter.cmake"_str);

    auto graph = lito::resolve_package_graph(directory.as_path());
    ASSERT_TRUE(graph.is_ok());
    ASSERT_TRUE(lito::prepare_external_dependency_sources(*graph, {}).is_ok());
    const auto& resolved = graph->packages[usize {}].cmake_external_dependencies[usize {}];
    EXPECT_TRUE(resolved.source.is_Directory());
    EXPECT_EQ(resolved.integration, lito::CMakeIntegration::BuildTree);
    ASSERT_TRUE(resolved.adapter.is_some());
    EXPECT_TRUE(resolved.adapter->as_path().starts_with(directory.as_path()));
}

TEST(CMake, CMakeInvalidManifestDocumentsAreRejectedByManifestOwner) {
    for (const auto path : INVALID_CMAKE_MANIFESTS) {
        auto loaded = lito::load_manifest_document(fixture_path(path).as_path());
        EXPECT_TRUE(loaded.is_err());
    }
}

TEST(CMake, CMakeArchitectureArchivesManifestIsTyped) {
    auto loaded = lito::load_package_manifest(
        fixture_path("dependency/cmake/manifest/architecture-archives"_str).as_path());
    ASSERT_TRUE(loaded.is_ok());
    ASSERT_EQ(loaded->cmake_external_dependencies.len(), usize(1));
    const auto& source = loaded->cmake_external_dependencies[usize {}].source;
    ASSERT_TRUE(source.is_ArchitectureArchives());
    const auto& variants = source.as_ArchitectureArchives().variants;
    ASSERT_EQ(variants.len(), usize(2));
    EXPECT_EQ(variants[usize {}].architecture.as_str(), "aarch64"_str);
    EXPECT_EQ(variants[usize {}].url.as_str(), "https://example.com/fixture-linuxarm64.tar.gz"_str);
    EXPECT_EQ(variants[usize(1)].architecture.as_str(), "x86_64"_str);
    EXPECT_EQ(variants[usize(1)].sha256.as_str(),
              "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"_str);
}

TEST(CMake, CMakeArchitectureArchivesInvalidManifestsAreRejected) {
    for (const auto path : INVALID_CMAKE_ARCHITECTURE_ARCHIVE_MANIFESTS) {
        auto loaded = lito::load_manifest_document(fixture_path(path).as_path());
        if (loaded.is_ok()) rstd::io::eprintln("unexpected valid manifest: {}", path);
        EXPECT_TRUE(loaded.is_err());
    }
}

TEST(CMake, CMakeArchitectureArchivesAreSelectedForEffectiveTarget) {
    auto variants = Vec<lito::CMakeArchiveVariant>::make();
    variants.push(lito::CMakeArchiveVariant {
        .architecture = lito::Architecture { .name = String::make("aarch64"_str) },
        .url          = String::make("https://example.com/arm64.tar.gz"_str),
        .sha256 =
            String::make("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"_str),
    });
    variants.push(lito::CMakeArchiveVariant {
        .architecture = lito::Architecture { .name = String::make("x86_64"_str) },
        .url          = String::make("https://example.com/x64.tar.gz"_str),
        .sha256 =
            String::make("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"_str),
    });
    auto requirement = lito::PreparedCMakeDependencyRequirement {
        .alias   = String::make("fixture"_str),
        .package = String::make("Fixture"_str),
        .source  = lito::PreparedCMakeDependencySource::ArchitectureArchives(rstd::move(variants)),
        .integration = lito::CMakeIntegration::BuildTree,
    };

    auto native = lito::resolve_cmake_requirement_for_platform(requirement, native_platform());
    ASSERT_TRUE(native.is_ok());
    ASSERT_TRUE(native->source.is_Archive());
    EXPECT_EQ(native->source.as_Archive().url.as_str(), "https://example.com/x64.tar.gz"_str);

    auto arm = lito::resolve_cmake_requirement_for_platform(
        requirement, explicit_platform("aarch64-unknown-linux-gnu"_str));
    ASSERT_TRUE(arm.is_ok());
    ASSERT_TRUE(arm->source.is_Archive());
    EXPECT_EQ(arm->source.as_Archive().url.as_str(), "https://example.com/arm64.tar.gz"_str);
    EXPECT_EQ(arm->source.as_Archive().sha256.as_str(),
              "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"_str);

    auto missing = lito::resolve_cmake_requirement_for_platform(
        requirement, explicit_platform("riscv64-unknown-linux-gnu"_str));
    ASSERT_TRUE(missing.is_err());
    auto missing_error = rstd::move(missing).unwrap_err();
    ASSERT_TRUE(missing_error.is_Message());
    const auto& message = missing_error.as_Message().message;
    EXPECT_TRUE(message.as_str().contains("fixture"_str));
    EXPECT_TRUE(message.as_str().contains("riscv64-unknown-linux-gnu"_str));
    EXPECT_TRUE(message.as_str().contains("architecture 'riscv64'"_str));
    EXPECT_TRUE(message.as_str().contains("aarch64, x86_64"_str));

    auto declarations = Vec<lito::PreparedCMakeDependencyRequirement>::make();
    declarations.push(rstd::move(requirement));
    auto parser = lito::make_clang_cpp_argument_parser();
    ASSERT_TRUE(parser.is_ok());
    auto cross_cmake = resolve_cmake_fixtures(declarations,
                                              default_profile(*parser),
                                              explicit_platform("aarch64-unknown-linux-gnu"_str),
                                              *parser);
    ASSERT_TRUE(cross_cmake.is_err());
    auto cross_error = rstd::move(cross_cmake).unwrap_err();
    EXPECT_TRUE(error_chain_text(cross_error)
                    .as_str()
                    .contains("without an explicit CMake toolchain contract"_str));
}

TEST(CMake, CMakeArchitectureArchivesSurviveWorkspaceInheritanceAndPreparation) {
    auto directory = fixture_path("workspace/architecture-archives"_str);
    auto document  = lito::load_manifest_document(directory.as_path());
    ASSERT_TRUE(document.is_ok());
    ASSERT_TRUE(document->workspace.is_some());
    ASSERT_EQ(document->workspace->cmake_external_dependencies.len(), usize(1));
    const auto& declared = document->workspace->cmake_external_dependencies[usize {}];
    ASSERT_TRUE(declared.source.is_ArchitectureArchives());
    ASSERT_EQ(declared.source.as_ArchitectureArchives().variants.len(), usize(2));

    auto graph = lito::resolve_package_graph(directory.as_path());
    ASSERT_TRUE(graph.is_ok());
    ASSERT_EQ(graph->packages.len(), usize(1));
    ASSERT_EQ(graph->packages[usize {}].manifest.cmake_external_dependencies.len(), usize(1));
    const auto& inherited =
        graph->packages[usize {}].manifest.cmake_external_dependencies[usize {}];
    ASSERT_TRUE(inherited.source.is_ArchitectureArchives());
    ASSERT_EQ(inherited.source.as_ArchitectureArchives().variants.len(), usize(2));
    ASSERT_EQ(inherited.targets.len(), usize(1));
    EXPECT_EQ(inherited.targets[usize {}].name.as_str(), "Fixture::fixture"_str);

    const auto source_count = graph->sources.len();
    ASSERT_TRUE(lito::prepare_external_dependency_sources(*graph, {}).is_ok());
    ASSERT_EQ(graph->packages[usize {}].cmake_external_dependencies.len(), usize(1));
    const auto& prepared = graph->packages[usize {}].cmake_external_dependencies[usize {}];
    ASSERT_TRUE(prepared.source.is_ArchitectureArchives());
    EXPECT_EQ(prepared.source.as_ArchitectureArchives().variants.len(), usize(2));
    EXPECT_EQ(graph->sources.len(), source_count);
}

TEST(CMake, BuildPlatformMakesNativeAndExplicitTargetIntentObservable) {
    auto compiler_default = pkg_config_target();
    auto host             = lito::HostInfo {
        .architecture = compiler_default.architecture.clone(),
        .os           = compiler_default.os.clone(),
    };
    auto native = lito::resolve_build_platform(host, compiler_default, None());
    ASSERT_TRUE(native.is_ok());
    EXPECT_EQ(native->intent, lito::BuildTargetIntent::Native);
    EXPECT_FALSE(native->cross);
    EXPECT_EQ(native->effective_target.architecture.as_str(), "x86_64"_str);

    auto arm_default = lito::parse_target_info("aarch64-unknown-linux-gnu"_str);
    ASSERT_TRUE(arm_default.is_ok());
    auto unintentional_cross = lito::resolve_build_platform(host, *arm_default, None());
    ASSERT_TRUE(unintentional_cross.is_err());
    auto cross_error = rstd::move(unintentional_cross).unwrap_err();
    EXPECT_TRUE(rstd::format("{}", cross_error)
                    .as_str()
                    .contains("declare an explicit target/toolchain contract"_str));

    auto explicit_cross =
        lito::resolve_build_platform(host, compiler_default, Some("aarch64-unknown-linux-gnu"_str));
    ASSERT_TRUE(explicit_cross.is_ok());
    EXPECT_EQ(explicit_cross->intent, lito::BuildTargetIntent::ExplicitTarget);
    EXPECT_TRUE(explicit_cross->cross);
    EXPECT_EQ(explicit_cross->effective_target.architecture.as_str(), "aarch64"_str);
}

TEST(CMake, CMakeManifestAcceptsUnnamespacedTargets) {
    auto loaded = lito::load_package_manifest(
        fixture_path("dependency/cmake/manifest/unnamespaced-target"_str).as_path());
    ASSERT_TRUE(loaded.is_ok());
    ASSERT_EQ(loaded->cmake_external_dependencies.len(), usize(1));
    const auto& requirement = loaded->cmake_external_dependencies[usize {}];
    EXPECT_EQ(requirement.package.as_str(), "qjs"_str);
    ASSERT_EQ(requirement.targets.len(), usize(1));
    EXPECT_EQ(requirement.targets[usize {}].name.as_str(), "qjs"_str);
}
