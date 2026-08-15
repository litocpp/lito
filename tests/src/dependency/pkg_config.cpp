#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito.cpp;
import lito.driver;
import lito.core;
import lito.system;
import lito.toolchain.cmake;
import lito.toolchain;
import lito.test.support;

using namespace rstd::prelude;
using namespace lito::system;
using namespace rstd::literals;
using namespace lito_test;
using PathBuf = rstd::path::PathBuf;

TEST(PkgConfig, PkgConfigManifestIsTypedBeforeResolution) {
    auto loaded = lito::load_package_manifest(
        fixture_path("dependency/pkg-config/manifest/valid"_str).as_path());
    ASSERT_TRUE(loaded.is_ok());
    EXPECT_TRUE(loaded->dependencies.is_empty());
    ASSERT_EQ(loaded->pkg_config_external_dependencies.len(), usize(2));
    const auto& curl = loaded->pkg_config_external_dependencies[usize {}];
    EXPECT_EQ(curl.alias.as_str(), "curl"_str);
    const auto& requirement = curl.requirement;
    EXPECT_EQ(requirement.module.as_str(), "libcurl"_str);
    ASSERT_TRUE(requirement.version.is_some());
    EXPECT_EQ(requirement.version->comparison, lito::PkgConfigVersionOperator::GreaterEqual);
    EXPECT_EQ(requirement.version->value.as_str(), "7.86.0"_str);
    EXPECT_EQ(requirement.mode, lito::PkgConfigQueryMode::Shared);

    auto graph = lito::resolve_package_graph(
        fixture_path("dependency/pkg-config/manifest/valid"_str).as_path());
    ASSERT_TRUE(graph.is_ok());
    ASSERT_EQ(graph->packages.len(), usize(1));
    EXPECT_TRUE(graph->packages[usize {}].dependencies.is_empty());
    EXPECT_EQ(graph->packages[usize {}].manifest.pkg_config_external_dependencies.len(), usize(2));
}

TEST(PkgConfig, PkgConfigInvalidManifestDocumentsAreRejectedByManifestOwner) {
    for (const auto path : INVALID_PKG_CONFIG_MANIFESTS) {
        auto loaded = lito::load_manifest_document(fixture_path(path).as_path());
        EXPECT_TRUE(loaded.is_err());
    }
}

TEST(PkgConfig, PkgConfigFragmentTokenizerPreservesArgumentsWithoutExecutingThem) {
    auto parsed = lito::tokenize_pkg_config_fragments(
        "-I'/path with spaces' -DVALUE=\\\"quoted\\\" '' '$()' ';'"_str);
    ASSERT_TRUE(parsed.is_ok());
    ASSERT_EQ(parsed->len(), usize(5));
    EXPECT_EQ((*parsed)[usize {}].as_str(), "-I/path with spaces"_str);
    EXPECT_EQ((*parsed)[usize(1)].as_str(), "-DVALUE=\"quoted\""_str);
    EXPECT_TRUE((*parsed)[usize(2)].is_empty());
    EXPECT_EQ((*parsed)[usize(3)].as_str(), "$()"_str);
    EXPECT_EQ((*parsed)[usize(4)].as_str(), ";"_str);

    EXPECT_TRUE(lito::tokenize_pkg_config_fragments("'unterminated"_str).is_err());
    EXPECT_TRUE(lito::tokenize_pkg_config_fragments("dangling\\"_str).is_err());

    auto double_quoted = lito::tokenize_pkg_config_fragments("\"double\\literal\""_str);
    ASSERT_TRUE(double_quoted.is_ok());
    ASSERT_EQ(double_quoted->len(), usize(1));
    EXPECT_EQ((*double_quoted)[usize {}].as_str(), "double\\literal"_str);
}

