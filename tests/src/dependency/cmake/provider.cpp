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

class CMakeProvider : public ProjectFixture {};

TEST_F(CMakeProvider, CMakeProviderBuildsInstallsAndReadsImportedTargetUsage) {
    auto c_flags   = EnvironmentVariableGuard("CFLAGS"_str, "-DLITO_C_FLAGS_LEAK=1"_str);
    auto cxx_flags = EnvironmentVariableGuard("CXXFLAGS"_str, "-DLITO_CXX_FLAGS_LEAK=1"_str);
    auto ld_flags  = EnvironmentVariableGuard("LDFLAGS"_str, "-Wl,--lito-ld-flags-leak"_str);
    auto parser    = lito::make_clang_cpp_argument_parser();
    ASSERT_TRUE(parser.is_ok());
    auto tree = cmake_package_project_tree();
    ASSERT_TRUE(tree.is_ok());
    auto project = materialize("cmake-package"_str, *tree);
    ASSERT_TRUE(project.is_ok());
    auto count_directory = cache_root("cmake-provider-count"_str);
    ASSERT_TRUE(rstd::fs::create_dir_all(count_directory.as_path()).is_ok());
    auto count_path =
        count_directory.join(rstd::path::PathBuf::from("configure-count"_str).as_path());
    auto target       = pkg_config_target();
    auto declarations = Vec<lito::PreparedCMakeDependencyRequirement>::make();
    auto targets      = Vec<lito::dependency::CMakeTargetRequirement>::make();
    targets.push(lito::dependency::CMakeTargetRequirement {
        .name       = String::make("LitoFixture::fixture"_str),
        .visibility = lito::dependency::DependencyVisibility::Private,
    });
    targets.push(lito::dependency::CMakeTargetRequirement {
        .name       = String::make("LitoFixture::headers"_str),
        .visibility = lito::dependency::DependencyVisibility::Public,
    });
    targets.push(lito::dependency::CMakeTargetRequirement {
        .name       = String::make("LitoFixture::order"_str),
        .visibility = lito::dependency::DependencyVisibility::LinkOnly,
    });
    auto cache = Vec<lito::dependency::CMakeCacheEntry>::make();
    cache.push(lito::dependency::CMakeCacheEntry {
        .name  = String::make("LITO_FIXTURE_CONFIGURE_COUNT"_str),
        .value = String::make(count_path.as_path().to_str().unwrap()),
    });
    declarations.push(lito::PreparedCMakeDependencyRequirement {
        .alias   = String::make("fixture"_str),
        .package = String::make("LitoFixture"_str),
        .source  = lito::PreparedCMakeDependencySource::Directory(
            project->root.clone(), String::make("lito-test-cmake-fixture-v4"_str), true),
        .config_directory = Some(rstd::path::PathBuf::from("lib/cmake/LitoFixture"_str)),
        .cache            = rstd::move(cache),
        .targets          = rstd::move(targets),
    });
    auto cold_assets = Vec<lito::ExternalAssetSet>::make();
    auto resolved    = resolve_cmake_fixtures(declarations,
                                              default_profile(*parser),
                                              native_platform(),
                                              *parser,
                                              build_root("cmake-fixture-work"_str).as_path(),
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
    EXPECT_EQ(dependency.targets[usize(1)].visibility,
              lito::dependency::DependencyVisibility::Public);
    EXPECT_EQ(dependency.targets[usize(2)].name.as_str(), "LitoFixture::order"_str);
    EXPECT_EQ(dependency.targets[usize(2)].visibility,
              lito::dependency::DependencyVisibility::LinkOnly);

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
                                              build_root("cmake-fixture-work"_str).as_path(),
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
    auto queried_again = resolve_cmake_fixtures(declarations,
                                                default_profile(*parser),
                                                native_platform(),
                                                *parser,
                                                build_root("cmake-fixture-work"_str).as_path());
    ASSERT_TRUE(queried_again.is_ok());
    auto second_count = rstd::fs::read_to_string(count_path.as_path());
    ASSERT_TRUE(second_count.is_ok());
    EXPECT_EQ(second_count->as_str(), "configure\n"_str);

    declarations[usize {}].cache.push(lito::dependency::CMakeCacheEntry {
        .name  = String::make("LITO_FIXTURE_VARIANT"_str),
        .value = String::make("ON"_str),
    });
    auto installed_again = resolve_cmake_fixtures(declarations,
                                                  default_profile(*parser),
                                                  native_platform(),
                                                  *parser,
                                                  build_root("cmake-fixture-work"_str).as_path());
    ASSERT_TRUE(installed_again.is_ok());
    auto third_count = rstd::fs::read_to_string(count_path.as_path());
    ASSERT_TRUE(third_count.is_ok());
    EXPECT_EQ(third_count->as_str(), "configure\nconfigure\n"_str);

    auto disabled_profile = lito::cpp::make_profile_spec(configuration(),
                                                         lito::manifest::ProjectProfile {
                                                             .exceptions = false,
                                                             .rtti       = false,
                                                         },
                                                         build_profile("debug"_str),
                                                         *parser);
    ASSERT_TRUE(disabled_profile.is_ok());
    auto profile_variant = resolve_cmake_fixtures(declarations,
                                                  *disabled_profile,
                                                  native_platform(),
                                                  *parser,
                                                  build_root("cmake-fixture-work"_str).as_path());
    ASSERT_TRUE(profile_variant.is_ok());
    auto fourth_count = rstd::fs::read_to_string(count_path.as_path());
    ASSERT_TRUE(fourth_count.is_ok());
    EXPECT_EQ(fourth_count->as_str(), "configure\nconfigure\nconfigure\n"_str);
}

TEST_F(CMakeProvider, CMakeProviderBuildsAndReadsSourceAdapterTargetUsage) {
    auto parser = lito::make_clang_cpp_argument_parser();
    ASSERT_TRUE(parser.is_ok());
    auto tree = cmake_source_adapter_project_tree();
    ASSERT_TRUE(tree.is_ok());
    auto project = materialize("cmake-source-adapter"_str, *tree);
    ASSERT_TRUE(project.is_ok());
    auto targets = Vec<lito::dependency::CMakeTargetRequirement>::make();
    targets.push(lito::dependency::CMakeTargetRequirement {
        .name       = String::make("LitoSourceAdapter::fixture"_str),
        .visibility = lito::dependency::DependencyVisibility::Private,
    });
    auto declarations = Vec<lito::PreparedCMakeDependencyRequirement>::make();
    declarations.push(lito::PreparedCMakeDependencyRequirement {
        .alias   = String::make("fixture"_str),
        .package = String::make("LitoSourceAdapter"_str),
        .source  = lito::PreparedCMakeDependencySource::Directory(
            project->root.clone(), String::make("lito-test-cmake-source-adapter-v1"_str), false),
        .adapter = Some(project->root.join(PathBuf::from("adapter.cmake"_str).as_path())),
        .targets = rstd::move(targets),
    });
    auto target   = pkg_config_target();
    auto resolved = resolve_cmake_fixtures(declarations,
                                           default_profile(*parser),
                                           native_platform(),
                                           *parser,
                                           build_root("cmake-source-adapter-work"_str).as_path());
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
                                         "LITO_CMAKE_SOURCE_ADAPTER_USAGE=1"_str;
        }
        if (occurrence.argument.is_IncludeDirectory()) has_include = true;
    }
    EXPECT_TRUE(has_macro);
    EXPECT_TRUE(has_include);

    auto has_archive = false;
    for (const auto& token : dependency.link_arguments.tokens) {
        if (token.as_str().contains("liblito_source_adapter.a"_str)) {
            has_archive = rstd::path::PathBuf::from(token.as_str()).as_path().is_absolute();
        }
    }
    EXPECT_TRUE(has_archive);
}

