#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import tenon;
import tenon.test.support;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace tenon_test;

namespace
{

inline constexpr ref<str> INVALID_MANIFESTS[] = {
    "manifest/git/multiple-selectors"_str,
    "manifest/git/path-and-git"_str,
    "manifest/git/url-fragment"_str,
    "manifest/package-name-dot"_str,
    "manifest/package-name-empty"_str,
    "manifest/test-attach-unknown-key"_str,
    "manifest/toml-explicit/dependency"_str,
    "manifest/toml-explicit/version-workspace-false"_str,
    "profile/owned-definition"_str,
    "profile/owned-option"_str,
    "workspace/mixed"_str,
};

inline constexpr ref<str> INVALID_PKG_CONFIG_MANIFESTS[] = {
    "manifest/pkg-config/path-version"_str, "manifest/pkg-config/selector"_str,
    "manifest/pkg-config/source-mix"_str,   "manifest/pkg-config/static-type"_str,
    "manifest/pkg-config/version"_str,
};

inline constexpr ref<str> INVALID_CMAKE_MANIFESTS[] = {
    "manifest/cmake/installed-cache"_str,
    "manifest/cmake/missing-target"_str,
    "manifest/cmake/provider-mix"_str,
    "manifest/cmake/unsafe-target"_str,
};

inline constexpr ref<str> INVALID_EXPLICIT_SOURCES[] = {
    "manifest/toml-explicit/duplicate"_str,
    "manifest/toml-explicit/missing"_str,
    "manifest/toml-explicit/outside-root"_str,
    "manifest/toml-explicit/unsupported"_str,
};

inline constexpr ref<str> INVALID_GRAPHS[] = {
    "resolver/cycle/a"_str,
    "resolver/missing"_str,
    "resolver/name-mismatch/root"_str,
    "resolver/same-name/root"_str,
    "workspace/default-not-member"_str,
    "workspace/duplicate-name"_str,
    "workspace/duplicate"_str,
    "workspace/inherited-version-missing"_str,
    "workspace/missing-member"_str,
    "workspace/nested"_str,
    "workspace/outside"_str,
};

inline constexpr ref<str> INVALID_LOCKS[] = {
    "lock/invalid"_str,
    "lock/missing"_str,
    "lock/stale"_str,
    "lock/v3"_str,
    "lock/v4-dangling"_str,
    "lock/v4-duplicate-name"_str,
    "lock/v4-duplicate-source"_str,
    "lock/v4-id-field"_str,
};

inline constexpr ref<str> VALID_BUILD_CASES[] = {
    "cache/long-path"_str,
    "discovery/preprocess/app"_str,
    "manifest/toml-module/directory-markers"_str,
    "manifest/toml-module/multiple-implementations"_str,
    "scanner/module-kinds"_str,
    "scanner/stdlib-header"_str,
};

inline constexpr ref<str> INVALID_BUILD_CASES[] = {
    "discovery/import-cycle"_str,
    "manifest/toml-module/logical-mismatch"_str,
    "manifest/toml-module/missing-primary"_str,
    "manifest/toml-module/partition-collision"_str,
    "scanner/header-unit"_str,
};

auto locked_graph_is_current(ref<str> relative) -> bool {
    auto directory = root(relative);
    auto session   = tenon::load_lock_session(directory.as_path(), true);
    if (session.is_err()) return false;
    auto options = session->take_resolution_options();
    auto graph   = tenon::resolve_package_graph(directory.as_path(), rstd::move(options));
    if (graph.is_err()) return false;
    return tenon::sync_lock(*graph, rstd::move(session).unwrap()).is_ok();
}

auto executable(const tenon::BuildSummary& summary) -> Option<ref<rstd::path::Path>> {
    for (const auto& artifact : summary.artifacts) {
        if (artifact.kind == tenon::ArtifactKind::Executable) {
            return Some(artifact.path.as_path());
        }
    }
    return None();
}

auto has_external_macro(const tenon::CompileContext& context) -> bool {
    for (const auto& macro : context.cpp.preprocessor.macros) {
        if (macro.value.as_str() == "TENON_EXTERNAL_USAGE=1"_str) return true;
    }
    return false;
}

auto pkg_config_target() -> tenon::TargetInfo {
    return tenon::TargetInfo {
        .triple = String::make("x86_64-unknown-linux-gnu"_str),
        .arch   = String::make("x86_64"_str),
        .os     = String::make("linux"_str),
        .family = tenon::TargetFamily::Unix,
    };
}

auto fixture_pkg_config() -> tenon::PkgConfigProviderConfig {
    auto library_paths = Vec<rstd::path::PathBuf>::make();
    library_paths.push(root("pkg-config"_str));
    return tenon::PkgConfigProviderConfig {
        .executable    = rstd::path::PathBuf::from("pkg-config"_str),
        .library_paths = rstd::move(library_paths),
    };
}

auto fixture_cmake() -> tenon::CMakeProviderConfig {
    return tenon::CMakeProviderConfig {
        .executable = rstd::path::PathBuf::from("cmake"_str),
        .generator  = String::make("Ninja"_str),
    };
}

auto versioned_fixture(ref<str>                        alias,
                       tenon::PkgConfigVersionOperator comparison,
                       ref<str>                        version,
                       tenon::PkgConfigQueryMode       mode = tenon::PkgConfigQueryMode::Shared)
    -> tenon::DeclaredExternalDependency {
    return tenon::DeclaredExternalDependency {
        .alias = String::make(alias),
        .requirement =
            tenon::ExternalDependencyRequirement::PkgConfig(tenon::PkgConfigDependencyRequirement {
                .module  = String::make("tenon-fixture"_str),
                .version = Some(tenon::PkgConfigVersionRequirement {
                    .comparison = comparison,
                    .value      = String::make(version),
                }),
                .mode    = mode,
            }),
    };
}

auto external_usage_metadata(tenon::DependencyVisibility     visibility,
                             const tenon::CppArgumentParser& parser)
    -> tenon::Result<tenon::PackageMetadata> {
    auto raw       = strings("-DTENON_EXTERNAL_USAGE=1"_str);
    auto arguments = parser.parse(raw, "pkg-config test fixture"_str);
    if (arguments.is_err()) {
        return Err(
            tenon::Error::make(tenon::ErrorKind::Dependency, rstd::move(arguments).unwrap_err()));
    }
    auto external = Vec<tenon::ResolvedExternalDependency>::make();
    external.push(tenon::ResolvedExternalDependency {
        .alias             = String::make("fixture"_str),
        .provider          = String::make("pkg-config"_str),
        .module            = String::make("tenon-fixture"_str),
        .version           = String::make("2.3.4"_str),
        .visibility        = visibility,
        .compile_arguments = rstd::move(arguments).unwrap(),
        .link_arguments =
            tenon::LinkArgumentSequence {
                .tokens   = strings("-ltenon_fixture"_str),
                .source   = String::make("pkg-config fixture"_str),
                .identity = String::make("fixture-link-v1"_str),
            },
        .identity = String::make("fixture-resolution-v1"_str),
    });
    auto dependencies = Vec<tenon::DependencySpec>::make();
    dependencies.push(tenon::DependencySpec {
        .target     = String::make("library"_str),
        .visibility = tenon::DependencyVisibility::Private,
    });
    auto targets = Vec<tenon::TargetMetadata>::make();
    targets.push(tenon::TargetMetadata {
        .manifest =
            tenon::PackageManifest {
                .name          = String::make("library"_str),
                .artifact_kind = tenon::ArtifactKind::StaticLibrary,
                .artifact_name = String::make("library"_str),
            },
        .external_dependencies = rstd::move(external),
    });
    targets.push(tenon::TargetMetadata {
        .manifest =
            tenon::PackageManifest {
                .name          = String::make("app"_str),
                .artifact_kind = tenon::ArtifactKind::Executable,
                .artifact_name = String::make("app"_str),
            },
        .dependencies = rstd::move(dependencies),
    });
    auto profile = tenon::make_profile_spec(configuration(), parser);
    if (profile.is_err()) return Err(rstd::move(profile).unwrap_err());
    auto profiles = Vec<tenon::ProfileSpec>::make();
    profiles.push(rstd::move(profile).unwrap());
    return Ok(tenon::PackageMetadata {
        .name            = String::make("external-usage"_str),
        .default_profile = String::make("debug"_str),
        .default_targets = strings("app"_str),
        .profiles        = rstd::move(profiles),
        .targets         = rstd::move(targets),
    });
}

} // namespace

