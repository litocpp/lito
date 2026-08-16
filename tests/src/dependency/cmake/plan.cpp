#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
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

class CMakePlan : public ProjectFixture {};

TEST_F(CMakePlan, CMakePlannerIsPureAndMaterializesOrderedPackageOperations) {
    auto parser = lito::make_clang_cpp_argument_parser();
    ASSERT_TRUE(parser.is_ok());
    auto tree = cmake_package_project_tree();
    ASSERT_TRUE(tree.is_ok());
    auto project = materialize("cmake-package"_str, *tree);
    ASSERT_TRUE(project.is_ok());
    auto platform = native_platform();
    auto targets  = Vec<lito::CMakeTargetRequirement>::make();
    targets.push(lito::CMakeTargetRequirement {
        .name = String::make("Fixture::fixture"_str),
    });
    auto prepared = lito::PreparedCMakeDependencyRequirement {
        .alias   = String::make("planner-fixture"_str),
        .package = String::make("Fixture"_str),
        .source  = lito::PreparedCMakeDependencySource::Directory(
            project->root.clone(), String::make("lito-test-cmake-planner-pure-v1"_str), false),
        .targets = rstd::move(targets),
    };
    auto requirement = lito::resolve_cmake_requirement_for_platform(prepared, platform);
    ASSERT_TRUE(requirement.is_ok());
    auto work_root = build_root("cmake-planner-work"_str);
    auto first     = lito::plan_cmake_package(*requirement,
                                              fixture_cmake(),
                                              configuration(),
                                              default_profile(*parser),
                                              platform.compiler_default,
                                              platform.effective_target.triple.as_str(),
                                              work_root.as_path(),
                                              usize(1));
    ASSERT_TRUE(first.is_ok());
    ASSERT_EQ(first->operations.len(), usize(7));
    EXPECT_EQ(first->operations[usize {}], lito::CMakePackageOperation::ConfigureSource);
    EXPECT_EQ(first->operations[usize(1)], lito::CMakePackageOperation::BuildSource);
    EXPECT_EQ(first->operations[usize(2)], lito::CMakePackageOperation::InstallSource);
    EXPECT_EQ(first->operations[usize(3)], lito::CMakePackageOperation::WriteQuery);
    EXPECT_EQ(first->operations[usize(4)], lito::CMakePackageOperation::ConfigureQuery);
    EXPECT_EQ(first->operations[usize(5)], lito::CMakePackageOperation::BuildQuery);
    EXPECT_EQ(first->operations[usize(6)], lito::CMakePackageOperation::ReadUsage);
    auto exists = rstd::fs::exists(first->area.root.as_path());
    ASSERT_TRUE(exists.is_ok());
    EXPECT_FALSE(*exists);

    auto parallel = lito::plan_cmake_package(*requirement,
                                             fixture_cmake(),
                                             configuration(),
                                             default_profile(*parser),
                                             platform.compiler_default,
                                             platform.effective_target.triple.as_str(),
                                             work_root.as_path(),
                                             usize(8));
    ASSERT_TRUE(parallel.is_ok());
    EXPECT_EQ(first->area.root.as_path(), parallel->area.root.as_path());
    EXPECT_EQ(first->area.query_root.as_path(), parallel->area.query_root.as_path());

    requirement->integration = lito::CMakeIntegration::BuildTree;
    auto build_tree          = lito::plan_cmake_package(*requirement,
                                                        fixture_cmake(),
                                                        configuration(),
                                                        default_profile(*parser),
                                                        platform.compiler_default,
                                                        platform.effective_target.triple.as_str(),
                                                        work_root.as_path());
    ASSERT_TRUE(build_tree.is_ok());
    ASSERT_EQ(build_tree->operations.len(), usize(4));
    EXPECT_EQ(build_tree->operations[usize {}], lito::CMakePackageOperation::WriteQuery);
    EXPECT_EQ(build_tree->operations[usize(1)], lito::CMakePackageOperation::ConfigureQuery);
    EXPECT_EQ(build_tree->operations[usize(2)], lito::CMakePackageOperation::BuildQuery);
    EXPECT_EQ(build_tree->operations[usize(3)], lito::CMakePackageOperation::ReadUsage);

    requirement->integration = lito::CMakeIntegration::Install;
    requirement->source      = lito::ResolvedCMakeDependencySource::Installed();
    auto installed           = lito::plan_cmake_package(*requirement,
                                                        fixture_cmake(),
                                                        configuration(),
                                                        default_profile(*parser),
                                                        platform.compiler_default,
                                                        platform.effective_target.triple.as_str(),
                                                        work_root.as_path());
    ASSERT_TRUE(installed.is_ok());
    ASSERT_EQ(installed->operations.len(), usize(4));
    EXPECT_EQ(installed->operations[usize {}], lito::CMakePackageOperation::WriteQuery);
    EXPECT_EQ(installed->operations[usize(1)], lito::CMakePackageOperation::ConfigureQuery);
    EXPECT_EQ(installed->operations[usize(2)], lito::CMakePackageOperation::BuildQuery);
    EXPECT_EQ(installed->operations[usize(3)], lito::CMakePackageOperation::ReadUsage);
}