TEST_F(CMakeProvider, CMakeProviderFindsPackageAndReadsGenericTargetUsage) {
    auto parser = lito::make_clang_cpp_argument_parser();
    ASSERT_TRUE(parser.is_ok());
    auto tree = cmake_find_package_tree();
    ASSERT_TRUE(tree.is_ok());
    auto project = materialize("cmake-find-generic"_str, *tree);
    ASSERT_TRUE(project.is_ok());
    auto provider = fixture_cmake();
    provider.search_paths.push(project->root.clone());
    auto targets = Vec<lito::dependency::CMakeTargetRequirement>::make();
    targets.push(lito::dependency::CMakeTargetRequirement {
        .name       = String::make("LitoFindFixture::raw"_str),
        .visibility = lito::dependency::DependencyVisibility::Private,
    });
    auto declarations = Vec<lito::PreparedCMakeDependencyRequirement>::make();
    declarations.push(lito::PreparedCMakeDependencyRequirement {
        .alias   = String::make("fixture"_str),
        .package = String::make("LitoFindFixture"_str),
        .source  = lito::PreparedCMakeDependencySource::Find(),
        .targets = rstd::move(targets),
    });
    auto resolved =
        resolve_cmake_fixtures_with_provider(declarations,
                                             default_profile(*parser),
                                             native_platform(),
                                             *parser,
                                             build_root("cmake-find-generic-work"_str).as_path(),
                                             rstd::move(provider));
    if (resolved.is_err()) {
        auto error = rstd::move(resolved).unwrap_err();
        rstd::io::eprintln("{}", error);
        EXPECT_TRUE(false);
        return;
    }
    ASSERT_EQ(resolved->len(), usize(1));
    EXPECT_EQ((*resolved)[usize {}].version.as_str(), "7.8.9"_str);
    ASSERT_EQ((*resolved)[usize {}].targets.len(), usize(1));
    auto has_macro = false;
    for (const auto& occurrence :
         (*resolved)[usize {}].targets[usize {}].compile_arguments.occurrences) {
        if (occurrence.argument.is_Macro()) {
            has_macro = has_macro || occurrence.argument.as_Macro().directive.value.as_str() ==
                                         "LITO_CMAKE_FIND_USAGE=1"_str;
        }
    }
    EXPECT_TRUE(has_macro);
}

