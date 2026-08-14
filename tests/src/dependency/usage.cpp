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

TEST(DependencyUsage, ExternalUsageSeparatesCompileVisibilityFromStaticLinkClosure) {
    auto parser = lito::make_clang_cpp_argument_parser();
    ASSERT_TRUE(parser.is_ok());

    auto private_metadata = external_usage_metadata(lito::DependencyVisibility::Private, *parser);
    ASSERT_TRUE(private_metadata.is_ok());
    auto private_plan =
        lito::resolve_source_discovery(*private_metadata, "debug"_str, Vec<String>::make());
    ASSERT_TRUE(private_plan.is_ok());
    EXPECT_TRUE(has_external_macro(private_plan->contexts[usize {}]));
    EXPECT_FALSE(has_external_macro(private_plan->contexts[usize(1)]));
    ASSERT_EQ(private_plan->link_inputs[usize(1)].len(), usize(2));
    EXPECT_TRUE(private_plan->link_inputs[usize(1)][usize {}].is_Target());
    EXPECT_TRUE(private_plan->link_inputs[usize(1)][usize(1)].is_External());

    auto public_metadata = external_usage_metadata(lito::DependencyVisibility::Public, *parser);
    ASSERT_TRUE(public_metadata.is_ok());
    auto public_plan =
        lito::resolve_source_discovery(*public_metadata, "debug"_str, Vec<String>::make());
    ASSERT_TRUE(public_plan.is_ok());
    EXPECT_TRUE(has_external_macro(public_plan->contexts[usize {}]));
    EXPECT_TRUE(has_external_macro(public_plan->contexts[usize(1)]));

    auto link_only_metadata =
        external_usage_metadata(lito::DependencyVisibility::LinkOnly, *parser);
    ASSERT_TRUE(link_only_metadata.is_ok());
    auto link_only_plan =
        lito::resolve_source_discovery(*link_only_metadata, "debug"_str, Vec<String>::make());
    ASSERT_TRUE(link_only_plan.is_ok());
    EXPECT_FALSE(has_external_macro(link_only_plan->contexts[usize {}]));
    EXPECT_FALSE(has_external_macro(link_only_plan->contexts[usize(1)]));
    ASSERT_EQ(link_only_plan->link_inputs[usize(1)].len(), usize(2));
    EXPECT_TRUE(link_only_plan->link_inputs[usize(1)][usize(1)].is_External());
}