TEST(Contracts, InvalidManifestDocumentsAreRejectedByManifestOwner) {
    for (const auto path : INVALID_MANIFESTS) {
        auto loaded = tenon::load_manifest_document(root(path).as_path());
        if (loaded.is_ok()) rstd::io::eprintln("unexpected valid manifest: {}", path);
        EXPECT_TRUE(loaded.is_err());
    }
}

TEST(Contracts, RemovedConfigFieldsAreRejectedByConfigOwner) {
    auto loaded = tenon::load_project_config(root("config/removed-scanner"_str).as_path());
    EXPECT_TRUE(loaded.is_err());
}

TEST(Contracts, PkgConfigProviderConfigurationBelongsToProjectConfig) {
    auto loaded = tenon::load_project_config(root("config/pkg-config"_str).as_path());
    ASSERT_TRUE(loaded.is_ok());
    EXPECT_TRUE(loaded->pkg_config.target_configured);
    EXPECT_EQ(loaded->pkg_config.executable.as_path().to_str().unwrap(), "pkg-config"_str);
    EXPECT_EQ(loaded->pkg_config.search_paths.len(), usize(1));
    EXPECT_EQ(loaded->pkg_config.library_paths.len(), usize(1));
    EXPECT_TRUE(loaded->pkg_config.sysroot.is_some());

    auto search_only =
        tenon::load_project_config(root("config/pkg-config-search-only"_str).as_path());
    ASSERT_TRUE(search_only.is_ok());
    EXPECT_FALSE(search_only->pkg_config.target_configured);
}

