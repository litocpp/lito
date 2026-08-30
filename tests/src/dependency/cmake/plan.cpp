#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito.core;
import lito.system;
import lito.tools.cmake;
import lito.driver;
import lito.toolchain;
import lito.test.support;

using namespace rstd::prelude;
using namespace lito::system;
using namespace rstd::literals;
using namespace lito_test;
using PathBuf = rstd::path::PathBuf;

class CMakePlan : public ProjectFixture {};

TEST_F(CMakePlan, CMakePlannerIsPureAndMaterializesOrderedPackageOperations) {
    auto parser = lito::make_clang_cpp_argument_parser();
    ASSERT_TRUE(parser.is_ok());
    auto tree = cmake_package_project_tree();
    ASSERT_TRUE(tree.is_ok());
    auto project = materialize("cmake-package"_str, *tree);
    ASSERT_TRUE(project.is_ok());
    auto platform = native_platform();
    auto targets  = Vec<lito::dependency::CMakeTargetRequirement>::make();
    targets.push(lito::dependency::CMakeTargetRequirement {
        .name = String::make("Fixture::fixture"_str),
    });
    auto prepared = lito::PreparedCMakeDependencyRequirement {
        .alias   = String::make("planner-fixture"_str),
        .package = String::make("Fixture"_str),
        .source  = lito::PreparedCMakeDependencySource::Directory(
            project->root.clone(), String::make("lito-test-cmake-planner-pure-v1"_str), false),
        .targets = rstd::move(targets),
    };
    auto selected = lito::resolve_cmake_requirement_for_platform(prepared, platform);
    ASSERT_TRUE(selected.is_ok());
    auto requirement = lito::materialize_cmake_requirement(*selected);
    ASSERT_TRUE(requirement.is_ok());
    auto work_root = build_root("cmake-planner-work"_str);
    auto first     = lito::plan_cmake_package(*requirement,
                                              fixture_cmake(),
                                              configuration(),
                                              default_profile(*parser),
                                              linker_identity(),
                                              platform.compiler_default,
                                              platform.effective_target.triple.as_str(),
                                              work_root.as_path(),
                                              usize(1));
    ASSERT_TRUE(first.is_ok());
    ASSERT_EQ(first->tool.operations.len(), usize(7));
    EXPECT_EQ(first->tool.operations[usize {}], lito::CMakePackageOperation::ConfigureSource);
    EXPECT_EQ(first->tool.operations[usize(1)], lito::CMakePackageOperation::BuildSource);
    EXPECT_EQ(first->tool.operations[usize(2)], lito::CMakePackageOperation::InstallSource);
    EXPECT_EQ(first->tool.operations[usize(3)], lito::CMakePackageOperation::WriteQuery);
    EXPECT_EQ(first->tool.operations[usize(4)], lito::CMakePackageOperation::ConfigureQuery);
    EXPECT_EQ(first->tool.operations[usize(5)], lito::CMakePackageOperation::BuildQuery);
    EXPECT_EQ(first->tool.operations[usize(6)], lito::CMakePackageOperation::ReadUsage);
    auto exists = rstd::fs::exists(first->tool.area.root.as_path());
    ASSERT_TRUE(exists.is_ok());
    EXPECT_FALSE(*exists);
    EXPECT_TRUE(
        first->tool.area.root.as_path().to_string_lossy().as_str().contains("Fixture-"_str));
    EXPECT_EQ(first->tool.area.query_root.as_path(),
              first->tool.area.root.join(PathBuf::from("query"_str).as_path()).as_path());
    EXPECT_EQ(first->tool.area.state.as_path(),
              first->tool.area.root.join(PathBuf::from("state.json"_str).as_path()).as_path());

    auto parallel = lito::plan_cmake_package(*requirement,
                                             fixture_cmake(),
                                             configuration(),
                                             default_profile(*parser),
                                             linker_identity(),
                                             platform.compiler_default,
                                             platform.effective_target.triple.as_str(),
                                             work_root.as_path(),
                                             usize(8));
    ASSERT_TRUE(parallel.is_ok());
    EXPECT_EQ(first->tool.area.root.as_path(), parallel->tool.area.root.as_path());
    EXPECT_EQ(first->tool.area.query_root.as_path(), parallel->tool.area.query_root.as_path());

    auto cache_variant_requirement = requirement->clone();
    cache_variant_requirement.cache.push(lito::dependency::CMakeCacheEntry {
        .name  = String::make("FIXTURE_VARIANT"_str),
        .value = String::make("ON"_str),
    });
    auto cache_variant = lito::plan_cmake_package(cache_variant_requirement,
                                                  fixture_cmake(),
                                                  configuration(),
                                                  default_profile(*parser),
                                                  linker_identity(),
                                                  platform.compiler_default,
                                                  platform.effective_target.triple.as_str(),
                                                  work_root.as_path());
    ASSERT_TRUE(cache_variant.is_ok());
    EXPECT_EQ(first->tool.area.root.as_path(), cache_variant->tool.area.root.as_path());
    EXPECT_NE(first->tool.area.preparation_identity.as_str(),
              cache_variant->tool.area.preparation_identity.as_str());

    auto source_variant_requirement   = requirement->clone();
    source_variant_requirement.source = lito::ResolvedCMakeDependencySource::Directory(
        project->root.clone(), String::make("lito-test-cmake-planner-pure-v2"_str), false);
    auto source_variant = lito::plan_cmake_package(source_variant_requirement,
                                                   fixture_cmake(),
                                                   configuration(),
                                                   default_profile(*parser),
                                                   linker_identity(),
                                                   platform.compiler_default,
                                                   platform.effective_target.triple.as_str(),
                                                   work_root.as_path());
    ASSERT_TRUE(source_variant.is_ok());
    EXPECT_NE(first->tool.area.root.as_path(), source_variant->tool.area.root.as_path());

    auto package_variant_requirement    = requirement->clone();
    package_variant_requirement.package = String::make("OtherFixture"_str);
    auto package_variant = lito::plan_cmake_package(package_variant_requirement,
                                                    fixture_cmake(),
                                                    configuration(),
                                                    default_profile(*parser),
                                                    linker_identity(),
                                                    platform.compiler_default,
                                                    platform.effective_target.triple.as_str(),
                                                    work_root.as_path());
    ASSERT_TRUE(package_variant.is_ok());
    EXPECT_NE(first->tool.area.root.as_path(), package_variant->tool.area.root.as_path());

    requirement->components.push(String::make("Feature"_str));
    auto component_variant = lito::plan_cmake_package(*requirement,
                                                      fixture_cmake(),
                                                      configuration(),
                                                      default_profile(*parser),
                                                      linker_identity(),
                                                      platform.compiler_default,
                                                      platform.effective_target.triple.as_str(),
                                                      work_root.as_path());
    ASSERT_TRUE(component_variant.is_ok());
    EXPECT_EQ(first->tool.area.root.as_path(), component_variant->tool.area.root.as_path());
    EXPECT_EQ(first->tool.area.query_root.as_path(),
              component_variant->tool.area.query_root.as_path());
    EXPECT_NE(first->tool.area.query_identity.as_str(),
              component_variant->tool.area.query_identity.as_str());

    requirement->adapter = Some(project->root.join(PathBuf::from("adapter.cmake"_str).as_path()));
    auto source_adapter  = lito::plan_cmake_package(*requirement,
                                                    fixture_cmake(),
                                                    configuration(),
                                                    default_profile(*parser),
                                                    linker_identity(),
                                                    platform.compiler_default,
                                                    platform.effective_target.triple.as_str(),
                                                    work_root.as_path());
    ASSERT_TRUE(source_adapter.is_ok());
    ASSERT_EQ(source_adapter->tool.operations.len(), usize(4));
    EXPECT_EQ(source_adapter->tool.operations[usize {}], lito::CMakePackageOperation::WriteQuery);
    EXPECT_EQ(source_adapter->tool.operations[usize(1)],
              lito::CMakePackageOperation::ConfigureQuery);
    EXPECT_EQ(source_adapter->tool.operations[usize(2)], lito::CMakePackageOperation::BuildQuery);
    EXPECT_EQ(source_adapter->tool.operations[usize(3)], lito::CMakePackageOperation::ReadUsage);
    EXPECT_EQ(first->tool.area.root.as_path(), source_adapter->tool.area.root.as_path());
    EXPECT_EQ(first->tool.area.query_root.as_path(),
              source_adapter->tool.area.query_root.as_path());

    requirement->source = lito::ResolvedCMakeDependencySource::Find();
    auto find_adapter   = lito::plan_cmake_package(*requirement,
                                                   fixture_cmake(),
                                                   configuration(),
                                                   default_profile(*parser),
                                                   linker_identity(),
                                                   platform.compiler_default,
                                                   platform.effective_target.triple.as_str(),
                                                   work_root.as_path());
    ASSERT_TRUE(find_adapter.is_ok());
    ASSERT_EQ(find_adapter->tool.operations.len(), usize(4));
    EXPECT_EQ(find_adapter->tool.operations[usize {}], lito::CMakePackageOperation::WriteQuery);
    EXPECT_EQ(find_adapter->tool.operations[usize(1)], lito::CMakePackageOperation::ConfigureQuery);
    EXPECT_EQ(find_adapter->tool.operations[usize(2)], lito::CMakePackageOperation::BuildQuery);
    EXPECT_EQ(find_adapter->tool.operations[usize(3)], lito::CMakePackageOperation::ReadUsage);
    EXPECT_NE(source_adapter->tool.area.root.as_path(), find_adapter->tool.area.root.as_path());

    requirement->adapter = None();
    auto find_generic    = lito::plan_cmake_package(*requirement,
                                                    fixture_cmake(),
                                                    configuration(),
                                                    default_profile(*parser),
                                                    linker_identity(),
                                                    platform.compiler_default,
                                                    platform.effective_target.triple.as_str(),
                                                    work_root.as_path());
    ASSERT_TRUE(find_generic.is_ok());
    ASSERT_EQ(find_generic->tool.operations.len(), usize(4));
    EXPECT_EQ(find_generic->tool.operations[usize {}], lito::CMakePackageOperation::WriteQuery);
    EXPECT_EQ(find_generic->tool.operations[usize(1)], lito::CMakePackageOperation::ConfigureQuery);
    EXPECT_EQ(find_generic->tool.operations[usize(2)], lito::CMakePackageOperation::BuildQuery);
    EXPECT_EQ(find_generic->tool.operations[usize(3)], lito::CMakePackageOperation::ReadUsage);
    EXPECT_EQ(find_adapter->tool.area.query_root.as_path(),
              find_generic->tool.area.query_root.as_path());
    EXPECT_NE(find_adapter->tool.area.query_identity.as_str(),
              find_generic->tool.area.query_identity.as_str());
    EXPECT_TRUE(find_generic->tool.area.root.as_path().to_string_lossy().as_str().ends_with(
        "Fixture-installed"_str));

    auto install_prefix   = project->root.join(PathBuf::from("application-prefix"_str).as_path());
    auto find_with_prefix = lito::plan_cmake_package(*requirement,
                                                     fixture_cmake(),
                                                     configuration(),
                                                     default_profile(*parser),
                                                     linker_identity(),
                                                     platform.compiler_default,
                                                     platform.effective_target.triple.as_str(),
                                                     work_root.as_path(),
                                                     usize(1),
                                                     None(),
                                                     Some(install_prefix.clone()));
    ASSERT_TRUE(find_with_prefix.is_ok());
    ASSERT_TRUE(find_with_prefix->tool.requirement.find_install_prefix.is_some());
    EXPECT_EQ(find_with_prefix->tool.requirement.find_install_prefix->as_path(),
              install_prefix.as_path());
    EXPECT_EQ(find_with_prefix->tool.area.root.as_path(), find_generic->tool.area.root.as_path());
    EXPECT_EQ(find_with_prefix->tool.area.query_root.as_path(),
              find_generic->tool.area.query_root.as_path());
    EXPECT_NE(find_with_prefix->tool.area.query_identity.as_str(),
              find_generic->tool.area.query_identity.as_str());

    auto android = lito::AndroidCmakeProjection {
        .toolchain_file =
            project->root.join(PathBuf::from("android.toolchain.cmake"_str).as_path()),
        .abi              = String::make("arm64-v8a"_str),
        .platform         = String::make("android-21"_str),
        .standard_library = String::make("c++_shared"_str),
        .identity         = String::make("android-cmake-fixture-v1"_str),
    };
    auto android_plan = lito::plan_cmake_package(*requirement,
                                                 fixture_cmake(),
                                                 configuration(),
                                                 default_profile(*parser),
                                                 linker_identity(),
                                                 platform.compiler_default,
                                                 "aarch64-linux-android21"_str,
                                                 work_root.as_path(),
                                                 usize(1),
                                                 Some(rstd::move(android)));
    ASSERT_TRUE(android_plan.is_ok());
    ASSERT_TRUE(android_plan->tool.toolchain.target.is_some());
    EXPECT_EQ(android_plan->tool.toolchain.target->file.as_path(),
              project->root.join(PathBuf::from("android.toolchain.cmake"_str).as_path()).as_path());
    ASSERT_EQ(android_plan->tool.toolchain.target->cache.len(), usize(3));
    EXPECT_EQ(android_plan->tool.toolchain.target->cache[usize {}].name.as_str(),
              "ANDROID_ABI"_str);
    EXPECT_EQ(android_plan->tool.toolchain.target->cache[usize {}].value.as_str(), "arm64-v8a"_str);
    EXPECT_EQ(android_plan->tool.toolchain.target->cache[usize(1)].value.as_str(),
              "android-21"_str);
    EXPECT_EQ(android_plan->tool.toolchain.target->cache[usize(2)].value.as_str(),
              "c++_shared"_str);
    EXPECT_EQ(android_plan->tool.area.root.as_path(), find_generic->tool.area.root.as_path());
    EXPECT_NE(android_plan->tool.area.preparation_identity.as_str(),
              find_generic->tool.area.preparation_identity.as_str());
}