TEST_F(CMakeProvider, CMakeProviderFindAdapterNormalizesTargetUsage) {
    auto parser = lito::make_clang_cpp_argument_parser();
    ASSERT_TRUE(parser.is_ok());
    auto tree = cmake_find_package_tree();
    ASSERT_TRUE(tree.is_ok());
    auto project = materialize("cmake-find-adapter"_str, *tree);
    ASSERT_TRUE(project.is_ok());
    auto provider = fixture_cmake();
    provider.search_paths.push(project->root.clone());
    auto targets = Vec<lito::dependency::CMakeTargetRequirement>::make();
    targets.push(lito::dependency::CMakeTargetRequirement {
        .name       = String::make("LitoFindFixture::fixture"_str),
        .visibility = lito::dependency::DependencyVisibility::Private,
    });
    auto declarations = Vec<lito::PreparedCMakeDependencyRequirement>::make();
    declarations.push(lito::PreparedCMakeDependencyRequirement {
        .alias   = String::make("fixture"_str),
        .package = String::make("LitoFindFixture"_str),
        .source  = lito::PreparedCMakeDependencySource::Find(),
        .adapter = Some(project->root.join(PathBuf::from("adapter.cmake"_str).as_path())),
        .targets = rstd::move(targets),
    });
    auto resolved =
        resolve_cmake_fixtures_with_provider(declarations,
                                             default_profile(*parser),
                                             native_platform(),
                                             *parser,
                                             build_root("cmake-find-adapter-work"_str).as_path(),
                                             rstd::move(provider));
    if (resolved.is_err()) {
        auto error = rstd::move(resolved).unwrap_err();
        rstd::io::eprintln("{}", error);
        EXPECT_TRUE(false);
        return;
    }
    ASSERT_EQ(resolved->len(), usize(1));
    EXPECT_EQ((*resolved)[usize {}].version.as_str(), "7.8.9"_str);
    ASSERT_EQ((*resolved)[usize {}].targets.len(), usize(1));
    auto has_macro = false;
    for (const auto& occurrence :
         (*resolved)[usize {}].targets[usize {}].compile_arguments.occurrences) {
        if (occurrence.argument.is_Macro()) {
            has_macro = has_macro || occurrence.argument.as_Macro().directive.value.as_str() ==
                                         "LITO_CMAKE_FIND_USAGE=1"_str;
        }
    }
    EXPECT_TRUE(has_macro);
}

TEST_F(CMakeProvider, CMakeProviderReportsFindAdapterTargetContractFailure) {
    auto parser = lito::make_clang_cpp_argument_parser();
    ASSERT_TRUE(parser.is_ok());
    auto tree = cmake_find_package_tree();
    ASSERT_TRUE(tree.is_ok());
    auto project = materialize("cmake-find-adapter-missing-target"_str, *tree);
    ASSERT_TRUE(project.is_ok());
    auto provider = fixture_cmake();
    provider.search_paths.push(project->root.clone());
    auto targets = Vec<lito::dependency::CMakeTargetRequirement>::make();
    targets.push(lito::dependency::CMakeTargetRequirement {
        .name       = String::make("LitoFindFixture::missing"_str),
        .visibility = lito::dependency::DependencyVisibility::Private,
    });
    auto declarations = Vec<lito::PreparedCMakeDependencyRequirement>::make();
    declarations.push(lito::PreparedCMakeDependencyRequirement {
        .alias   = String::make("fixture"_str),
        .package = String::make("LitoFindFixture"_str),
        .source  = lito::PreparedCMakeDependencySource::Find(),
        .adapter = Some(project->root.join(PathBuf::from("adapter.cmake"_str).as_path())),
        .targets = rstd::move(targets),
    });
    auto resolved = resolve_cmake_fixtures_with_provider(
        declarations,
        default_profile(*parser),
        native_platform(),
        *parser,
        build_root("cmake-find-adapter-missing-target-work"_str).as_path(),
        rstd::move(provider));
    ASSERT_TRUE(resolved.is_err());
    auto message = error_chain_text(rstd::move(resolved).unwrap_err());
    EXPECT_TRUE(message.as_str().contains("LitoFindFixture"_str));
    EXPECT_TRUE(message.as_str().contains("alias fixture"_str));
    EXPECT_TRUE(message.as_str().contains("find adapter"_str));
    EXPECT_TRUE(message.as_str().contains("LitoFindFixture::missing"_str));
    EXPECT_TRUE(message.as_str().contains("cmake-find-adapter-missing-target-work"_str));
}