TEST(Contracts, CMakeProviderConfigurationBelongsToProjectConfig) {
    auto loaded = tenon::load_project_config(root("config/cmake"_str).as_path());
    ASSERT_TRUE(loaded.is_ok());
    EXPECT_EQ(loaded->cmake.executable.as_path().to_str().unwrap(), "custom-cmake"_str);
    EXPECT_EQ(loaded->cmake.generator.as_str(), "Unix Makefiles"_str);
}

TEST(Contracts, PkgConfigManifestIsTypedBeforeResolution) {
    auto loaded = tenon::load_package_manifest(root("manifest/pkg-config/valid"_str).as_path());
    ASSERT_TRUE(loaded.is_ok());
    ASSERT_EQ(loaded->dependencies.len(), usize(2));
    const auto& curl = loaded->dependencies[usize {}];
    EXPECT_EQ(curl.name.as_str(), "curl"_str);
    ASSERT_TRUE(curl.source.is_PkgConfig());
    const auto& requirement = curl.source.as_PkgConfig().requirement;
    EXPECT_EQ(requirement.module.as_str(), "libcurl"_str);
    ASSERT_TRUE(requirement.version.is_some());
    EXPECT_EQ(requirement.version->comparison, tenon::PkgConfigVersionOperator::GreaterEqual);
    EXPECT_EQ(requirement.version->value.as_str(), "7.86.0"_str);
    EXPECT_EQ(requirement.mode, tenon::PkgConfigQueryMode::Shared);

    auto graph = tenon::resolve_package_graph(root("manifest/pkg-config/valid"_str).as_path());
    ASSERT_TRUE(graph.is_ok());
    ASSERT_EQ(graph->packages.len(), usize(1));
    EXPECT_TRUE(graph->packages[usize {}].dependencies.is_empty());
    EXPECT_EQ(graph->packages[usize {}].external_dependencies.len(), usize(2));
}

TEST(Contracts, PkgConfigInvalidManifestDocumentsAreRejectedByManifestOwner) {
    for (const auto path : INVALID_PKG_CONFIG_MANIFESTS) {
        auto loaded = tenon::load_manifest_document(root(path).as_path());
        EXPECT_TRUE(loaded.is_err());
    }
}

TEST(Contracts, CMakeManifestIsTypedAndSourceIsResolvedByPackageOwner) {
    auto loaded = tenon::load_package_manifest(root("manifest/cmake/valid"_str).as_path());
    ASSERT_TRUE(loaded.is_ok());
    ASSERT_EQ(loaded->dependencies.len(), usize(2));

    const auto installed_index =
        loaded->dependencies[usize {}].name.as_str() == "vulkan"_str ? usize {} : usize(1);
    const auto  source_index = installed_index == usize {} ? usize(1) : usize {};
    const auto& installed    = loaded->dependencies[installed_index];
    ASSERT_TRUE(installed.source.is_CMake());
    const auto& installed_requirement = installed.source.as_CMake().requirement;
    EXPECT_EQ(installed_requirement.package.as_str(), "Vulkan"_str);
    EXPECT_EQ(installed_requirement.target.as_str(), "Vulkan::Vulkan"_str);
    EXPECT_TRUE(installed_requirement.source.is_Installed());

    const auto& source = loaded->dependencies[source_index];
    ASSERT_TRUE(source.source.is_CMake());
    const auto& source_requirement = source.source.as_CMake().requirement;
    EXPECT_EQ(source_requirement.package.as_str(), "TenonFixture"_str);
    EXPECT_EQ(source_requirement.target.as_str(), "TenonFixture::fixture"_str);
    EXPECT_TRUE(source_requirement.source.is_Path());
    ASSERT_EQ(source_requirement.cache.len(), usize(1));
    EXPECT_EQ(source_requirement.cache[usize {}].name.as_str(), "TENON_FIXTURE_OPTION"_str);
    EXPECT_EQ(source_requirement.cache[usize {}].value.as_str(), "ON"_str);

    auto graph = tenon::resolve_package_graph(root("manifest/cmake/valid"_str).as_path());
    ASSERT_TRUE(graph.is_ok());
    ASSERT_EQ(graph->packages.len(), usize(1));
    ASSERT_EQ(graph->packages[usize {}].external_dependencies.len(), usize(2));
    const auto resolved_index =
        graph->packages[usize {}].external_dependencies[usize {}].alias.as_str() == "fixture"_str
            ? usize {}
            : usize(1);
    const auto& resolved = graph->packages[usize {}].external_dependencies[resolved_index];
    ASSERT_TRUE(resolved.requirement.is_CMake());
    EXPECT_TRUE(resolved.requirement.as_CMake().requirement.source_root.is_some());
    EXPECT_TRUE(resolved.requirement.as_CMake().requirement.source_identity.is_some());
}