TEST_F(CMakePlan, PackageResolutionMergesRequirementsAndRejectsContractConflicts) {
    auto first_targets = Vec<lito::dependency::CMakeTargetRequirement>::make();
    first_targets.push(lito::dependency::CMakeTargetRequirement {
        .name       = String::make("Fixture::core"_str),
        .visibility = lito::dependency::DependencyVisibility::Public,
    });
    auto second_targets = Vec<lito::dependency::CMakeTargetRequirement>::make();
    second_targets.push(lito::dependency::CMakeTargetRequirement {
        .name       = String::make("Fixture::core"_str),
        .visibility = lito::dependency::DependencyVisibility::LinkOnly,
    });
    second_targets.push(lito::dependency::CMakeTargetRequirement {
        .name       = String::make("Fixture::extra"_str),
        .visibility = lito::dependency::DependencyVisibility::Private,
    });
    auto requirements = Vec<lito::ResolvedCMakeDependencyRequirement>::make();
    requirements.push(lito::ResolvedCMakeDependencyRequirement {
        .alias      = String::make("fixture-core"_str),
        .package    = String::make("Fixture"_str),
        .components = strings("Core"_str),
        .source     = lito::ResolvedCMakeDependencySource::Directory(
            PathBuf::from("/fixture"_str), String::make("git+fixture#1"_str), true),
        .targets = rstd::move(first_targets),
    });
    requirements.push(lito::ResolvedCMakeDependencyRequirement {
        .alias      = String::make("fixture-extra"_str),
        .package    = String::make("Fixture"_str),
        .components = strings("Extra"_str, "Core"_str),
        .source     = lito::ResolvedCMakeDependencySource::Directory(
            PathBuf::from("/fixture"_str), String::make("git+fixture#1"_str), true),
        .targets = rstd::move(second_targets),
    });
    auto merged = lito::resolve_cmake_package(requirements);
    ASSERT_TRUE(merged.is_ok());
    EXPECT_EQ(merged->requirement.alias.as_str(), "Fixture"_str);
    ASSERT_EQ(merged->requirement.components.len(), usize(2));
    EXPECT_EQ(merged->requirement.components[usize {}].as_str(), "Core"_str);
    EXPECT_EQ(merged->requirement.components[usize(1)].as_str(), "Extra"_str);
    ASSERT_EQ(merged->requirement.targets.len(), usize(2));
    EXPECT_EQ(merged->requirement.targets[usize {}].visibility,
              lito::dependency::DependencyVisibility::Private);

    auto parser = lito::make_clang_cpp_argument_parser();
    ASSERT_TRUE(parser.is_ok());
    auto platform = native_platform();
    auto plan     = lito::plan_cmake_package(merged->requirement,
                                             fixture_cmake(),
                                             configuration(),
                                             default_profile(*parser),
                                             linker_identity(),
                                             platform.compiler_default,
                                             platform.effective_target.triple.as_str(),
                                             build_root("cmake-package-resolution"_str).as_path());
    ASSERT_TRUE(plan.is_ok());
    auto target_snapshots = Vec<lito::tools::cmake::CMakeTargetUsageSnapshot>::make();
    target_snapshots.push(lito::tools::cmake::CMakeTargetUsageSnapshot {
        .compile = strings("-DFIXTURE_CORE=1"_str),
        .link    = strings("-lfixture-core"_str),
    });
    target_snapshots.push(lito::tools::cmake::CMakeTargetUsageSnapshot {
        .compile = strings("-DFIXTURE_EXTRA=1"_str),
        .link    = strings("-lfixture-extra"_str),
    });
    auto snapshot = lito::CMakeUsageSnapshot {
        .version = String::make("1.0.0"_str),
        .targets = rstd::move(target_snapshots),
        .combined =
            lito::tools::cmake::CMakeTargetUsageSnapshot {
                .link = strings("-lfixture-core"_str, "-lfixture-extra"_str),
            },
    };
    auto core_usage = lito::materialize_cmake_usage(*plan, snapshot, requirements[usize {}]);
    ASSERT_TRUE(core_usage.is_ok());
    ASSERT_EQ(core_usage->targets.len(), usize(1));
    EXPECT_EQ(core_usage->targets[usize {}].visibility,
              lito::dependency::DependencyVisibility::Public);
    ASSERT_EQ(core_usage->link_arguments.tokens.len(), usize(1));
    EXPECT_EQ(core_usage->link_arguments.tokens[usize {}].as_str(), "-lfixture-core"_str);
    auto extra_usage = lito::materialize_cmake_usage(*plan, snapshot, requirements[usize(1)]);
    ASSERT_TRUE(extra_usage.is_ok());
    ASSERT_EQ(extra_usage->targets.len(), usize(2));
    EXPECT_EQ(extra_usage->targets[usize {}].visibility,
              lito::dependency::DependencyVisibility::LinkOnly);
    EXPECT_EQ(extra_usage->targets[usize(1)].visibility,
              lito::dependency::DependencyVisibility::Private);
    ASSERT_EQ(extra_usage->link_arguments.tokens.len(), usize(2));

    requirements[usize(1)].source = lito::ResolvedCMakeDependencySource::Find();
    auto conflict                 = lito::resolve_cmake_package(requirements);
    ASSERT_TRUE(conflict.is_err());
    EXPECT_TRUE(error_chain_text(conflict.unwrap_err())
                    .as_str()
                    .contains("conflicting source contract"_str));
}
