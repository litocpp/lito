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
        lito::cpp::resolve_native_targets(*private_metadata, "debug"_str, Vec<String>::make());
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
        lito::cpp::resolve_native_targets(*public_metadata, "debug"_str, Vec<String>::make());
    ASSERT_TRUE(public_plan.is_ok());
    EXPECT_TRUE(has_external_macro(public_plan->contexts[usize {}]));
    EXPECT_TRUE(has_external_macro(public_plan->contexts[usize(1)]));

    auto link_only_metadata =
        external_usage_metadata(lito::dependency::DependencyVisibility::LinkOnly, *parser);
    ASSERT_TRUE(link_only_metadata.is_ok());
    auto link_only_plan =
        lito::cpp::resolve_native_targets(*link_only_metadata, "debug"_str, Vec<String>::make());
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
    metadata->targets[usize {}].usage.arguments =
        lito::cpp::LanguageArgumentLayer::Cpp(as<Clone>(*parsed).clone());
    metadata->targets[usize {}].usage.interface_arguments =
        lito::cpp::LanguageArgumentLayer::Cpp(rstd::move(parsed).unwrap());
    metadata->targets[usize {}].usage.link_requirements.posix_threads = true;
    metadata->targets[usize {}].usage.link_requirements.thread_sources.push(
        String::make("static library usage"_str));
    metadata->targets[usize {}].usage.link_requirements.system_libraries.push(
        lito::link::SystemLibraryRequirement {
            .name   = String::make("platform-api"_str),
            .source = String::make("static library usage"_str),
        });

    auto planned = lito::cpp::resolve_native_targets(*metadata, "debug"_str, Vec<String>::make());
    ASSERT_TRUE(planned.is_ok());
    EXPECT_TRUE(lito::compiler::uses_posix_threads(
        planned->contexts[usize {}].language.as_Cpp().options.common));
    EXPECT_TRUE(lito::compiler::uses_posix_threads(
        planned->contexts[usize(1)].language.as_Cpp().options.common));
    EXPECT_TRUE(planned->link_requirements[usize(1)].posix_threads);
    ASSERT_EQ(planned->link_requirements[usize(1)].system_libraries.len(), usize(1));
    EXPECT_EQ(planned->link_requirements[usize(1)].system_libraries[usize {}].name.as_str(),
              "platform-api"_str);

    metadata->targets[usize(1)].dependencies[usize {}].visibility =
        lito::dependency::DependencyVisibility::LinkOnly;
    auto link_only = lito::cpp::resolve_native_targets(*metadata, "debug"_str, Vec<String>::make());
    ASSERT_TRUE(link_only.is_ok());
    EXPECT_FALSE(lito::compiler::uses_posix_threads(
        link_only->contexts[usize(1)].language.as_Cpp().options.common));
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

    metadata->profiles[usize {}].cpp.common.threading = lito::compiler::ThreadingModel::Posix;
    metadata->profiles[usize {}].cpp_link_requirements.posix_threads = true;
    metadata->profiles[usize {}].cpp_link_requirements.thread_sources.push(
        String::make("build.options"_str));

    auto planned = lito::cpp::resolve_native_targets(*metadata, "debug"_str, Vec<String>::make());
    ASSERT_TRUE(planned.is_ok());
    EXPECT_TRUE(planned->link_requirements[usize(1)].posix_threads);
}

TEST(DependencyUsage, ExternalCompileOptionsResolveInTheConsumerLanguage) {
    auto parser = lito::make_clang_cpp_argument_parser();
    ASSERT_TRUE(parser.is_ok());
    auto targets = Vec<lito::cpp::ExternalTargetUsage>::make();
    targets.push(lito::cpp::ExternalTargetUsage {
        .name            = String::make("fixture"_str),
        .compile_options = strings("-fno-builtin"_str, "-pthread"_str),
        .compile_source  = String::make("external fixture"_str),
    });
    auto dependencies = Vec<lito::cpp::ExternalDependencyUsage>::make();
    dependencies.push(lito::cpp::ExternalDependencyUsage {
        .alias   = String::make("fixture"_str),
        .targets = rstd::move(targets),
    });

    auto resolved = lito::cpp::resolve_external_usage(
        rstd::move(dependencies), lito::manifest::PackageLanguage::C, *parser);
    ASSERT_TRUE(resolved.is_ok());
    ASSERT_EQ(resolved->len(), usize(1));
    ASSERT_EQ((*resolved)[usize {}].targets.len(), usize(1));
    const auto& arguments = (*resolved)[usize {}].targets[usize {}].compile_arguments;
    ASSERT_TRUE(arguments.is_C());
    ASSERT_EQ(arguments.as_C().layer.occurrences.len(), usize(2));
    EXPECT_TRUE(arguments.as_C().layer.occurrences[usize {}].argument.is_Vendor());
    EXPECT_TRUE(arguments.as_C().layer.occurrences[usize(1)].argument.is_Common());
}

TEST(DependencyUsage, LinkOnlyDependencyStillChecksArtifactAbi) {
    auto parser = lito::make_clang_cpp_argument_parser();
    ASSERT_TRUE(parser.is_ok());
    auto metadata =
        external_usage_metadata(lito::dependency::DependencyVisibility::LinkOnly, *parser);
    ASSERT_TRUE(metadata.is_ok());

    auto parsed = parser->parse(strings("-D_GLIBCXX_DEBUG=1"_str), "library ABI"_str);
    ASSERT_TRUE(parsed.is_ok());
    metadata->targets[usize {}].usage.arguments =
        lito::cpp::LanguageArgumentLayer::Cpp(rstd::move(parsed).unwrap());

    auto planned = lito::cpp::resolve_native_targets(*metadata, "debug"_str, Vec<String>::make());
    ASSERT_TRUE(planned.is_err());
    auto error = rstd::move(planned).unwrap_err();
    ASSERT_TRUE(error.is_Message());
    EXPECT_TRUE(error.as_Message().message.as_str().contains("standard library ABI modes"_str));
    EXPECT_TRUE(error.as_Message().message.as_str().contains("_GLIBCXX_DEBUG"_str));
}