TEST(Contracts, CMakeInvalidManifestDocumentsAreRejectedByManifestOwner) {
    for (const auto path : INVALID_CMAKE_MANIFESTS) {
        auto loaded = tenon::load_manifest_document(root(path).as_path());
        EXPECT_TRUE(loaded.is_err());
    }
}

TEST(Contracts, PkgConfigFragmentTokenizerPreservesArgumentsWithoutExecutingThem) {
    auto parsed = tenon::tokenize_pkg_config_fragments(
        "-I'/path with spaces' -DVALUE=\\\"quoted\\\" '' '$()' ';'"_str);
    ASSERT_TRUE(parsed.is_ok());
    ASSERT_EQ(parsed->len(), usize(5));
    EXPECT_EQ((*parsed)[usize {}].as_str(), "-I/path with spaces"_str);
    EXPECT_EQ((*parsed)[usize(1)].as_str(), "-DVALUE=\"quoted\""_str);
    EXPECT_TRUE((*parsed)[usize(2)].is_empty());
    EXPECT_EQ((*parsed)[usize(3)].as_str(), "$()"_str);
    EXPECT_EQ((*parsed)[usize(4)].as_str(), ";"_str);

    EXPECT_TRUE(tenon::tokenize_pkg_config_fragments("'unterminated"_str).is_err());
    EXPECT_TRUE(tenon::tokenize_pkg_config_fragments("dangling\\"_str).is_err());

    auto double_quoted = tenon::tokenize_pkg_config_fragments("\"double\\literal\""_str);
    ASSERT_TRUE(double_quoted.is_ok());
    ASSERT_EQ(double_quoted->len(), usize(1));
    EXPECT_EQ((*double_quoted)[usize {}].as_str(), "double\\literal"_str);
}

TEST(Contracts, PkgConfigProviderProducesTypedCompileAndOrderedLinkRequirements) {
    auto parser = tenon::make_clang_cpp_argument_parser();
    ASSERT_TRUE(parser.is_ok());
    auto config       = fixture_pkg_config();
    auto target       = pkg_config_target();
    auto declarations = Vec<tenon::DeclaredExternalDependency>::make();
    declarations.push(versioned_fixture("fixture"_str,
                                        tenon::PkgConfigVersionOperator::GreaterEqual,
                                        "2.0.0"_str,
                                        tenon::PkgConfigQueryMode::Static));
    auto resolved = tenon::resolve_external_dependencies(declarations,
                                                         config,
                                                         fixture_cmake(),
                                                         configuration(),
                                                         target,
                                                         target.triple.as_str(),
                                                         *parser);
    ASSERT_TRUE(resolved.is_ok());
    ASSERT_EQ(resolved->len(), usize(1));
    EXPECT_EQ((*resolved)[usize {}].version.as_str(), "2.3.4"_str);

    auto user_include   = false;
    auto system_include = false;
    for (const auto& occurrence : (*resolved)[usize {}].compile_arguments.occurrences) {
        if (! occurrence.argument.is_IncludeDirectory()) continue;
        const auto& include = occurrence.argument.as_IncludeDirectory().directory;
        user_include        = user_include || include.kind == tenon::CppIncludeDirectoryKind::User;
        system_include = system_include || include.kind == tenon::CppIncludeDirectoryKind::System;
    }
    EXPECT_TRUE(user_include);
    EXPECT_TRUE(system_include);

    auto repeat_count = usize {};
    auto has_private  = false;
    for (const auto& token : (*resolved)[usize {}].link_arguments.tokens) {
        if (token.as_str() == "-lrepeat"_str) ++repeat_count;
        if (token.as_str() == "-ltenon_private"_str) has_private = true;
    }
    EXPECT_EQ(repeat_count, usize(2));
    EXPECT_TRUE(has_private);

    declarations[usize {}].requirement.as_PkgConfig().requirement.mode =
        tenon::PkgConfigQueryMode::Shared;
    auto shared = tenon::resolve_external_dependencies(declarations,
                                                       config,
                                                       fixture_cmake(),
                                                       configuration(),
                                                       target,
                                                       target.triple.as_str(),
                                                       *parser);
    ASSERT_TRUE(shared.is_ok());
    for (const auto& token : (*shared)[usize {}].link_arguments.tokens) {
        EXPECT_NE(token.as_str(), "-ltenon_private"_str);
    }
}

