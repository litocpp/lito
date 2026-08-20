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
    EXPECT_NE(first->tool.area.query_root.as_path(),
              component_variant->tool.area.query_root.as_path());

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
    EXPECT_NE(find_adapter->tool.area.query_root.as_path(),
              find_generic->tool.area.query_root.as_path());

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
    EXPECT_NE(android_plan->tool.area.root.as_path(), find_generic->tool.area.root.as_path());
}
