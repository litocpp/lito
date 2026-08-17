#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito.cpp;
import lito.core;
import lito.system;
import lito.toolchain.cmake;
import lito.toolchain;
import lito.driver;
import lito.test.support;

using namespace rstd::prelude;
using namespace lito::system;
using namespace rstd::literals;
using namespace lito_test;
using PathBuf = rstd::path::PathBuf;

TEST(DependencyUsage, ExternalUsageSeparatesCompileVisibilityFromStaticLinkClosure) {
    auto parser = lito::make_clang_cpp_argument_parser();
    ASSERT_TRUE(parser.is_ok());

    auto private_metadata =
        external_usage_metadata(lito::dependency::DependencyVisibility::Private, *parser);
    ASSERT_TRUE(private_metadata.is_ok());
    auto private_plan =
        lito::cpp::resolve_source_discovery(*private_metadata, "debug"_str, Vec<String>::make());
    ASSERT_TRUE(private_plan.is_ok());
    EXPECT_TRUE(has_external_macro(private_plan->contexts[usize {}]));
    EXPECT_FALSE(has_external_macro(private_plan->contexts[usize(1)]));
    ASSERT_EQ(private_plan->link_inputs[usize(1)].len(), usize(2));
    EXPECT_TRUE(private_plan->link_inputs[usize(1)][usize {}].is_Target());
    EXPECT_TRUE(private_plan->link_inputs[usize(1)][usize(1)].is_External());

    auto public_metadata =
        external_usage_metadata(lito::dependency::DependencyVisibility::Public, *parser);
    ASSERT_TRUE(public_metadata.is_ok());
    auto public_plan =
        lito::cpp::resolve_source_discovery(*public_metadata, "debug"_str, Vec<String>::make());
    ASSERT_TRUE(public_plan.is_ok());
    EXPECT_TRUE(has_external_macro(public_plan->contexts[usize {}]));
    EXPECT_TRUE(has_external_macro(public_plan->contexts[usize(1)]));

    auto link_only_metadata =
        external_usage_metadata(lito::dependency::DependencyVisibility::LinkOnly, *parser);
    ASSERT_TRUE(link_only_metadata.is_ok());
    auto link_only_plan =
        lito::cpp::resolve_source_discovery(*link_only_metadata, "debug"_str, Vec<String>::make());
    ASSERT_TRUE(link_only_plan.is_ok());
    EXPECT_FALSE(has_external_macro(link_only_plan->contexts[usize {}]));
    EXPECT_FALSE(has_external_macro(link_only_plan->contexts[usize(1)]));
    ASSERT_EQ(link_only_plan->link_inputs[usize(1)].len(), usize(2));
    EXPECT_TRUE(link_only_plan->link_inputs[usize(1)][usize(1)].is_External());
}

TEST(DependencyUsage, StaticLinkRequirementsReachTheFinalLinkClosure) {
    auto parser = lito::make_clang_cpp_argument_parser();
    ASSERT_TRUE(parser.is_ok());
    auto metadata =
        external_usage_metadata(lito::dependency::DependencyVisibility::Private, *parser);
    ASSERT_TRUE(metadata.is_ok());

    auto parsed = parser->parse(strings("-pthread"_str), "static library usage"_str);
    ASSERT_TRUE(parsed.is_ok());
    metadata->targets[usize {}].usage.arguments                       = as<Clone>(*parsed).clone();
    metadata->targets[usize {}].usage.interface_arguments             = rstd::move(parsed).unwrap();
    metadata->targets[usize {}].usage.link_requirements.posix_threads = true;
    metadata->targets[usize {}].usage.link_requirements.thread_sources.push(
        String::make("static library usage"_str));
    metadata->targets[usize {}].usage.link_requirements.system_libraries.push(
        lito::cpp::CppSystemLibraryRequirement {
            .name   = String::make("platform-api"_str),
            .source = String::make("static library usage"_str),
        });

    auto planned = lito::cpp::resolve_source_discovery(*metadata, "debug"_str, Vec<String>::make());
    ASSERT_TRUE(planned.is_ok());
    EXPECT_TRUE(planned->contexts[usize {}].cpp.threading.posix);
    EXPECT_TRUE(planned->contexts[usize(1)].cpp.threading.posix);
    EXPECT_TRUE(planned->link_requirements[usize(1)].posix_threads);
    ASSERT_EQ(planned->link_requirements[usize(1)].system_libraries.len(), usize(1));
    EXPECT_EQ(planned->link_requirements[usize(1)].system_libraries[usize {}].name.as_str(),
              "platform-api"_str);

    metadata->targets[usize(1)].dependencies[usize {}].visibility =
        lito::dependency::DependencyVisibility::LinkOnly;
    auto link_only =
        lito::cpp::resolve_source_discovery(*metadata, "debug"_str, Vec<String>::make());
    ASSERT_TRUE(link_only.is_ok());
    EXPECT_FALSE(link_only->contexts[usize(1)].cpp.threading.posix);
    EXPECT_TRUE(link_only->link_requirements[usize(1)].posix_threads);
    ASSERT_EQ(link_only->link_requirements[usize(1)].system_libraries.len(), usize(1));
    EXPECT_EQ(link_only->link_requirements[usize(1)].system_libraries[usize {}].name.as_str(),
              "platform-api"_str);
}

TEST(DependencyUsage, ProfileThreadRequirementReachesTheFinalLink) {
    auto parser = lito::make_clang_cpp_argument_parser();
    ASSERT_TRUE(parser.is_ok());
    auto metadata =
        external_usage_metadata(lito::dependency::DependencyVisibility::Private, *parser);
    ASSERT_TRUE(metadata.is_ok());

    metadata->profiles[usize {}].cpp.threading.posix             = true;
    metadata->profiles[usize {}].link_requirements.posix_threads = true;
    metadata->profiles[usize {}].link_requirements.thread_sources.push(
        String::make("build.options"_str));

    auto planned = lito::cpp::resolve_source_discovery(*metadata, "debug"_str, Vec<String>::make());
    ASSERT_TRUE(planned.is_ok());
    EXPECT_TRUE(planned->link_requirements[usize(1)].posix_threads);
}

TEST(DependencyUsage, LinkOnlyDependencyStillChecksArtifactAbi) {
    auto parser = lito::make_clang_cpp_argument_parser();
    ASSERT_TRUE(parser.is_ok());
    auto metadata =
        external_usage_metadata(lito::dependency::DependencyVisibility::LinkOnly, *parser);
    ASSERT_TRUE(metadata.is_ok());

    auto parsed = parser->parse(strings("-D_GLIBCXX_DEBUG=1"_str), "library ABI"_str);
    ASSERT_TRUE(parsed.is_ok());
    metadata->targets[usize {}].usage.arguments = rstd::move(parsed).unwrap();

    auto planned = lito::cpp::resolve_source_discovery(*metadata, "debug"_str, Vec<String>::make());
    ASSERT_TRUE(planned.is_err());
    auto error = rstd::move(planned).unwrap_err();
    ASSERT_TRUE(error.is_Message());
    EXPECT_TRUE(error.as_Message().message.as_str().contains("standard library ABI modes"_str));
    EXPECT_TRUE(error.as_Message().message.as_str().contains("_GLIBCXX_DEBUG"_str));
}