TEST(Contracts, CMakeProviderBuildsInstallsAndReadsImportedTargetUsage) {
    auto parser = tenon::make_clang_cpp_argument_parser();
    ASSERT_TRUE(parser.is_ok());
    auto target       = pkg_config_target();
    auto declarations = Vec<tenon::DeclaredExternalDependency>::make();
    declarations.push(tenon::DeclaredExternalDependency {
        .alias = String::make("fixture"_str),
        .requirement =
            tenon::ExternalDependencyRequirement::CMake(tenon::ResolvedCMakeDependencyRequirement {
                .package         = String::make("TenonFixture"_str),
                .target          = String::make("TenonFixture::fixture"_str),
                .source_identity = Some(String::make("tenon-test-cmake-fixture-v1"_str)),
                .source_root     = Some(root("cmake/package"_str)),
            }),
        .visibility = tenon::DependencyVisibility::Public,
    });
    auto resolved = tenon::resolve_external_dependencies(declarations,
                                                         fixture_pkg_config(),
                                                         fixture_cmake(),
                                                         configuration(),
                                                         target,
                                                         target.triple.as_str(),
                                                         *parser);
    ASSERT_TRUE(resolved.is_ok());
    ASSERT_EQ(resolved->len(), usize(1));
    const auto& dependency = (*resolved)[usize {}];
    EXPECT_EQ(dependency.provider.as_str(), "cmake"_str);
    EXPECT_EQ(dependency.module.as_str(), "TenonFixture::fixture"_str);
    EXPECT_EQ(dependency.version.as_str(), "1.2.3"_str);

    auto has_macro   = false;
    auto has_include = false;
    for (const auto& occurrence : dependency.compile_arguments.occurrences) {
        if (occurrence.argument.is_Macro()) {
            has_macro = has_macro || occurrence.argument.as_Macro().directive.value.as_str() ==
                                         "TENON_CMAKE_USAGE=1"_str;
        }
        if (occurrence.argument.is_IncludeDirectory()) has_include = true;
    }
    EXPECT_TRUE(has_macro);
    EXPECT_TRUE(has_include);

    auto has_archive = false;
    for (const auto& token : dependency.link_arguments.tokens) {
        if (token.as_str().contains("libtenon_fixture.a"_str)) has_archive = true;
    }
    EXPECT_TRUE(has_archive);
}

TEST(Contracts, PkgConfigProviderSupportsVersionOperatorsAndReportsDependencyContext) {
    auto parser = tenon::make_clang_cpp_argument_parser();
    ASSERT_TRUE(parser.is_ok());
    auto config       = fixture_pkg_config();
    auto target       = pkg_config_target();
    auto declarations = Vec<tenon::DeclaredExternalDependency>::make();
    declarations.push(
        versioned_fixture("equal"_str, tenon::PkgConfigVersionOperator::Equal, "2.3.4"_str));
    declarations.push(
        versioned_fixture("less"_str, tenon::PkgConfigVersionOperator::Less, "3.0.0"_str));
    declarations.push(
        versioned_fixture("greater"_str, tenon::PkgConfigVersionOperator::Greater, "2.0.0"_str));
    declarations.push(versioned_fixture(
        "less-equal"_str, tenon::PkgConfigVersionOperator::LessEqual, "2.3.4"_str));
    declarations.push(versioned_fixture(
        "greater-equal"_str, tenon::PkgConfigVersionOperator::GreaterEqual, "2.3.4"_str));
    auto resolved = tenon::resolve_external_dependencies(declarations,
                                                         config,
                                                         fixture_cmake(),
                                                         configuration(),
                                                         target,
                                                         target.triple.as_str(),
                                                         *parser);
    ASSERT_TRUE(resolved.is_ok());
    EXPECT_EQ(resolved->len(), usize(5));

    auto incompatible = Vec<tenon::DeclaredExternalDependency>::make();
    incompatible.push(versioned_fixture(
        "incompatible"_str, tenon::PkgConfigVersionOperator::Greater, "99.0.0"_str));
    auto failed = tenon::resolve_external_dependencies(incompatible,
                                                       config,
                                                       fixture_cmake(),
                                                       configuration(),
                                                       target,
                                                       target.triple.as_str(),
                                                       *parser);
    ASSERT_TRUE(failed.is_err());
    auto error = rstd::move(failed).unwrap_err();
    EXPECT_TRUE(error.message.as_str().contains("incompatible"_str));
    EXPECT_TRUE(error.message.as_str().contains("tenon-fixture"_str));
}

