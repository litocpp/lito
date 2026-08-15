#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito.cpp;
import lito.core;
import lito.system;
import lito.toolchain.cmake;
import lito.driver;
import lito.toolchain;
import lito.test.support;

using namespace rstd::prelude;
using namespace lito::system;
using namespace rstd::literals;
using namespace lito_test;
using PathBuf = rstd::path::PathBuf;

TEST(CMake, CMakeProviderBuildsInstallsAndReadsImportedTargetUsage) {
    auto parser = lito::make_clang_cpp_argument_parser();
    ASSERT_TRUE(parser.is_ok());
    auto count_directory = output_root("cmake-provider-count"_str);
    ASSERT_TRUE(clear_output(count_directory.as_path()));
    ASSERT_TRUE(rstd::fs::create_dir_all(count_directory.as_path()).is_ok());
    auto count_path =
        count_directory.join(rstd::path::PathBuf::from("configure-count"_str).as_path());
    auto target       = pkg_config_target();
    auto declarations = Vec<lito::PreparedCMakeDependencyRequirement>::make();
    auto targets      = Vec<lito::CMakeTargetRequirement>::make();
    targets.push(lito::CMakeTargetRequirement {
        .name       = String::make("LitoFixture::fixture"_str),
        .visibility = lito::DependencyVisibility::Private,
    });
    targets.push(lito::CMakeTargetRequirement {
        .name       = String::make("LitoFixture::headers"_str),
        .visibility = lito::DependencyVisibility::Public,
    });
    targets.push(lito::CMakeTargetRequirement {
        .name       = String::make("LitoFixture::order"_str),
        .visibility = lito::DependencyVisibility::LinkOnly,
    });
    auto cache = Vec<lito::CMakeCacheEntry>::make();
    cache.push(lito::CMakeCacheEntry {
        .name  = String::make("LITO_FIXTURE_CONFIGURE_COUNT"_str),
        .value = String::make(count_path.as_path().to_str().unwrap()),
    });
    declarations.push(lito::PreparedCMakeDependencyRequirement {
        .alias   = String::make("fixture"_str),
        .package = String::make("LitoFixture"_str),
        .source  = lito::PreparedCMakeDependencySource::Directory(
            fixture_path("dependency/cmake/project/package"_str),
            String::make("lito-test-cmake-fixture-v4"_str),
            true),
        .config_directory = Some(rstd::path::PathBuf::from("lib/cmake/LitoFixture"_str)),
        .cache            = rstd::move(cache),
        .targets          = rstd::move(targets),
    });
    auto cold_assets = Vec<lito::ExternalAssetSet>::make();
    auto resolved    = resolve_cmake_fixtures(declarations,
                                              default_profile(*parser),
                                              native_platform(),
                                              *parser,
                                              usize(1),
                                              rstd::addressof(cold_assets));
    if (resolved.is_err()) {
        auto error = rstd::move(resolved).unwrap_err();
        rstd::io::eprintln("{}", error);
        EXPECT_TRUE(false);
        return;
    }
    ASSERT_EQ(resolved->len(), usize(1));
    const auto& dependency = (*resolved)[usize {}];
    EXPECT_EQ(dependency.provider.as_str(), "cmake"_str);
    EXPECT_EQ(dependency.version.as_str(), "1.2.3"_str);
    ASSERT_EQ(dependency.targets.len(), usize(3));
    EXPECT_EQ(dependency.targets[usize {}].name.as_str(), "LitoFixture::fixture"_str);

    auto has_macro   = false;
    auto has_include = false;
    for (const auto& occurrence : dependency.targets[usize {}].compile_arguments.occurrences) {
        if (occurrence.argument.is_Macro()) {
            has_macro = has_macro || occurrence.argument.as_Macro().directive.value.as_str() ==
                                         "LITO_CMAKE_USAGE=1"_str;
        }
        if (occurrence.argument.is_IncludeDirectory()) has_include = true;
    }
    EXPECT_TRUE(has_macro);
    EXPECT_TRUE(has_include);

    auto has_archive = false;
    for (const auto& token : dependency.link_arguments.tokens) {
        if (token.as_str().contains("liblito_fixture.a"_str)) has_archive = true;
    }
    EXPECT_TRUE(has_archive);
    EXPECT_EQ(dependency.targets[usize(1)].name.as_str(), "LitoFixture::headers"_str);
    EXPECT_EQ(dependency.targets[usize(1)].visibility, lito::DependencyVisibility::Public);
    EXPECT_EQ(dependency.targets[usize(2)].name.as_str(), "LitoFixture::order"_str);
    EXPECT_EQ(dependency.targets[usize(2)].visibility, lito::DependencyVisibility::LinkOnly);

    ASSERT_EQ(cold_assets.len(), usize(1));
    EXPECT_EQ(cold_assets[usize {}].alias.as_str(), "fixture"_str);
    EXPECT_EQ(cold_assets[usize {}].name.as_str(), "runtime"_str);
    ASSERT_EQ(cold_assets[usize {}].entries.len(), usize(2));
    EXPECT_EQ(cold_assets[usize {}].entries[usize {}].logical_path.as_path(),
              PathBuf::from("runtime.bin"_str).as_path());
    EXPECT_EQ(cold_assets[usize {}].entries[usize(1)].logical_path.as_path(),
              PathBuf::from("nested/resource.dat"_str).as_path());

    auto first_count = rstd::fs::read_to_string(count_path.as_path());
    ASSERT_TRUE(first_count.is_ok());
    EXPECT_EQ(first_count->as_str(), "configure\n"_str);
    auto warm_assets = Vec<lito::ExternalAssetSet>::make();
    auto warm        = resolve_cmake_fixtures(declarations,
                                              default_profile(*parser),
                                              native_platform(),
                                              *parser,
                                              usize(1),
                                              rstd::addressof(warm_assets));
    ASSERT_TRUE(warm.is_ok());
    ASSERT_EQ(warm_assets.len(), cold_assets.len());
    ASSERT_EQ(warm_assets[usize {}].entries.len(), cold_assets[usize {}].entries.len());
    for (usize index {}; index < cold_assets[usize {}].entries.len(); ++index) {
        EXPECT_EQ(warm_assets[usize {}].entries[index].logical_path.as_path(),
                  cold_assets[usize {}].entries[index].logical_path.as_path());
        EXPECT_EQ(warm_assets[usize {}].entries[index].source.as_path(),
                  cold_assets[usize {}].entries[index].source.as_path());
    }
    auto warm_count = rstd::fs::read_to_string(count_path.as_path());
    ASSERT_TRUE(warm_count.is_ok());
    EXPECT_EQ(warm_count->as_str(), "configure\n"_str);
    declarations[usize {}].targets[usize {}].name = String::make("LitoFixture::headers"_str);
    declarations[usize {}].targets[usize(1)].name = String::make("LitoFixture::fixture"_str);
    auto queried_again =
        resolve_cmake_fixtures(declarations, default_profile(*parser), native_platform(), *parser);
    ASSERT_TRUE(queried_again.is_ok());
    auto second_count = rstd::fs::read_to_string(count_path.as_path());
    ASSERT_TRUE(second_count.is_ok());
    EXPECT_EQ(second_count->as_str(), "configure\n"_str);

    declarations[usize {}].cache.push(lito::CMakeCacheEntry {
        .name  = String::make("LITO_FIXTURE_VARIANT"_str),
        .value = String::make("ON"_str),
    });
    auto installed_again =
        resolve_cmake_fixtures(declarations, default_profile(*parser), native_platform(), *parser);
    ASSERT_TRUE(installed_again.is_ok());
    auto third_count = rstd::fs::read_to_string(count_path.as_path());
    ASSERT_TRUE(third_count.is_ok());
    EXPECT_EQ(third_count->as_str(), "configure\nconfigure\n"_str);

    auto disabled_profile = lito::cpp::make_profile_spec(configuration(),
                                                         lito::ProjectProfile {
                                                             .exceptions = false,
                                                             .rtti       = false,
                                                         },
                                                         build_profile("debug"_str),
                                                         *parser);
    ASSERT_TRUE(disabled_profile.is_ok());
    auto profile_variant =
        resolve_cmake_fixtures(declarations, *disabled_profile, native_platform(), *parser);
    ASSERT_TRUE(profile_variant.is_ok());
    auto fourth_count = rstd::fs::read_to_string(count_path.as_path());
    ASSERT_TRUE(fourth_count.is_ok());
    EXPECT_EQ(fourth_count->as_str(), "configure\nconfigure\nconfigure\n"_str);
    EXPECT_TRUE(clear_output(count_directory.as_path()));
}