TEST(PkgConfig, PkgConfigProviderProducesTypedCompileAndOrderedLinkRequirements) {
    auto parser = lito::make_clang_cpp_argument_parser();
    ASSERT_TRUE(parser.is_ok());
    auto config       = fixture_pkg_config();
    auto target       = pkg_config_target();
    auto declarations = Vec<lito::PkgConfigExternalDependency>::make();
    declarations.push(versioned_fixture("fixture"_str,
                                        lito::PkgConfigVersionOperator::GreaterEqual,
                                        "2.0.0"_str,
                                        lito::PkgConfigQueryMode::Static));
    auto resolved = lito::resolve_external_dependencies(declarations,
                                                        config,
                                                        fixture_cmake(),
                                                        configuration(),
                                                        default_profile(*parser),
                                                        native_platform(),
                                                        *parser);
    ASSERT_TRUE(resolved.is_ok());
    ASSERT_EQ(resolved->len(), usize(1));
    EXPECT_EQ((*resolved)[usize {}].version.as_str(), "2.3.4"_str);

    auto user_include   = false;
    auto system_include = false;
    for (const auto& occurrence :
         (*resolved)[usize {}].targets[usize {}].compile_arguments.occurrences) {
        if (! occurrence.argument.is_IncludeDirectory()) continue;
        const auto& include = occurrence.argument.as_IncludeDirectory().directory;
        user_include = user_include || include.kind == lito::cpp::CppIncludeDirectoryKind::User;
        system_include =
            system_include || include.kind == lito::cpp::CppIncludeDirectoryKind::System;
    }
    EXPECT_TRUE(user_include);
    EXPECT_TRUE(system_include);

    auto repeat_count = usize {};
    auto has_private  = false;
    for (const auto& token : (*resolved)[usize {}].link_arguments.tokens) {
        if (token.as_str() == "-lrepeat"_str) ++repeat_count;
        if (token.as_str() == "-llito_private"_str) has_private = true;
    }
    EXPECT_EQ(repeat_count, usize(2));
    EXPECT_TRUE(has_private);

    declarations[usize {}].requirement.mode = lito::PkgConfigQueryMode::Shared;
    auto shared = lito::resolve_external_dependencies(declarations,
                                                      config,
                                                      fixture_cmake(),
                                                      configuration(),
                                                      default_profile(*parser),
                                                      native_platform(),
                                                      *parser);
    ASSERT_TRUE(shared.is_ok());
    for (const auto& token : (*shared)[usize {}].link_arguments.tokens) {
        EXPECT_NE(token.as_str(), "-llito_private"_str);
    }
}

TEST(PkgConfig, PkgConfigProviderSupportsVersionOperatorsAndReportsDependencyContext) {
    auto parser = lito::make_clang_cpp_argument_parser();
    ASSERT_TRUE(parser.is_ok());
    auto config       = fixture_pkg_config();
    auto target       = pkg_config_target();
    auto declarations = Vec<lito::PkgConfigExternalDependency>::make();
    declarations.push(
        versioned_fixture("equal"_str, lito::PkgConfigVersionOperator::Equal, "2.3.4"_str));
    declarations.push(
        versioned_fixture("less"_str, lito::PkgConfigVersionOperator::Less, "3.0.0"_str));
    declarations.push(
        versioned_fixture("greater"_str, lito::PkgConfigVersionOperator::Greater, "2.0.0"_str));
    declarations.push(versioned_fixture(
        "less-equal"_str, lito::PkgConfigVersionOperator::LessEqual, "2.3.4"_str));
    declarations.push(versioned_fixture(
        "greater-equal"_str, lito::PkgConfigVersionOperator::GreaterEqual, "2.3.4"_str));
    auto resolved = lito::resolve_external_dependencies(declarations,
                                                        config,
                                                        fixture_cmake(),
                                                        configuration(),
                                                        default_profile(*parser),
                                                        native_platform(),
                                                        *parser);
    ASSERT_TRUE(resolved.is_ok());
    EXPECT_EQ(resolved->len(), usize(5));

    auto incompatible = Vec<lito::PkgConfigExternalDependency>::make();
    incompatible.push(versioned_fixture(
        "incompatible"_str, lito::PkgConfigVersionOperator::Greater, "99.0.0"_str));
    auto failed = lito::resolve_external_dependencies(incompatible,
                                                      config,
                                                      fixture_cmake(),
                                                      configuration(),
                                                      default_profile(*parser),
                                                      native_platform(),
                                                      *parser);
    ASSERT_TRUE(failed.is_err());
    auto error = rstd::move(failed).unwrap_err();
    ASSERT_TRUE(error.is_Message());
    EXPECT_TRUE(error.as_Message().message.as_str().contains("incompatible"_str));
    EXPECT_TRUE(error.as_Message().message.as_str().contains("lito-fixture"_str));
}