TEST(Contracts, PkgConfigProviderFailsClosedForCrossTargetsAndMissingInputs) {
    auto parser = tenon::make_clang_cpp_argument_parser();
    ASSERT_TRUE(parser.is_ok());
    auto config       = fixture_pkg_config();
    auto target       = pkg_config_target();
    auto declarations = Vec<tenon::DeclaredExternalDependency>::make();
    declarations.push(versioned_fixture(
        "fixture"_str, tenon::PkgConfigVersionOperator::GreaterEqual, "2.0.0"_str));

    auto implicit_cross = tenon::resolve_external_dependencies(declarations,
                                                               config,
                                                               fixture_cmake(),
                                                               configuration(),
                                                               target,
                                                               "aarch64-unknown-linux-gnu"_str,
                                                               *parser);
    EXPECT_TRUE(implicit_cross.is_err());

    config.target_configured = true;
    auto explicit_cross      = tenon::resolve_external_dependencies(declarations,
                                                                    config,
                                                                    fixture_cmake(),
                                                                    configuration(),
                                                                    target,
                                                                    "aarch64-unknown-linux-gnu"_str,
                                                                    *parser);
    EXPECT_TRUE(explicit_cross.is_ok());

    config.executable     = rstd::path::PathBuf::from("tenon-missing-pkg-config-provider"_str);
    auto missing_provider = tenon::resolve_external_dependencies(declarations,
                                                                 config,
                                                                 fixture_cmake(),
                                                                 configuration(),
                                                                 target,
                                                                 target.triple.as_str(),
                                                                 *parser);
    ASSERT_TRUE(missing_provider.is_err());
    auto provider_error = rstd::move(missing_provider).unwrap_err();
    EXPECT_TRUE(provider_error.message.as_str().contains("fixture"_str));
    EXPECT_TRUE(provider_error.message.as_str().contains("tenon-fixture"_str));

    config                       = fixture_pkg_config();
    declarations[usize {}].alias = String::make("missing-module"_str);
    declarations[usize {}].requirement.as_PkgConfig().requirement.module =
        String::make("tenon-module-does-not-exist"_str);
    auto missing_module = tenon::resolve_external_dependencies(declarations,
                                                               config,
                                                               fixture_cmake(),
                                                               configuration(),
                                                               target,
                                                               target.triple.as_str(),
                                                               *parser);
    ASSERT_TRUE(missing_module.is_err());
    auto module_error = rstd::move(missing_module).unwrap_err();
    EXPECT_TRUE(module_error.message.as_str().contains("missing-module"_str));
    EXPECT_TRUE(module_error.message.as_str().contains("tenon-module-does-not-exist"_str));
}

TEST(Contracts, PkgConfigProviderCachesEquivalentQueriesWithinResolution) {
    auto parser = tenon::make_clang_cpp_argument_parser();
    ASSERT_TRUE(parser.is_ok());
    auto directory = output_root("pkg-config-counting-provider"_str);
    ASSERT_TRUE(clear_output(directory.as_path()));
    ASSERT_TRUE(rstd::fs::create_dir_all(directory.as_path()).is_ok());
    auto config = tenon::PkgConfigProviderConfig {
        .executable        = root("pkg-config/counting-provider"_str),
        .sysroot           = Some(directory.clone()),
        .target_configured = true,
    };
    auto target       = pkg_config_target();
    auto declarations = Vec<tenon::DeclaredExternalDependency>::make();
    declarations.push(
        versioned_fixture("first"_str, tenon::PkgConfigVersionOperator::GreaterEqual, "1.0.0"_str));
    declarations.push(versioned_fixture(
        "second"_str, tenon::PkgConfigVersionOperator::GreaterEqual, "1.0.0"_str));
    auto resolved = tenon::resolve_external_dependencies(declarations,
                                                         config,
                                                         fixture_cmake(),
                                                         configuration(),
                                                         target,
                                                         target.triple.as_str(),
                                                         *parser);
    ASSERT_TRUE(resolved.is_ok());
    ASSERT_EQ(resolved->len(), usize(2));
    auto count = rstd::fs::read_to_string(
        directory.join(rstd::path::PathBuf::from("provider-count"_str).as_path()).as_path());
    ASSERT_TRUE(count.is_ok());
    EXPECT_EQ(count->as_str(), "4\n"_str);
    EXPECT_TRUE(clear_output(directory.as_path()));
}