TEST(CMake, CMakeProviderBuildsAndReadsBuildTreeTargetUsage) {
    auto parser = lito::make_clang_cpp_argument_parser();
    ASSERT_TRUE(parser.is_ok());
    auto targets = Vec<lito::CMakeTargetRequirement>::make();
    targets.push(lito::CMakeTargetRequirement {
        .name       = String::make("LitoBuildTree::fixture"_str),
        .visibility = lito::DependencyVisibility::Private,
    });
    auto declarations = Vec<lito::PreparedCMakeDependencyRequirement>::make();
    declarations.push(lito::PreparedCMakeDependencyRequirement {
        .alias   = String::make("fixture"_str),
        .package = String::make("LitoBuildTree"_str),
        .source  = lito::PreparedCMakeDependencySource::Directory(
            fixture_path("dependency/cmake/project/build-tree"_str),
            String::make("lito-test-cmake-build-tree-v1"_str),
            false),
        .integration = lito::CMakeIntegration::BuildTree,
        .adapter     = Some(fixture_path("dependency/cmake/manifest/build-tree/adapter.cmake"_str)),
        .targets     = rstd::move(targets),
    });
    auto target = pkg_config_target();
    auto resolved =
        resolve_cmake_fixtures(declarations, default_profile(*parser), native_platform(), *parser);
    if (resolved.is_err()) {
        auto error = rstd::move(resolved).unwrap_err();
        rstd::io::eprintln("{}", error);
        EXPECT_TRUE(false);
        return;
    }
    ASSERT_EQ(resolved->len(), usize(1));
    const auto& dependency = (*resolved)[usize {}];
    EXPECT_EQ(dependency.version.as_str(), "4.5.6"_str);
    ASSERT_EQ(dependency.targets.len(), usize(1));

    auto has_macro   = false;
    auto has_include = false;
    for (const auto& occurrence : dependency.targets[usize {}].compile_arguments.occurrences) {
        if (occurrence.argument.is_Macro()) {
            has_macro = has_macro || occurrence.argument.as_Macro().directive.value.as_str() ==
                                         "LITO_CMAKE_BUILD_TREE_USAGE=1"_str;
        }
        if (occurrence.argument.is_IncludeDirectory()) has_include = true;
    }
    EXPECT_TRUE(has_macro);
    EXPECT_TRUE(has_include);

    auto has_archive = false;
    for (const auto& token : dependency.link_arguments.tokens) {
        if (token.as_str().contains("liblito_build_tree.a"_str)) {
            has_archive = rstd::path::PathBuf::from(token.as_str()).as_path().is_absolute();
        }
    }
    EXPECT_TRUE(has_archive);
}