TEST(PkgConfig, PkgConfigProviderFailsClosedForCrossTargetsAndMissingInputs) {
    auto parser = lito::make_clang_cpp_argument_parser();
    ASSERT_TRUE(parser.is_ok());
    auto config       = fixture_pkg_config();
    auto target       = pkg_config_target();
    auto declarations = Vec<lito::PkgConfigExternalDependency>::make();
    declarations.push(versioned_fixture(
        "fixture"_str, lito::PkgConfigVersionOperator::GreaterEqual, "2.0.0"_str));

    auto implicit_cross =
        lito::resolve_external_dependencies(declarations,
                                            config,
                                            fixture_cmake(),
                                            configuration(),
                                            default_profile(*parser),
                                            explicit_platform("aarch64-unknown-linux-gnu"_str),
                                            *parser);
    EXPECT_TRUE(implicit_cross.is_err());

    config.target_configured = true;
    auto explicit_cross =
        lito::resolve_external_dependencies(declarations,
                                            config,
                                            fixture_cmake(),
                                            configuration(),
                                            default_profile(*parser),
                                            explicit_platform("aarch64-unknown-linux-gnu"_str),
                                            *parser);
    EXPECT_TRUE(explicit_cross.is_ok());

    config.executable     = rstd::path::PathBuf::from("lito-missing-pkg-config-provider"_str);
    auto missing_provider = lito::resolve_external_dependencies(declarations,
                                                                config,
                                                                fixture_cmake(),
                                                                configuration(),
                                                                default_profile(*parser),
                                                                native_platform(),
                                                                *parser);
    ASSERT_TRUE(missing_provider.is_err());
    auto provider_error = rstd::move(missing_provider).unwrap_err();
    EXPECT_TRUE(error_chain_text(provider_error).as_str().contains("fixture"_str));
    EXPECT_TRUE(error_chain_text(provider_error).as_str().contains("lito-fixture"_str));

    config                                    = fixture_pkg_config();
    declarations[usize {}].alias              = String::make("missing-module"_str);
    declarations[usize {}].requirement.module = String::make("lito-module-does-not-exist"_str);
    auto missing_module = lito::resolve_external_dependencies(declarations,
                                                              config,
                                                              fixture_cmake(),
                                                              configuration(),
                                                              default_profile(*parser),
                                                              native_platform(),
                                                              *parser);
    ASSERT_TRUE(missing_module.is_err());
    auto module_error = rstd::move(missing_module).unwrap_err();
    ASSERT_TRUE(module_error.is_Message());
    EXPECT_TRUE(module_error.as_Message().message.as_str().contains("missing-module"_str));
    EXPECT_TRUE(
        module_error.as_Message().message.as_str().contains("lito-module-does-not-exist"_str));
}

TEST(PkgConfig, PkgConfigProviderCachesEquivalentQueriesWithinResolution) {
    auto parser = lito::make_clang_cpp_argument_parser();
    ASSERT_TRUE(parser.is_ok());
    auto directory = output_root("pkg-config-counting-provider"_str);
    ASSERT_TRUE(clear_output(directory.as_path()));
    ASSERT_TRUE(rstd::fs::create_dir_all(directory.as_path()).is_ok());
    auto config = lito::PkgConfigProviderConfig {
        .executable        = fixture_path("dependency/pkg-config/provider/counting-provider"_str),
        .sysroot           = Some(directory.clone()),
        .target_configured = true,
    };
    auto target       = pkg_config_target();
    auto declarations = Vec<lito::PkgConfigExternalDependency>::make();
    declarations.push(
        versioned_fixture("first"_str, lito::PkgConfigVersionOperator::GreaterEqual, "1.0.0"_str));
    declarations.push(
        versioned_fixture("second"_str, lito::PkgConfigVersionOperator::GreaterEqual, "1.0.0"_str));
    auto resolved = lito::resolve_external_dependencies(declarations,
                                                        config,
                                                        fixture_cmake(),
                                                        configuration(),
                                                        default_profile(*parser),
                                                        native_platform(),
                                                        *parser);
    ASSERT_TRUE(resolved.is_ok());
    ASSERT_EQ(resolved->len(), usize(2));
    auto count = rstd::fs::read_to_string(
        directory.join(rstd::path::PathBuf::from("provider-count"_str).as_path()).as_path());
    ASSERT_TRUE(count.is_ok());
    EXPECT_EQ(count->as_str(), "4\n"_str);
    EXPECT_TRUE(clear_output(directory.as_path()));
}