TEST(Contracts, ExternalUsageSeparatesCompileVisibilityFromStaticLinkClosure) {
    auto parser = tenon::make_clang_cpp_argument_parser();
    ASSERT_TRUE(parser.is_ok());

    auto private_metadata = external_usage_metadata(tenon::DependencyVisibility::Private, *parser);
    ASSERT_TRUE(private_metadata.is_ok());
    auto private_plan =
        tenon::resolve_source_discovery(*private_metadata, "debug"_str, Vec<String>::make());
    ASSERT_TRUE(private_plan.is_ok());
    EXPECT_TRUE(has_external_macro(private_plan->contexts[usize {}]));
    EXPECT_FALSE(has_external_macro(private_plan->contexts[usize(1)]));
    ASSERT_EQ(private_plan->link_inputs[usize(1)].len(), usize(2));
    EXPECT_TRUE(private_plan->link_inputs[usize(1)][usize {}].is_Target());
    EXPECT_TRUE(private_plan->link_inputs[usize(1)][usize(1)].is_External());

    auto public_metadata = external_usage_metadata(tenon::DependencyVisibility::Public, *parser);
    ASSERT_TRUE(public_metadata.is_ok());
    auto public_plan =
        tenon::resolve_source_discovery(*public_metadata, "debug"_str, Vec<String>::make());
    ASSERT_TRUE(public_plan.is_ok());
    EXPECT_TRUE(has_external_macro(public_plan->contexts[usize {}]));
    EXPECT_TRUE(has_external_macro(public_plan->contexts[usize(1)]));

    auto runtime_metadata = external_usage_metadata(tenon::DependencyVisibility::Runtime, *parser);
    ASSERT_TRUE(runtime_metadata.is_ok());
    auto runtime_plan =
        tenon::resolve_source_discovery(*runtime_metadata, "debug"_str, Vec<String>::make());
    ASSERT_TRUE(runtime_plan.is_ok());
    EXPECT_FALSE(has_external_macro(runtime_plan->contexts[usize {}]));
    EXPECT_FALSE(has_external_macro(runtime_plan->contexts[usize(1)]));
    ASSERT_EQ(runtime_plan->link_inputs[usize(1)].len(), usize(2));
    EXPECT_TRUE(runtime_plan->link_inputs[usize(1)][usize(1)].is_External());
}

TEST(Contracts, InvalidDependencyGraphsAreRejectedByResolverOwner) {
    for (const auto path : INVALID_GRAPHS) {
        auto resolved = tenon::resolve_package_graph(root(path).as_path());
        if (resolved.is_ok()) rstd::io::eprintln("unexpected valid graph: {}", path);
        EXPECT_TRUE(resolved.is_err());
    }
}

TEST(Contracts, ProjectNameComesFromRootManifest) {
    auto workspace = tenon::resolve_package_graph(root("../demo/workspace"_str).as_path());
    ASSERT_TRUE(workspace.is_ok());
    EXPECT_TRUE(workspace->root_is_workspace);
    EXPECT_EQ(workspace->name.as_str(), "demo-workspace"_str);

    auto workspace_member =
        tenon::resolve_package_graph(root("../demo/workspace/app-one"_str).as_path());
    ASSERT_TRUE(workspace_member.is_ok());
    EXPECT_TRUE(workspace_member->root_is_workspace);
    EXPECT_EQ(workspace_member->name.as_str(), "demo-workspace"_str);

    auto package =
        tenon::resolve_package_graph(root("../demo/module-convention/demo-app"_str).as_path());
    ASSERT_TRUE(package.is_ok());
    EXPECT_FALSE(package->root_is_workspace);
    EXPECT_EQ(package->name.as_str(), "demo-app"_str);
}

