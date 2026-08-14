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

TEST(PackageResolver, InvalidDependencyGraphsAreRejectedByResolverOwner) {
    for (const auto path : INVALID_GRAPHS) {
        auto resolved = lito::resolve_package_graph(fixture_path(path).as_path());
        if (resolved.is_ok()) rstd::io::eprintln("unexpected valid graph: {}", path);
        EXPECT_TRUE(resolved.is_err());
    }
}

TEST(PackageResolver, ProjectNameComesFromRootManifest) {
    auto workspace = lito::resolve_package_graph(repository_path("demo/workspace"_str).as_path());
    ASSERT_TRUE(workspace.is_ok());
    EXPECT_TRUE(workspace->root_is_workspace);
    EXPECT_EQ(workspace->name.as_str(), "demo-workspace"_str);

    auto workspace_member =
        lito::resolve_package_graph(repository_path("demo/workspace/app-one"_str).as_path());
    ASSERT_TRUE(workspace_member.is_ok());
    EXPECT_TRUE(workspace_member->root_is_workspace);
    EXPECT_EQ(workspace_member->name.as_str(), "demo-workspace"_str);

    auto package = lito::resolve_package_graph(
        repository_path("demo/module-convention/demo-app"_str).as_path());
    ASSERT_TRUE(package.is_ok());
    EXPECT_FALSE(package->root_is_workspace);
    EXPECT_EQ(package->name.as_str(), "demo-app"_str);
}