TEST(Contracts, WorkspaceNameIsRequiredAndValidatedByManifestOwner) {
    auto missing = tenon::load_manifest_document(root("workspace/name-missing"_str).as_path());
    ASSERT_TRUE(missing.is_err());
    EXPECT_TRUE(missing.unwrap_err().message.as_str().contains("missing 'name'"_str));

    auto invalid = tenon::load_manifest_document(root("workspace/name-invalid"_str).as_path());
    ASSERT_TRUE(invalid.is_err());
    EXPECT_TRUE(invalid.unwrap_err().message.as_str().contains("workspace.name"_str));

    auto valid = tenon::load_manifest_document(root("../demo/workspace"_str).as_path());
    ASSERT_TRUE(valid.is_ok());
    ASSERT_TRUE(valid->workspace.is_some());
    EXPECT_EQ(valid->workspace->name.as_str(), "demo-workspace"_str);
}

TEST(Contracts, InvalidExplicitSourcesAreRejectedByDiscoveryOwner) {
    for (const auto path : INVALID_EXPLICIT_SOURCES) {
        auto loaded = tenon::load_package_manifest(root(path).as_path());
        ASSERT_TRUE(loaded.is_ok());
        auto discovered = tenon::discover_explicit_sources(*loaded);
        if (discovered.is_ok()) rstd::io::eprintln("unexpected valid sources: {}", path);
        EXPECT_TRUE(discovered.is_err());
    }
}

TEST(Contracts, TestAttachmentRequiresADirectLibraryDependency) {
    auto directory = root("manifest/test-attach-not-direct"_str);
    auto output    = output_root("test-attach-not-direct"_str);
    auto tested    = tenon::test(tenon::TestRequest {
        .build  = build_request(directory.as_path(), output.as_path(), Vec<String>::make()),
        .no_run = true,
    });
    ASSERT_TRUE(tested.is_err());
    EXPECT_TRUE(tested.unwrap_err().message.as_str().contains("direct dependency"_str));
    EXPECT_TRUE(clear_output(output.as_path()));
}

TEST(Contracts, LockValidationAndMigrationAreOwnedByLockStore) {
    EXPECT_TRUE(locked_graph_is_current("lock/default-update"_str));
    for (const auto path : INVALID_LOCKS) {
        auto current = locked_graph_is_current(path);
        if (current) rstd::io::eprintln("unexpected current lock: {}", path);
        EXPECT_FALSE(current);
    }
    EXPECT_TRUE(tenon::load_lock_session(root("lock/v3"_str).as_path(), false).is_ok());
}

TEST(Contracts, DiscoveryAndModuleConventionsBuildExpectedCases) {
    auto output = output_root("contracts-build"_str);
    ASSERT_TRUE(clear_output(output.as_path()));
    for (const auto path : VALID_BUILD_CASES) {
        auto directory = root(path);
        auto built =
            tenon::build(build_request(directory.as_path(), output.as_path(), Vec<String>::make()));
        if (built.is_err()) rstd::io::eprintln("unexpected build failure: {}", path);
        EXPECT_TRUE(built.is_ok());
    }
    for (const auto path : INVALID_BUILD_CASES) {
        auto directory = root(path);
        auto built =
            tenon::build(build_request(directory.as_path(), output.as_path(), Vec<String>::make()));
        if (built.is_ok()) rstd::io::eprintln("unexpected build success: {}", path);
        EXPECT_TRUE(built.is_err());
    }
    EXPECT_TRUE(clear_output(output.as_path()));
}

TEST(Contracts, BuildProfileOwnsOptimizationAndDebugDefinitions) {
    auto directory = root("profile"_str);
    auto output    = output_root("profile"_str);
    ASSERT_TRUE(clear_output(output.as_path()));

    auto debug =
        tenon::build(build_request(directory.as_path(), output.as_path(), Vec<String>::make()));
    ASSERT_TRUE(debug.is_ok());
    auto debug_executable = executable(*debug);
    ASSERT_TRUE(debug_executable.is_some());
    auto debug_status = rstd::process::Command::make((*debug_executable).as_os_str())
                            .current_dir(directory.as_path())
                            .status();
    ASSERT_TRUE(debug_status.is_ok());
    ASSERT_TRUE(debug_status->code().is_some());
    EXPECT_EQ(*debug_status->code(), i32(1));

    auto release = tenon::build(build_request(
        directory.as_path(), output.as_path(), Vec<String>::make(), tenon::BuildProfile::Release));
    ASSERT_TRUE(release.is_ok());
    auto release_executable = executable(*release);
    ASSERT_TRUE(release_executable.is_some());
    auto release_status = rstd::process::Command::make((*release_executable).as_os_str())
                              .current_dir(directory.as_path())
                              .status();
    ASSERT_TRUE(release_status.is_ok());
    EXPECT_TRUE(release_status->success());
    EXPECT_TRUE(clear_output(output.as_path()));
}
