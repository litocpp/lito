#include <rstd/test/gtest.hpp>
#include <rstd/macro.hpp>

import rstd;
import rstd.test;
import lito;
import lito.process;
import lito.test.support;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito_test;

namespace
{

inline constexpr ref<str> INVALID_MANIFESTS[] = {
    "manifest/empty-target-array"_str,
    "manifest/git/commit-invalid"_str,
    "manifest/git/multiple-selectors"_str,
    "manifest/git/path-and-git"_str,
    "manifest/git/url-fragment"_str,
    "manifest/legacy-target-schema"_str,
    "manifest/package-name-dot"_str,
    "manifest/package-name-empty"_str,
    "manifest/package-version-missing"_str,
    "manifest/profile/cycle"_str,
    "manifest/profile/linker-owned"_str,
    "manifest/profile/nested"_str,
    "manifest/profile/type"_str,
    "manifest/public-usage-without-lib"_str,
    "manifest/source-root-descendant"_str,
    "manifest/target-link-stdlib-type"_str,
    "manifest/test-attach-unknown-key"_str,
    "manifest/toml-explicit/dependency"_str,
    "manifest/toml-explicit/version-workspace-false"_str,
    "profile/owned-definition"_str,
    "workspace/cmake-definition-targets"_str,
    "workspace/dependency-definition-visibility"_str,
    "workspace/dependency-reference-mixed"_str,
    "workspace/mixed"_str,
};

inline constexpr ref<str> INVALID_PKG_CONFIG_MANIFESTS[] = {
    "manifest/pkg-config/legacy-dependency"_str, "manifest/pkg-config/path-version"_str,
    "manifest/pkg-config/selector"_str,          "manifest/pkg-config/source-mix"_str,
    "manifest/pkg-config/static-type"_str,       "manifest/pkg-config/version"_str,
};

inline constexpr ref<str> INVALID_CMAKE_MANIFESTS[] = {
    "manifest/cmake/adapter-install"_str,      "manifest/cmake/archive-missing-sha"_str,
    "manifest/cmake/build-tree-installed"_str, "manifest/cmake/config-directory-parent"_str,
    "manifest/cmake/duplicate-target"_str,     "manifest/cmake/empty-targets"_str,
    "manifest/cmake/installed-cache"_str,      "manifest/cmake/installed-config-directory"_str,
    "manifest/cmake/legacy-dependency"_str,    "manifest/cmake/missing-target"_str,
    "manifest/cmake/provider-mix"_str,         "manifest/cmake/unsafe-target"_str,
};

inline constexpr ref<str> INVALID_CMAKE_ARCHITECTURE_ARCHIVE_MANIFESTS[] = {
    "manifest/cmake/archives-empty"_str,
    "manifest/cmake/archives-git-mix"_str,
    "manifest/cmake/archives-install"_str,
    "manifest/cmake/archives-invalid-sha"_str,
    "manifest/cmake/archives-invalid-url"_str,
    "manifest/cmake/archives-missing-archive"_str,
    "manifest/cmake/archives-missing-sha"_str,
    "manifest/cmake/archives-noncanonical-architecture"_str,
    "manifest/cmake/archives-path-mix"_str,
    "manifest/cmake/archives-source-mix"_str,
    "manifest/cmake/archives-unknown-field"_str,
    "manifest/cmake/archives-unsafe-architecture"_str,
};

inline constexpr ref<str> INVALID_EXPLICIT_SOURCES[] = {
    "manifest/toml-explicit/duplicate"_str,
    "manifest/toml-explicit/missing"_str,
    "manifest/toml-explicit/outside-root"_str,
    "manifest/toml-explicit/unsupported"_str,
};

inline constexpr ref<str> INVALID_GRAPHS[] = {
    "conventional-test/invalid-kind"_str,
    "conventional-test/invalid-name"_str,
    "conventional-test/invalid-overlap"_str,
    "conventional-test/invalid-profile"_str,
    "conventional-test/invalid-version"_str,
    "conventional-test/invalid-workspace-kind"_str,
    "resolver/cycle/a"_str,
    "resolver/missing"_str,
    "resolver/name-mismatch/root"_str,
    "resolver/same-name/root"_str,
    "workspace/default-not-member"_str,
    "workspace/duplicate-name"_str,
    "workspace/duplicate"_str,
    "workspace/inherited-dependency-missing"_str,
    "workspace/inherited-dependency-outside"_str,
    "workspace/inherited-version-missing"_str,
    "workspace/member-profile"_str,
    "workspace/missing-member"_str,
    "workspace/nested"_str,
    "workspace/outside"_str,
};

inline constexpr ref<str> INVALID_LOCKS[] = {
    "lock/invalid"_str,
    "lock/missing"_str,
    "lock/stale"_str,
    "lock/v3"_str,
    "lock/v4-commit-mismatch"_str,
    "lock/v4-dangling"_str,
    "lock/v4-duplicate-name"_str,
    "lock/v4-duplicate-source"_str,
    "lock/v4-id-field"_str,
};

inline constexpr ref<str> VALID_BUILD_CASES[] = {
    "cache/long-path"_str,
    "discovery/preprocess/app"_str,
    "manifest/multiple-primary-modules"_str,
    "manifest/toml-module/directory-markers"_str,
    "manifest/toml-module/multiple-implementations"_str,
    "scanner/module-kinds"_str,
    "scanner/stdlib-header"_str,
    "workspace/shared-source-root"_str,
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
    auto session   = lito::load_lock_session(directory.as_path(), true);
    if (session.is_err()) return false;
    auto options = session->take_resolution_options();
    auto graph   = lito::resolve_package_graph(directory.as_path(), rstd::move(options));
    if (graph.is_err()) return false;
    return lito::sync_lock(*graph, rstd::move(session).unwrap()).is_ok();
}

auto executable(const lito::BuildSummary& summary) -> Option<ref<rstd::path::Path>> {
    for (const auto& artifact : summary.artifacts) {
        if (artifact.kind == lito::ArtifactKind::Executable) {
            return Some(artifact.path.as_path());
        }
    }
    return None();
}

auto project_root_role(const lito::ResolvedPackageGraph& graph, ref<str> name)
    -> Option<lito::ProjectRootRole> {
    for (const auto& root : graph.roots) {
        if (root.name.as_str() == name) {
            auto role = root.role;
            return Some(role);
        }
    }
    return None();
}

auto contains_name(const Vec<String>& names, ref<str> name) -> bool {
    for (const auto& candidate : names) {
        if (candidate.as_str() == name) return true;
    }
    return false;
}

auto has_external_macro(const lito::CompileContext& context) -> bool {
    for (const auto& macro : context.cpp.preprocessor.macros) {
        if (macro.value.as_str() == "LITO_EXTERNAL_USAGE=1"_str) return true;
    }
    return false;
}

auto pkg_config_target() -> lito::TargetInfo {
    return lito::TargetInfo {
        .triple = String::make("x86_64-unknown-linux-gnu"_str),
        .architecture =
            lito::Architecture {
                .name = String::make("x86_64"_str),
            },
        .os     = String::make("linux"_str),
        .family = lito::TargetFamily::Unix,
    };
}

auto native_platform() -> lito::BuildPlatform {
    auto target   = pkg_config_target();
    auto resolved = lito::resolve_build_platform(
        lito::HostInfo {
            .architecture = target.architecture.clone(),
            .os           = target.os.clone(),
        },
        target,
        None());
    return rstd::move(resolved).unwrap();
}

auto explicit_platform(ref<str> target_triple) -> lito::BuildPlatform {
    auto target   = pkg_config_target();
    auto resolved = lito::resolve_build_platform(
        lito::HostInfo {
            .architecture = target.architecture.clone(),
            .os           = target.os.clone(),
        },
        target,
        Some(target_triple));
    return rstd::move(resolved).unwrap();
}

auto default_profile(const lito::CppArgumentParser& parser) -> lito::ProfileSpec {
    auto profile = lito::make_profile_spec(
        configuration(), lito::ProjectProfile {}, build_profile("debug"_str), parser);
    return rstd::move(profile).unwrap();
}

auto fixture_pkg_config() -> lito::PkgConfigProviderConfig {
    auto library_paths = Vec<rstd::path::PathBuf>::make();
    library_paths.push(root("pkg-config"_str));
    return lito::PkgConfigProviderConfig {
        .executable    = rstd::path::PathBuf::from("pkg-config"_str),
        .library_paths = rstd::move(library_paths),
    };
}

auto fixture_cmake() -> lito::CMakeProviderConfig {
    return lito::CMakeProviderConfig {
        .executable = rstd::path::PathBuf::from("cmake"_str),
        .generator  = String::make("Ninja"_str),
    };
}

auto resolve_cmake_fixtures(const Vec<lito::PreparedCMakeDependencyRequirement>& declarations,
                            const lito::ProfileSpec&                             profile,
                            const lito::BuildPlatform&                           platform,
                            const lito::CppArgumentParser&                       parser,
                            usize                                                jobs = usize(1))
    -> lito::Result<Vec<lito::ResolvedExternalDependency>> {
    auto environment = lito::ResolvedProcessEnvironment::resolve(lito::ProcessEnvironmentSpec {});
    if (environment.is_err()) return Err(rstd::move(environment).unwrap_err());
    auto resolver = lito::ToolResolver(*environment);
    auto provider = fixture_cmake();
    auto tool     = resolver.resolve(provider.executable.as_path(), "CMake executable"_str);
    if (tool.is_err()) return Err(rstd::move(tool).unwrap_err());
    provider.executable = rstd::move(tool).unwrap().executable;
    auto identified     = lito::identify_cmake_provider(rstd::move(provider), *environment);
    if (identified.is_err()) return Err(rstd::move(identified).unwrap_err());
    provider    = rstd::move(identified).unwrap();
    auto result = Vec<lito::ResolvedExternalDependency>::make();
    for (const auto& declaration : declarations) {
        auto requirement = lito::resolve_cmake_requirement_for_platform(declaration, platform);
        if (requirement.is_err()) return Err(rstd::move(requirement).unwrap_err());
        if (requirement->adapter.is_some() && requirement->adapter_identity.is_empty()) {
            auto contents = rstd::fs::read_to_string(requirement->adapter->as_path());
            if (contents.is_err()) {
                return Err(lito::Error::make(lito::ErrorKind::Dependency,
                                             rstd::format("cannot read CMake adapter '{}': {}",
                                                          requirement->adapter->as_path(),
                                                          rstd::move(contents).unwrap_err())));
            }
            requirement->adapter_identity =
                rstd::format("{}\n{}", requirement->adapter->as_path(), contents->as_str());
        }
        auto plan = lito::plan_cmake_package(*requirement,
                                             provider,
                                             configuration(),
                                             profile,
                                             platform.compiler_default,
                                             platform.effective_target.triple.as_str(),
                                             jobs);
        if (plan.is_err()) return Err(rstd::move(plan).unwrap_err());
        auto snapshot = lito::execute_cmake_package(*plan, *environment);
        if (snapshot.is_err()) return Err(rstd::move(snapshot).unwrap_err());
        auto usage = lito::materialize_cmake_usage(*plan, *snapshot, parser);
        if (usage.is_err()) return Err(rstd::move(usage).unwrap_err());
        result.push(rstd::move(usage).unwrap());
    }
    return Ok(rstd::move(result));
}

auto git_revision(ref<rstd::path::Path> repository, ref<str> revision) -> Option<String> {
    auto command = rstd::process::Command::make("git"_str);
    command.arg("-C"_str).arg(repository.as_os_str()).arg("rev-parse"_str).arg(revision);
    auto output = command.output();
    if (output.is_err() || ! output->status.success()) return None();
    auto text = String::from_utf8(rstd::move(output->stdout_buf));
    if (text.is_err()) return None();
    return Some(String::make(text->as_str().trim_ascii()));
}

template<typename... Arguments>
auto git_succeeds(ref<rstd::path::Path> repository, Arguments... arguments) -> bool {
    auto command = rstd::process::Command::make("git"_str);
    (command.arg(arguments), ...);
    command.current_dir(repository)
        .set_stdout(rstd::process::Stdio::null())
        .set_stderr(rstd::process::Stdio::null());
    auto status = command.status();
    return status.is_ok() && status->success();
}

auto external_git_graph(ref<str> url, lito::GitReference reference) -> lito::ResolvedPackageGraph {
    auto declarations = Vec<lito::CMakeDependencyRequirement>::make();
    declarations.push(lito::CMakeDependencyRequirement {
        .alias   = String::make("fixture"_str),
        .package = String::make("Fixture"_str),
        .source  = lito::CMakeDependencySource::Git(String::make(url), rstd::move(reference)),
    });
    auto packages = Vec<lito::ResolvedPackage>::make();
    packages.push(lito::ResolvedPackage {
        .manifest =
            lito::PackageManifest {
                .name                        = String::make("fixture-root"_str),
                .cmake_external_dependencies = rstd::move(declarations),
            },
    });
    return lito::ResolvedPackageGraph {
        .root_directory = root("lock/git-update"_str),
        .packages       = rstd::move(packages),
    };
}

auto resolved_git_commit(const lito::ResolvedPackageGraph& graph) -> Option<ref<str>> {
    for (const auto& source : graph.sources) {
        if (source.kind == lito::PackageSourceKind::Git) return Some(source.commit.as_str());
    }
    return None();
}

auto write_executable(ref<rstd::path::Path> path) -> bool {
    if (rstd::fs::write(path, "fixture\n"_str.as_bytes()).is_err()) return false;
#if RSTD_OS_UNIX
    return rstd::fs::set_permissions(path, rstd::fs::Permissions::from_mode(u32(0755))).is_ok();
#else
    return true;
#endif
}

auto versioned_fixture(ref<str>                       alias,
                       lito::PkgConfigVersionOperator comparison,
                       ref<str>                       version,
                       lito::PkgConfigQueryMode       mode = lito::PkgConfigQueryMode::Shared)
    -> lito::PkgConfigExternalDependency {
    return lito::PkgConfigExternalDependency {
        .alias = String::make(alias),
        .requirement =
            lito::PkgConfigDependencyRequirement {
                .module  = String::make("lito-fixture"_str),
                .version = Some(lito::PkgConfigVersionRequirement {
                    .comparison = comparison,
                    .value      = String::make(version),
                }),
                .mode    = mode,
            },
    };
}

auto external_usage_metadata(lito::DependencyVisibility     visibility,
                             const lito::CppArgumentParser& parser)
    -> lito::Result<lito::PackageMetadata> {
    auto raw       = strings("-DLITO_EXTERNAL_USAGE=1"_str);
    auto arguments = parser.parse(raw, "pkg-config test fixture"_str);
    if (arguments.is_err()) {
        return Err(
            lito::Error::make(lito::ErrorKind::Dependency, rstd::move(arguments).unwrap_err()));
    }
    auto external         = Vec<lito::ResolvedExternalDependency>::make();
    auto external_targets = Vec<lito::ResolvedExternalTargetUsage>::make();
    external_targets.push(lito::ResolvedExternalTargetUsage {
        .name              = String::make("lito-fixture"_str),
        .visibility        = visibility,
        .compile_arguments = rstd::move(arguments).unwrap(),
        .identity          = String::make("fixture-resolution-v1"_str),
    });
    external.push(lito::ResolvedExternalDependency {
        .alias    = String::make("fixture"_str),
        .provider = String::make("pkg-config"_str),
        .version  = String::make("2.3.4"_str),
        .targets  = rstd::move(external_targets),
        .link_arguments =
            lito::LinkArgumentSequence {
                .tokens   = strings("-llito_fixture"_str),
                .source   = String::make("pkg-config fixture"_str),
                .identity = String::make("fixture-link-v1"_str),
            },
        .identity = String::make("fixture-resolution-v1"_str),
    });
    auto dependencies = Vec<lito::DependencySpec>::make();
    dependencies.push(lito::DependencySpec {
        .target =
            lito::PackageTargetId {
                .package = String::make("external-usage"_str),
                .kind    = lito::PackageTargetKind::Library,
                .name    = String::make("library"_str),
            },
        .visibility = lito::DependencyVisibility::Private,
    });
    auto targets = Vec<lito::ResolvedTarget>::make();
    targets.push(lito::ResolvedTarget {
        .id =
            lito::PackageTargetId {
                .package = String::make("external-usage"_str),
                .kind    = lito::PackageTargetKind::Library,
                .name    = String::make("library"_str),
            },
        .artifact_kind         = lito::ArtifactKind::StaticLibrary,
        .artifact_name         = String::make("library"_str),
        .external_dependencies = rstd::move(external),
    });
    targets.push(lito::ResolvedTarget {
        .id =
            lito::PackageTargetId {
                .package = String::make("external-usage"_str),
                .kind    = lito::PackageTargetKind::Binary,
                .name    = String::make("app"_str),
            },
        .artifact_kind = lito::ArtifactKind::Executable,
        .artifact_name = String::make("app"_str),
        .dependencies  = rstd::move(dependencies),
    });
    auto default_targets = Vec<lito::PackageTargetId>::make();
    default_targets.push(targets[usize(1)].id.clone());
    auto profiles = Vec<lito::ProfileSpec>::make();
    profiles.push(default_profile(parser));
    return Ok(lito::PackageMetadata {
        .name            = String::make("external-usage"_str),
        .default_profile = String::make("debug"_str),
        .default_targets = rstd::move(default_targets),
        .profiles        = rstd::move(profiles),
        .targets         = rstd::move(targets),
    });
}

} // namespace

TEST(Contracts, InvalidManifestDocumentsAreRejectedByManifestOwner) {
    for (const auto path : INVALID_MANIFESTS) {
        auto loaded = lito::load_manifest_document(root(path).as_path());
        if (loaded.is_ok()) rstd::io::eprintln("unexpected valid manifest: {}", path);
        EXPECT_TRUE(loaded.is_err());
    }
}

TEST(Contracts, PackageManifestOwnsTypedTargetCollection) {
    auto loaded = lito::load_package_manifest(root("project/multi-target"_str).as_path());
    ASSERT_TRUE(loaded.is_ok());
    ASSERT_EQ(loaded->targets.len(), usize(7));

    auto libraries  = usize {};
    auto binaries   = usize {};
    auto tests      = usize {};
    auto benchmarks = usize {};
    auto no_stdlib  = usize {};
    for (const auto& target : loaded->targets) {
        switch (lito::package_target_kind(target)) {
        case lito::PackageTargetKind::Library: ++libraries; break;
        case lito::PackageTargetKind::Binary: ++binaries; break;
        case lito::PackageTargetKind::Test: ++tests; break;
        case lito::PackageTargetKind::Benchmark: ++benchmarks; break;
        case lito::PackageTargetKind::TestAttachment:
        case lito::PackageTargetKind::CompileTest: break;
        }
        if (! lito::package_target_links_stdlib(target)) ++no_stdlib;
    }
    EXPECT_EQ(libraries, usize(1));
    EXPECT_EQ(binaries, usize(2));
    EXPECT_EQ(tests, usize(2));
    EXPECT_EQ(benchmarks, usize(2));
    EXPECT_EQ(no_stdlib, usize(3));
}

TEST(Contracts, ProjectProfileDefaultsAndRootOwnershipAreTyped) {
    auto defaults = lito::resolve_package_graph(root("profile"_str).as_path());
    ASSERT_TRUE(defaults.is_ok());
    EXPECT_TRUE(defaults->profile.exceptions);
    EXPECT_TRUE(defaults->profile.rtti);

    auto disabled = lito::resolve_package_graph(root("profile/disabled"_str).as_path());
    ASSERT_TRUE(disabled.is_ok());
    EXPECT_FALSE(disabled->profile.exceptions);
    EXPECT_FALSE(disabled->profile.rtti);

    auto workspace = lito::resolve_package_graph(root("workspace/profile"_str).as_path());
    ASSERT_TRUE(workspace.is_ok());
    EXPECT_TRUE(workspace->profile.exceptions);
    EXPECT_FALSE(workspace->profile.rtti);

    auto dependency = lito::resolve_package_graph(root("profile/dependency/root"_str).as_path());
    ASSERT_TRUE(dependency.is_ok());
    EXPECT_TRUE(dependency->profile.exceptions);
    EXPECT_TRUE(dependency->profile.rtti);
}

TEST(Contracts, ProjectProfileMaterializesIdenticalLanguageSemanticsAcrossBuildModes) {
    auto parser = lito::make_clang_cpp_argument_parser();
    ASSERT_TRUE(parser.is_ok());
    auto project = lito::ProjectProfile {
        .exceptions = false,
        .rtti       = false,
    };
    auto debug =
        lito::make_profile_spec(configuration(), project, build_profile("debug"_str), *parser);
    auto release =
        lito::make_profile_spec(configuration(), project, build_profile("release"_str), *parser);
    ASSERT_TRUE(debug.is_ok());
    ASSERT_TRUE(release.is_ok());
    EXPECT_FALSE(debug->cpp.language.exceptions);
    EXPECT_FALSE(debug->cpp.language.rtti);
    EXPECT_FALSE(release->cpp.language.exceptions);
    EXPECT_FALSE(release->cpp.language.rtti);
}

TEST(Contracts, BuildProfilesResolveCargoStyleValuesAndInheritance) {
    auto graph = lito::resolve_package_graph(root("profile"_str).as_path());
    ASSERT_TRUE(graph.is_ok());

    auto perf = lito::resolve_build_profile(graph->profile, build_profile("perf"_str));
    ASSERT_TRUE(perf.is_ok());
    EXPECT_EQ(perf->family, lito::BuildProfileFamily::Release);
    EXPECT_EQ(perf->optimization, lito::CppOptimization::Level2);
    EXPECT_EQ(perf->debug_info, lito::CppDebugInfo::LineTablesOnly);
    EXPECT_EQ(perf->strip, lito::StripMode::None);
    EXPECT_EQ(perf->lto, lito::CppLto::Off);
    EXPECT_TRUE(perf->ndebug);

    auto aliases = lito::resolve_build_profile(graph->profile, build_profile("aliases"_str));
    ASSERT_TRUE(aliases.is_ok());
    EXPECT_EQ(aliases->optimization, lito::CppOptimization::SizeMin);
    EXPECT_EQ(aliases->debug_info, lito::CppDebugInfo::Limited);
    EXPECT_EQ(aliases->strip, lito::StripMode::Symbols);
    EXPECT_EQ(aliases->lto, lito::CppLto::Fat);
}

TEST(Contracts, BuildProfileCatalogRejectsUnknownParentsAndCycles) {
    auto unknown = lito::ProjectProfile {};
    unknown.build_profiles.push(lito::BuildProfileDefinition {
        .name     = build_profile("custom"_str),
        .inherits = Some(build_profile("missing"_str)),
    });
    auto unknown_result = lito::validate_build_profiles(unknown);
    ASSERT_TRUE(unknown_result.is_err());
    EXPECT_TRUE(unknown_result.unwrap_err().message.as_str().contains("unknown profile"_str));

    auto cycle = lito::ProjectProfile {};
    cycle.build_profiles.push(lito::BuildProfileDefinition {
        .name     = build_profile("first"_str),
        .inherits = Some(build_profile("second"_str)),
    });
    cycle.build_profiles.push(lito::BuildProfileDefinition {
        .name     = build_profile("second"_str),
        .inherits = Some(build_profile("first"_str)),
    });
    auto cycle_result = lito::validate_build_profiles(cycle);
    ASSERT_TRUE(cycle_result.is_err());
    EXPECT_TRUE(cycle_result.unwrap_err().message.as_str().contains("inheritance cycle"_str));
}

TEST(Contracts, CodegenProfilesMaterializeTypedClangOptionsAndCacheIdentities) {
    EXPECT_EQ(lito::cpp_optimization_option(lito::CppOptimization::Level2), "-O2"_str);
    EXPECT_EQ(lito::cpp_optimization_option(lito::CppOptimization::Size), "-Os"_str);
    EXPECT_EQ(lito::cpp_optimization_option(lito::CppOptimization::SizeMin), "-Oz"_str);
    EXPECT_EQ(lito::cpp_debug_option(lito::CppDebugInfo::LineDirectivesOnly),
              "-gline-directives-only"_str);
    EXPECT_EQ(lito::cpp_debug_option(lito::CppDebugInfo::LineTablesOnly), "-gline-tables-only"_str);
    EXPECT_EQ(lito::cpp_debug_option(lito::CppDebugInfo::Limited), "-g1"_str);
    EXPECT_EQ(lito::cpp_lto_option(lito::CppLto::Off), "-fno-lto"_str);
    EXPECT_EQ(lito::cpp_lto_option(lito::CppLto::Thin), "-flto=thin"_str);
    EXPECT_EQ(lito::cpp_lto_option(lito::CppLto::Fat), "-flto=full"_str);

    auto graph = lito::resolve_package_graph(root("profile"_str).as_path());
    ASSERT_TRUE(graph.is_ok());
    auto parser = lito::make_clang_cpp_argument_parser();
    ASSERT_TRUE(parser.is_ok());
    auto perf = lito::make_profile_spec(
        configuration(), graph->profile, build_profile("perf"_str), *parser);
    auto variant = lito::make_profile_spec(
        configuration(), graph->profile, build_profile("codegen-variant"_str), *parser);
    ASSERT_TRUE(perf.is_ok());
    ASSERT_TRUE(variant.is_ok());
    EXPECT_NE(lito::cpp_compile_identity(perf->cpp).as_str(),
              lito::cpp_compile_identity(variant->cpp).as_str());
    EXPECT_EQ(lito::cpp_scan_identity(perf->cpp).as_str(),
              lito::cpp_scan_identity(variant->cpp).as_str());
    EXPECT_EQ(lito::cpp_bmi_compatibility_identity(perf->cpp).as_str(),
              lito::cpp_bmi_compatibility_identity(variant->cpp).as_str());

    auto aliases = lito::make_profile_spec(
        configuration(), graph->profile, build_profile("aliases"_str), *parser);
    ASSERT_TRUE(aliases.is_ok());
    EXPECT_NE(lito::cpp_scan_identity(perf->cpp).as_str(),
              lito::cpp_scan_identity(aliases->cpp).as_str());
}

TEST(Contracts, RawCompilerAndLinkerOptionsCannotOverrideOwnedSettings) {
    auto parser = lito::make_clang_cpp_argument_parser();
    ASSERT_TRUE(parser.is_ok());

    auto compiler_configuration = configuration();
    compiler_configuration.options.push(String::make("-flto=auto"_str));
    auto compiler = lito::make_profile_spec(
        compiler_configuration, lito::ProjectProfile {}, build_profile("debug"_str), *parser);
    ASSERT_TRUE(compiler.is_err());
    EXPECT_TRUE(compiler.unwrap_err().message.as_str().contains("selected profile"_str));

    auto linker_configuration = configuration();
    linker_configuration.linker_options.push(String::make("-Wl,--strip-debug"_str));
    auto linker = lito::make_profile_spec(
        linker_configuration, lito::ProjectProfile {}, build_profile("debug"_str), *parser);
    ASSERT_TRUE(linker.is_err());
    EXPECT_TRUE(linker.unwrap_err().message.as_str().contains("selected profile"_str));

    auto stdlib_configuration = configuration();
    stdlib_configuration.linker_options.push(String::make("-nostdlib++"_str));
    auto stdlib = lito::make_profile_spec(
        stdlib_configuration, lito::ProjectProfile {}, build_profile("debug"_str), *parser);
    ASSERT_TRUE(stdlib.is_err());
    EXPECT_TRUE(stdlib.unwrap_err().message.as_str().contains("Lito-owned setting"_str));
}

TEST(Contracts, CompilerOptionsAreValidatedAfterToolchainParsing) {
    auto directory = root("profile/owned-option"_str);
    auto graph     = lito::resolve_package_graph(directory.as_path());
    ASSERT_TRUE(graph.is_ok());

    auto parser = lito::make_clang_cpp_argument_parser();
    ASSERT_TRUE(parser.is_ok());
    auto packages = strings("fixture-profile-owned_option"_str);
    auto targets  = Vec<lito::PackageTargetId>::make();
    targets.push(lito::PackageTargetId {
        .package = String::make("fixture-profile-owned_option"_str),
        .kind    = lito::PackageTargetKind::Binary,
        .name    = String::make("fixture-profile-owned_option"_str),
    });
    auto build_configuration = configuration();
    auto build_arguments     = lito::parse_build_arguments(build_configuration, *parser);
    ASSERT_TRUE(build_arguments.is_ok());
    auto profile = lito::make_profile_spec(build_configuration,
                                           graph->profile,
                                           build_profile("debug"_str),
                                           rstd::move(build_arguments).unwrap());
    ASSERT_TRUE(profile.is_ok());
    auto external_usage = lito::ExternalUsageCatalog {};
    external_usage.packages.push(lito::ExternalPackageUsage {
        .package = String::make("fixture-profile-owned_option"_str),
    });
    auto metadata = lito::adapt_package_graph_metadata(rstd::move(graph).unwrap(),
                                                       packages,
                                                       targets,
                                                       build_configuration,
                                                       rstd::move(profile).unwrap(),
                                                       native_platform(),
                                                       rstd::move(external_usage),
                                                       *parser);
    ASSERT_TRUE(metadata.is_ok());

    auto planned = lito::resolve_source_discovery(*metadata, "debug"_str, Vec<String>::make());
    ASSERT_TRUE(planned.is_err());
    EXPECT_TRUE(planned.unwrap_err().message.as_str().contains("optimization"_str));
}

TEST(Contracts, ManifestLocatorPrefersLitoAndAcceptsLegacyTenon) {
    auto legacy = lito::load_package_manifest(root("manifest/name/legacy"_str).as_path());
    ASSERT_TRUE(legacy.is_ok());
    EXPECT_EQ(legacy->name.as_str(), "legacy-manifest"_str);

    auto preferred = lito::load_package_manifest(root("manifest/name/preferred"_str).as_path());
    ASSERT_TRUE(preferred.is_ok());
    EXPECT_EQ(preferred->name.as_str(), "preferred-manifest"_str);
}

TEST(Contracts, PackageSourceRootIsIndependentFromWorkspaceMemberDirectory) {
    auto loaded = lito::load_package_manifest(
        root("workspace/shared-source-root/packages/library"_str).as_path());
    ASSERT_TRUE(loaded.is_ok());
    EXPECT_NE(loaded->root.as_path(), loaded->source_root.as_path());
    EXPECT_EQ(loaded->source_root.as_path(), root("workspace/shared-source-root"_str).as_path());
}

TEST(Contracts, ManifestGitCommitIsTypedAndValidated) {
    auto loaded = lito::load_package_manifest(root("manifest/git/commit"_str).as_path());
    ASSERT_TRUE(loaded.is_ok());
    ASSERT_EQ(loaded->dependencies.len(), usize(1));
    const auto& source = loaded->dependencies[usize {}].source;
    ASSERT_TRUE(source.is_Git());
    EXPECT_EQ(source.as_Git().reference.kind, lito::GitReferenceKind::Commit);
    EXPECT_EQ(source.as_Git().reference.value.as_str(),
              "0123456789abcdef0123456789abcdef01234567"_str);
}

TEST(Contracts, RemovedConfigFieldsAreRejectedByConfigOwner) {
    auto loaded = lito::load_project_config(root("config/removed-scanner"_str).as_path());
    EXPECT_TRUE(loaded.is_err());
}

TEST(Contracts, EnvironmentAppendPathBelongsToProjectConfig) {
    auto loaded = lito::load_project_config(root("config/environment-valid"_str).as_path());
    ASSERT_TRUE(loaded.is_ok());
    ASSERT_EQ(loaded->environment.append_path.len(), usize(2));
    EXPECT_EQ(loaded->environment.append_path[usize {}].as_path(),
              root("config/environment-valid"_str).as_path());
    EXPECT_EQ(loaded->environment.append_path[usize(1)].as_path(), root("config"_str).as_path());

    auto empty = lito::load_project_config(root("config/environment-empty"_str).as_path());
    ASSERT_TRUE(empty.is_ok());
    EXPECT_TRUE(empty->environment.append_path.is_empty());

    auto unconfigured = lito::load_project_config(root("config"_str).as_path());
    ASSERT_TRUE(unconfigured.is_ok());
    EXPECT_TRUE(unconfigured->environment.append_path.is_empty());
}

TEST(Contracts, InvalidEnvironmentAppendPathIsRejectedByConfigOwner) {
    constexpr ref<str> cases[] = {
        "config/environment-wrong-type"_str, "config/environment-empty-entry"_str,
        "config/environment-missing"_str,    "config/environment-file"_str,
        "config/environment-unknown"_str,
    };
    for (const auto path : cases) {
        EXPECT_TRUE(lito::load_project_config(root(path).as_path()).is_err());
    }
}

TEST(Contracts, ToolResolverUsesOneOrderedEffectivePathSnapshot) {
    auto directory = output_root("tool-resolver"_str);
    ASSERT_TRUE(clear_output(directory.as_path()));
    auto inherited = directory.join(rstd::path::PathBuf::from("inherited"_str).as_path());
    auto first     = directory.join(rstd::path::PathBuf::from("first"_str).as_path());
    auto second    = directory.join(rstd::path::PathBuf::from("second"_str).as_path());
    auto relative  = directory.join(rstd::path::PathBuf::from("relative"_str).as_path());
    ASSERT_TRUE(rstd::fs::create_dir_all(inherited.as_path()).is_ok());
    ASSERT_TRUE(rstd::fs::create_dir_all(first.as_path()).is_ok());
    ASSERT_TRUE(rstd::fs::create_dir_all(second.as_path()).is_ok());
    ASSERT_TRUE(rstd::fs::create_dir_all(relative.as_path()).is_ok());

    auto inherited_tool  = inherited.join(rstd::path::PathBuf::from("lito-tool"_str).as_path());
    auto appended_tool   = first.join(rstd::path::PathBuf::from("lito-tool"_str).as_path());
    auto first_fallback  = first.join(rstd::path::PathBuf::from("lito-fallback"_str).as_path());
    auto second_fallback = second.join(rstd::path::PathBuf::from("lito-fallback"_str).as_path());
    auto absolute_tool   = second.join(rstd::path::PathBuf::from("lito-absolute"_str).as_path());
    auto non_executable =
        first.join(rstd::path::PathBuf::from("lito-non-executable"_str).as_path());
    ASSERT_TRUE(write_executable(inherited_tool.as_path()));
    ASSERT_TRUE(write_executable(appended_tool.as_path()));
    ASSERT_TRUE(write_executable(first_fallback.as_path()));
    ASSERT_TRUE(write_executable(second_fallback.as_path()));
    ASSERT_TRUE(write_executable(absolute_tool.as_path()));
#if RSTD_OS_WINDOWS
    auto extension_tool =
        second.join(rstd::path::PathBuf::from("lito-extension.EXE"_str).as_path());
    ASSERT_TRUE(write_executable(extension_tool.as_path()));
#endif
    ASSERT_TRUE(rstd::fs::write(non_executable.as_path(), "fixture\n"_str.as_bytes()).is_ok());
#if RSTD_OS_UNIX
    ASSERT_TRUE(rstd::fs::set_permissions(non_executable.as_path(),
                                          rstd::fs::Permissions::from_mode(u32(0644)))
                    .is_ok());
#endif

    auto inherited_entries = Vec<rstd::path::PathBuf>::make();
    inherited_entries.push(inherited.clone());
    inherited_entries.push(rstd::path::PathBuf::make());
    inherited_entries.push(rstd::path::PathBuf::from("relative"_str));
    auto inherited_path = rstd::env::join_paths(inherited_entries.as_slice());
    ASSERT_TRUE(inherited_path.is_ok());
    auto append = Vec<rstd::path::PathBuf>::make();
    append.push(first.clone());
    append.push(second.clone());
    auto environment = lito::ResolvedProcessEnvironment::resolve(
        lito::ProcessEnvironmentSpec { .append_path = rstd::move(append) },
        Some(inherited_path->as_os_str()),
        directory.as_path());
    ASSERT_TRUE(environment.is_ok());
    ASSERT_EQ(environment->search_directories().len(), usize(5));
    EXPECT_EQ(environment->search_directories()[usize {}].as_path(), inherited.as_path());
    EXPECT_EQ(environment->search_directories()[usize(1)].as_path(), directory.as_path());
    EXPECT_EQ(environment->search_directories()[usize(2)].as_path(), relative.as_path());
    EXPECT_EQ(environment->search_directories()[usize(3)].as_path(), first.as_path());
    EXPECT_EQ(environment->search_directories()[usize(4)].as_path(), second.as_path());

    auto child_entries =
        rstd::env::split_paths(environment->child_path()).collect<Vec<rstd::path::PathBuf>>();
    ASSERT_EQ(child_entries.len(), environment->search_directories().len());
    for (usize index {}; index < child_entries.len(); ++index) {
        EXPECT_EQ(child_entries[index].as_path(),
                  environment->search_directories()[index].as_path());
    }

    auto resolver = lito::ToolResolver(*environment);
    auto selected = resolver.resolve(rstd::path::PathBuf::from("lito-tool"_str).as_path(),
                                     "test executable"_str);
    ASSERT_TRUE(selected.is_ok());
    EXPECT_EQ(selected->executable.as_path(), inherited_tool.as_path());

    auto fallback = resolver.resolve(rstd::path::PathBuf::from("lito-fallback"_str).as_path(),
                                     "test executable"_str);
    ASSERT_TRUE(fallback.is_ok());
    EXPECT_EQ(fallback->executable.as_path(), first_fallback.as_path());

    auto absolute = resolver.resolve(absolute_tool.as_path(), "test executable"_str);
    ASSERT_TRUE(absolute.is_ok());
    EXPECT_EQ(absolute->executable.as_path(), absolute_tool.as_path());
    EXPECT_TRUE(absolute->executable.as_path().is_absolute());
#if RSTD_OS_WINDOWS
    auto extension = resolver.resolve(rstd::path::PathBuf::from("lito-extension"_str).as_path(),
                                      "test executable"_str);
    ASSERT_TRUE(extension.is_ok());
    EXPECT_EQ(extension->executable.as_path(), extension_tool.as_path());
#endif

    EXPECT_TRUE(
        resolver
            .resolve(rstd::path::PathBuf::from("nested/tool"_str).as_path(), "test executable"_str)
            .is_err());
    EXPECT_TRUE(resolver
                    .resolve(rstd::path::PathBuf::from("lito-non-executable"_str).as_path(),
                             "test executable"_str)
                    .is_err());
    auto missing = resolver.resolve(rstd::path::PathBuf::from("lito-missing"_str).as_path(),
                                    "test executable"_str);
    ASSERT_TRUE(missing.is_err());
    auto missing_error = rstd::move(missing).unwrap_err();
    EXPECT_TRUE(missing_error.message.as_str().contains("lito-missing"_str));
    EXPECT_TRUE(missing_error.message.as_str().contains(first.as_path().to_str().unwrap()));

    ASSERT_TRUE(rstd::fs::remove_file(inherited_tool.as_path()).is_ok());
    auto cached = resolver.resolve(rstd::path::PathBuf::from("lito-tool"_str).as_path(),
                                   "test executable"_str);
    ASSERT_TRUE(cached.is_ok());
    EXPECT_EQ(cached->executable.as_path(), inherited_tool.as_path());
    EXPECT_TRUE(clear_output(directory.as_path()));
}

TEST(Contracts, ProcessExecutionRejectsUnresolvedToolNames) {
    auto environment = lito::ResolvedProcessEnvironment::resolve(
        lito::ProcessEnvironmentSpec {}, None(), root("config"_str).as_path());
    ASSERT_TRUE(environment.is_ok());
    auto arguments = strings("lito-unresolved-tool"_str);
    EXPECT_TRUE(lito::run_command(arguments, *environment).is_err());
    EXPECT_TRUE(lito::run_command_with_input(arguments, ""_str, *environment).is_err());
}

TEST(Contracts, BuildUsesConfiguredAppendedToolPath) {
    auto project = root("environment/append-path"_str);
    auto config  = lito::load_project_config(project.as_path());
    ASSERT_TRUE(config.is_ok());
    auto output = output_root("environment-append-path"_str);
    ASSERT_TRUE(clear_output(output.as_path()));
    auto request = build_request(
        project.as_path(), output.as_path(), Vec<String>::make(), build_profile("release"_str));
    request.environment             = rstd::move(config->environment);
    request.configuration.toolchain = rstd::move(config->toolchain);
    auto built                      = lito::build(request);
    ASSERT_TRUE(built.is_ok());
    EXPECT_EQ(artifact_count(*built, lito::ArtifactKind::Executable), usize(1));
    EXPECT_TRUE(clear_output(output.as_path()));
}

TEST(Contracts, TestArtifactReceivesConfiguredEffectivePath) {
    auto project = root("environment/test-path"_str);
    auto config  = lito::load_project_config(project.as_path());
    ASSERT_TRUE(config.is_ok());
    auto output = output_root("environment-test-path"_str);
    ASSERT_TRUE(clear_output(output.as_path()));
    auto request = build_request(
        project.as_path(), output.as_path(), Vec<String>::make(), build_profile("release"_str));
    request.environment             = rstd::move(config->environment);
    request.configuration.toolchain = rstd::move(config->toolchain);
    auto tested                     = lito::test(lito::TestRequest {
        .build = rstd::move(request),
    });
    ASSERT_TRUE(tested.is_ok());
    EXPECT_TRUE(tested->success());
    ASSERT_EQ(tested->executions.len(), usize(1));
    EXPECT_TRUE(tested->executions[usize {}].success());
    EXPECT_TRUE(clear_output(output.as_path()));
}

TEST(Contracts, PkgConfigProviderConfigurationBelongsToProjectConfig) {
    auto loaded = lito::load_project_config(root("config/pkg-config"_str).as_path());
    ASSERT_TRUE(loaded.is_ok());
    EXPECT_TRUE(loaded->pkg_config.target_configured);
    EXPECT_EQ(loaded->pkg_config.executable.as_path().to_str().unwrap(), "pkg-config"_str);
    EXPECT_EQ(loaded->pkg_config.search_paths.len(), usize(1));
    EXPECT_EQ(loaded->pkg_config.library_paths.len(), usize(1));
    EXPECT_TRUE(loaded->pkg_config.sysroot.is_some());

    auto search_only =
        lito::load_project_config(root("config/pkg-config-search-only"_str).as_path());
    ASSERT_TRUE(search_only.is_ok());
    EXPECT_FALSE(search_only->pkg_config.target_configured);
}

TEST(Contracts, CMakeProviderConfigurationBelongsToProjectConfig) {
    auto loaded = lito::load_project_config(root("config/cmake"_str).as_path());
    ASSERT_TRUE(loaded.is_ok());
    EXPECT_EQ(loaded->cmake.executable.as_path().to_str().unwrap(), "custom-cmake"_str);
    EXPECT_EQ(loaded->cmake.generator.as_str(), "Unix Makefiles"_str);
    ASSERT_EQ(loaded->cmake.search_paths.len(), usize(1));
    EXPECT_EQ(loaded->cmake.search_paths[usize {}].as_path(), root("config/cmake"_str).as_path());
}

TEST(Contracts, PkgConfigManifestIsTypedBeforeResolution) {
    auto loaded = lito::load_package_manifest(root("manifest/pkg-config/valid"_str).as_path());
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

    auto graph = lito::resolve_package_graph(root("manifest/pkg-config/valid"_str).as_path());
    ASSERT_TRUE(graph.is_ok());
    ASSERT_EQ(graph->packages.len(), usize(1));
    EXPECT_TRUE(graph->packages[usize {}].dependencies.is_empty());
    EXPECT_EQ(graph->packages[usize {}].manifest.pkg_config_external_dependencies.len(), usize(2));
}

TEST(Contracts, PkgConfigInvalidManifestDocumentsAreRejectedByManifestOwner) {
    for (const auto path : INVALID_PKG_CONFIG_MANIFESTS) {
        auto loaded = lito::load_manifest_document(root(path).as_path());
        EXPECT_TRUE(loaded.is_err());
    }
}

TEST(Contracts, CMakeManifestIsTypedAndSourceIsResolvedByExternalOwner) {
    auto loaded = lito::load_package_manifest(root("manifest/cmake/valid"_str).as_path());
    ASSERT_TRUE(loaded.is_ok());
    EXPECT_TRUE(loaded->dependencies.is_empty());
    ASSERT_EQ(loaded->cmake_external_dependencies.len(), usize(2));

    const auto installed_index =
        loaded->cmake_external_dependencies[usize {}].alias.as_str() == "vulkan"_str ? usize {}
                                                                                     : usize(1);
    const auto  source_index          = installed_index == usize {} ? usize(1) : usize {};
    const auto& installed_requirement = loaded->cmake_external_dependencies[installed_index];
    EXPECT_EQ(installed_requirement.package.as_str(), "Vulkan"_str);
    ASSERT_EQ(installed_requirement.targets.len(), usize(1));
    EXPECT_EQ(installed_requirement.targets[usize {}].name.as_str(), "Vulkan::Vulkan"_str);
    EXPECT_TRUE(installed_requirement.source.is_Installed());

    const auto& source_requirement = loaded->cmake_external_dependencies[source_index];
    EXPECT_EQ(source_requirement.package.as_str(), "LitoFixture"_str);
    ASSERT_EQ(source_requirement.targets.len(), usize(3));
    EXPECT_EQ(source_requirement.targets[usize {}].name.as_str(), "LitoFixture::fixture"_str);
    EXPECT_EQ(source_requirement.targets[usize(1)].name.as_str(), "LitoFixture::headers"_str);
    EXPECT_EQ(source_requirement.targets[usize(2)].name.as_str(), "LitoFixture::order"_str);
    EXPECT_TRUE(source_requirement.source.is_Path());
    ASSERT_EQ(source_requirement.cache.len(), usize(1));
    EXPECT_EQ(source_requirement.cache[usize {}].name.as_str(), "LITO_FIXTURE_OPTION"_str);
    EXPECT_EQ(source_requirement.cache[usize {}].value.as_str(), "ON"_str);
    ASSERT_TRUE(source_requirement.config_directory.is_some());
    EXPECT_EQ(source_requirement.config_directory->as_path().to_str().unwrap(),
              "lib/cmake/LitoFixture"_str);

    auto graph = lito::resolve_package_graph(root("manifest/cmake/valid"_str).as_path());
    ASSERT_TRUE(graph.is_ok());
    ASSERT_EQ(graph->packages.len(), usize(1));
    EXPECT_TRUE(graph->packages[usize {}].cmake_external_dependencies.is_empty());
    auto external = lito::prepare_external_dependency_sources(*graph, {});
    ASSERT_TRUE(external.is_ok());
    ASSERT_EQ(graph->packages[usize {}].cmake_external_dependencies.len(), usize(2));
    const auto resolved_index =
        graph->packages[usize {}].cmake_external_dependencies[usize {}].alias.as_str() ==
                "fixture"_str
            ? usize {}
            : usize(1);
    const auto& resolved = graph->packages[usize {}].cmake_external_dependencies[resolved_index];
    ASSERT_TRUE(resolved.source.is_Directory());
    EXPECT_FALSE(resolved.source.as_Directory().identity.is_empty());
    EXPECT_FALSE(resolved.source.as_Directory().root.as_path().to_str().unwrap().is_empty());
}

TEST(Contracts, CMakeBuildTreeManifestIsTypedAndAdapterIsResolvedByPackageOwner) {
    auto directory = root("manifest/cmake/build-tree"_str);
    auto loaded    = lito::load_package_manifest(directory.as_path());
    ASSERT_TRUE(loaded.is_ok());
    ASSERT_EQ(loaded->cmake_external_dependencies.len(), usize(1));
    const auto& declared = loaded->cmake_external_dependencies[usize {}];
    EXPECT_EQ(declared.integration, lito::CMakeIntegration::BuildTree);
    ASSERT_TRUE(declared.source.is_Path());
    ASSERT_TRUE(declared.adapter.is_some());
    EXPECT_EQ(declared.adapter->as_path().to_str().unwrap(), "adapter.cmake"_str);

    auto graph = lito::resolve_package_graph(directory.as_path());
    ASSERT_TRUE(graph.is_ok());
    ASSERT_TRUE(lito::prepare_external_dependency_sources(*graph, {}).is_ok());
    const auto& resolved = graph->packages[usize {}].cmake_external_dependencies[usize {}];
    EXPECT_TRUE(resolved.source.is_Directory());
    EXPECT_EQ(resolved.integration, lito::CMakeIntegration::BuildTree);
    ASSERT_TRUE(resolved.adapter.is_some());
    EXPECT_TRUE(resolved.adapter->as_path().starts_with(directory.as_path()));
}

TEST(Contracts, CMakeInvalidManifestDocumentsAreRejectedByManifestOwner) {
    for (const auto path : INVALID_CMAKE_MANIFESTS) {
        auto loaded = lito::load_manifest_document(root(path).as_path());
        EXPECT_TRUE(loaded.is_err());
    }
}

TEST(Contracts, CMakeArchitectureArchivesManifestIsTyped) {
    auto loaded =
        lito::load_package_manifest(root("manifest/cmake/architecture-archives"_str).as_path());
    ASSERT_TRUE(loaded.is_ok());
    ASSERT_EQ(loaded->cmake_external_dependencies.len(), usize(1));
    const auto& source = loaded->cmake_external_dependencies[usize {}].source;
    ASSERT_TRUE(source.is_ArchitectureArchives());
    const auto& variants = source.as_ArchitectureArchives().variants;
    ASSERT_EQ(variants.len(), usize(2));
    EXPECT_EQ(variants[usize {}].architecture.as_str(), "aarch64"_str);
    EXPECT_EQ(variants[usize {}].url.as_str(), "https://example.com/fixture-linuxarm64.tar.gz"_str);
    EXPECT_EQ(variants[usize(1)].architecture.as_str(), "x86_64"_str);
    EXPECT_EQ(variants[usize(1)].sha256.as_str(),
              "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"_str);
}

TEST(Contracts, CMakeArchitectureArchivesInvalidManifestsAreRejected) {
    for (const auto path : INVALID_CMAKE_ARCHITECTURE_ARCHIVE_MANIFESTS) {
        auto loaded = lito::load_manifest_document(root(path).as_path());
        if (loaded.is_ok()) rstd::io::eprintln("unexpected valid manifest: {}", path);
        EXPECT_TRUE(loaded.is_err());
    }
}

TEST(Contracts, CMakeArchitectureArchivesAreSelectedForEffectiveTarget) {
    auto variants = Vec<lito::CMakeArchiveVariant>::make();
    variants.push(lito::CMakeArchiveVariant {
        .architecture = lito::Architecture { .name = String::make("aarch64"_str) },
        .url          = String::make("https://example.com/arm64.tar.gz"_str),
        .sha256 =
            String::make("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"_str),
    });
    variants.push(lito::CMakeArchiveVariant {
        .architecture = lito::Architecture { .name = String::make("x86_64"_str) },
        .url          = String::make("https://example.com/x64.tar.gz"_str),
        .sha256 =
            String::make("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"_str),
    });
    auto requirement = lito::PreparedCMakeDependencyRequirement {
        .alias   = String::make("fixture"_str),
        .package = String::make("Fixture"_str),
        .source  = lito::PreparedCMakeDependencySource::ArchitectureArchives(rstd::move(variants)),
        .integration = lito::CMakeIntegration::BuildTree,
    };

    auto native = lito::resolve_cmake_requirement_for_platform(requirement, native_platform());
    ASSERT_TRUE(native.is_ok());
    ASSERT_TRUE(native->source.is_Archive());
    EXPECT_EQ(native->source.as_Archive().url.as_str(), "https://example.com/x64.tar.gz"_str);

    auto arm = lito::resolve_cmake_requirement_for_platform(
        requirement, explicit_platform("aarch64-unknown-linux-gnu"_str));
    ASSERT_TRUE(arm.is_ok());
    ASSERT_TRUE(arm->source.is_Archive());
    EXPECT_EQ(arm->source.as_Archive().url.as_str(), "https://example.com/arm64.tar.gz"_str);
    EXPECT_EQ(arm->source.as_Archive().sha256.as_str(),
              "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"_str);

    auto missing = lito::resolve_cmake_requirement_for_platform(
        requirement, explicit_platform("riscv64-unknown-linux-gnu"_str));
    ASSERT_TRUE(missing.is_err());
    const auto& message = missing.unwrap_err().message;
    EXPECT_TRUE(message.as_str().contains("fixture"_str));
    EXPECT_TRUE(message.as_str().contains("riscv64-unknown-linux-gnu"_str));
    EXPECT_TRUE(message.as_str().contains("architecture 'riscv64'"_str));
    EXPECT_TRUE(message.as_str().contains("aarch64, x86_64"_str));

    auto declarations = Vec<lito::PreparedCMakeDependencyRequirement>::make();
    declarations.push(rstd::move(requirement));
    auto parser = lito::make_clang_cpp_argument_parser();
    ASSERT_TRUE(parser.is_ok());
    auto cross_cmake = resolve_cmake_fixtures(declarations,
                                              default_profile(*parser),
                                              explicit_platform("aarch64-unknown-linux-gnu"_str),
                                              *parser);
    ASSERT_TRUE(cross_cmake.is_err());
    EXPECT_TRUE(cross_cmake.unwrap_err().message.as_str().contains(
        "without an explicit CMake toolchain contract"_str));
}

TEST(Contracts, CMakeArchitectureArchivesSurviveWorkspaceInheritanceAndPreparation) {
    auto directory = root("workspace/architecture-archives"_str);
    auto document  = lito::load_manifest_document(directory.as_path());
    ASSERT_TRUE(document.is_ok());
    ASSERT_TRUE(document->workspace.is_some());
    ASSERT_EQ(document->workspace->cmake_external_dependencies.len(), usize(1));
    const auto& declared = document->workspace->cmake_external_dependencies[usize {}];
    ASSERT_TRUE(declared.source.is_ArchitectureArchives());
    ASSERT_EQ(declared.source.as_ArchitectureArchives().variants.len(), usize(2));

    auto graph = lito::resolve_package_graph(directory.as_path());
    ASSERT_TRUE(graph.is_ok());
    ASSERT_EQ(graph->packages.len(), usize(1));
    ASSERT_EQ(graph->packages[usize {}].manifest.cmake_external_dependencies.len(), usize(1));
    const auto& inherited =
        graph->packages[usize {}].manifest.cmake_external_dependencies[usize {}];
    ASSERT_TRUE(inherited.source.is_ArchitectureArchives());
    ASSERT_EQ(inherited.source.as_ArchitectureArchives().variants.len(), usize(2));
    ASSERT_EQ(inherited.targets.len(), usize(1));
    EXPECT_EQ(inherited.targets[usize {}].name.as_str(), "Fixture::fixture"_str);

    const auto source_count = graph->sources.len();
    ASSERT_TRUE(lito::prepare_external_dependency_sources(*graph, {}).is_ok());
    ASSERT_EQ(graph->packages[usize {}].cmake_external_dependencies.len(), usize(1));
    const auto& prepared = graph->packages[usize {}].cmake_external_dependencies[usize {}];
    ASSERT_TRUE(prepared.source.is_ArchitectureArchives());
    EXPECT_EQ(prepared.source.as_ArchitectureArchives().variants.len(), usize(2));
    EXPECT_EQ(graph->sources.len(), source_count);
}

TEST(Contracts, BuildPlatformMakesNativeAndExplicitTargetIntentObservable) {
    auto compiler_default = pkg_config_target();
    auto host             = lito::HostInfo {
        .architecture = compiler_default.architecture.clone(),
        .os           = compiler_default.os.clone(),
    };
    auto native = lito::resolve_build_platform(host, compiler_default, None());
    ASSERT_TRUE(native.is_ok());
    EXPECT_EQ(native->intent, lito::BuildTargetIntent::Native);
    EXPECT_FALSE(native->cross);
    EXPECT_EQ(native->effective_target.architecture.as_str(), "x86_64"_str);

    auto arm_default = lito::parse_target_info("aarch64-unknown-linux-gnu"_str);
    ASSERT_TRUE(arm_default.is_ok());
    auto unintentional_cross = lito::resolve_build_platform(host, *arm_default, None());
    ASSERT_TRUE(unintentional_cross.is_err());
    EXPECT_TRUE(unintentional_cross.unwrap_err().message.as_str().contains(
        "declare an explicit target/toolchain contract"_str));

    auto explicit_cross =
        lito::resolve_build_platform(host, compiler_default, Some("aarch64-unknown-linux-gnu"_str));
    ASSERT_TRUE(explicit_cross.is_ok());
    EXPECT_EQ(explicit_cross->intent, lito::BuildTargetIntent::ExplicitTarget);
    EXPECT_TRUE(explicit_cross->cross);
    EXPECT_EQ(explicit_cross->effective_target.architecture.as_str(), "aarch64"_str);
}

TEST(Contracts, CMakeManifestAcceptsUnnamespacedTargets) {
    auto loaded =
        lito::load_package_manifest(root("manifest/cmake/unnamespaced-target"_str).as_path());
    ASSERT_TRUE(loaded.is_ok());
    ASSERT_EQ(loaded->cmake_external_dependencies.len(), usize(1));
    const auto& requirement = loaded->cmake_external_dependencies[usize {}];
    EXPECT_EQ(requirement.package.as_str(), "qjs"_str);
    ASSERT_EQ(requirement.targets.len(), usize(1));
    EXPECT_EQ(requirement.targets[usize {}].name.as_str(), "qjs"_str);
}

TEST(Contracts, PkgConfigFragmentTokenizerPreservesArgumentsWithoutExecutingThem) {
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

TEST(Contracts, PkgConfigProviderProducesTypedCompileAndOrderedLinkRequirements) {
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
        user_include        = user_include || include.kind == lito::CppIncludeDirectoryKind::User;
        system_include = system_include || include.kind == lito::CppIncludeDirectoryKind::System;
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

TEST(Contracts, CMakePlannerIsPureAndMaterializesOrderedPackageOperations) {
    auto parser = lito::make_clang_cpp_argument_parser();
    ASSERT_TRUE(parser.is_ok());
    auto platform = native_platform();
    auto targets  = Vec<lito::CMakeTargetRequirement>::make();
    targets.push(lito::CMakeTargetRequirement {
        .name = String::make("Fixture::fixture"_str),
    });
    auto prepared = lito::PreparedCMakeDependencyRequirement {
        .alias   = String::make("planner-fixture"_str),
        .package = String::make("Fixture"_str),
        .source  = lito::PreparedCMakeDependencySource::Directory(
            root("cmake/package"_str), String::make("lito-test-cmake-planner-pure-v1"_str), false),
        .targets = rstd::move(targets),
    };
    auto requirement = lito::resolve_cmake_requirement_for_platform(prepared, platform);
    ASSERT_TRUE(requirement.is_ok());
    auto first = lito::plan_cmake_package(*requirement,
                                          fixture_cmake(),
                                          configuration(),
                                          default_profile(*parser),
                                          platform.compiler_default,
                                          platform.effective_target.triple.as_str(),
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
                                                        platform.effective_target.triple.as_str());
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
                                                        platform.effective_target.triple.as_str());
    ASSERT_TRUE(installed.is_ok());
    ASSERT_EQ(installed->operations.len(), usize(4));
    EXPECT_EQ(installed->operations[usize {}], lito::CMakePackageOperation::WriteQuery);
    EXPECT_EQ(installed->operations[usize(1)], lito::CMakePackageOperation::ConfigureQuery);
    EXPECT_EQ(installed->operations[usize(2)], lito::CMakePackageOperation::BuildQuery);
    EXPECT_EQ(installed->operations[usize(3)], lito::CMakePackageOperation::ReadUsage);
}

TEST(Contracts, CMakeProviderBuildsInstallsAndReadsImportedTargetUsage) {
    auto parser = lito::make_clang_cpp_argument_parser();
    ASSERT_TRUE(parser.is_ok());
    auto count_directory = output_root("cmake-provider-count"_str);
    ASSERT_TRUE(clear_output(count_directory.as_path()));
    ASSERT_TRUE(rstd::fs::create_dir_all(count_directory.as_path()).is_ok());
    auto count_path =
        count_directory.join(rstd::path::PathBuf::from("configure-count"_str).as_path());
    auto target       = pkg_config_target();
    auto declarations = Vec<lito::PreparedCMakeDependencyRequirement>::make();
    auto targets      = Vec<lito::CMakeTargetRequirement>::make();
    targets.push(lito::CMakeTargetRequirement {
        .name       = String::make("LitoFixture::fixture"_str),
        .visibility = lito::DependencyVisibility::Private,
    });
    targets.push(lito::CMakeTargetRequirement {
        .name       = String::make("LitoFixture::headers"_str),
        .visibility = lito::DependencyVisibility::Public,
    });
    targets.push(lito::CMakeTargetRequirement {
        .name       = String::make("LitoFixture::order"_str),
        .visibility = lito::DependencyVisibility::Runtime,
    });
    auto cache = Vec<lito::CMakeCacheEntry>::make();
    cache.push(lito::CMakeCacheEntry {
        .name  = String::make("LITO_FIXTURE_CONFIGURE_COUNT"_str),
        .value = String::make(count_path.as_path().to_str().unwrap()),
    });
    declarations.push(lito::PreparedCMakeDependencyRequirement {
        .alias   = String::make("fixture"_str),
        .package = String::make("LitoFixture"_str),
        .source  = lito::PreparedCMakeDependencySource::Directory(
            root("cmake/package"_str), String::make("lito-test-cmake-fixture-v3"_str), true),
        .config_directory = Some(rstd::path::PathBuf::from("lib/cmake/LitoFixture"_str)),
        .cache            = rstd::move(cache),
        .targets          = rstd::move(targets),
    });
    auto resolved =
        resolve_cmake_fixtures(declarations, default_profile(*parser), native_platform(), *parser);
    if (resolved.is_err()) {
        auto error = rstd::move(resolved).unwrap_err();
        rstd::io::eprintln("{}", error.message.as_str());
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
    EXPECT_EQ(dependency.targets[usize(1)].visibility, lito::DependencyVisibility::Public);
    EXPECT_EQ(dependency.targets[usize(2)].name.as_str(), "LitoFixture::order"_str);
    EXPECT_EQ(dependency.targets[usize(2)].visibility, lito::DependencyVisibility::Runtime);

    auto first_count = rstd::fs::read_to_string(count_path.as_path());
    ASSERT_TRUE(first_count.is_ok());
    EXPECT_EQ(first_count->as_str(), "configure\n"_str);
    declarations[usize {}].targets[usize {}].name = String::make("LitoFixture::headers"_str);
    declarations[usize {}].targets[usize(1)].name = String::make("LitoFixture::fixture"_str);
    auto queried_again =
        resolve_cmake_fixtures(declarations, default_profile(*parser), native_platform(), *parser);
    ASSERT_TRUE(queried_again.is_ok());
    auto second_count = rstd::fs::read_to_string(count_path.as_path());
    ASSERT_TRUE(second_count.is_ok());
    EXPECT_EQ(second_count->as_str(), "configure\n"_str);

    declarations[usize {}].cache.push(lito::CMakeCacheEntry {
        .name  = String::make("LITO_FIXTURE_VARIANT"_str),
        .value = String::make("ON"_str),
    });
    auto installed_again =
        resolve_cmake_fixtures(declarations, default_profile(*parser), native_platform(), *parser);
    ASSERT_TRUE(installed_again.is_ok());
    auto third_count = rstd::fs::read_to_string(count_path.as_path());
    ASSERT_TRUE(third_count.is_ok());
    EXPECT_EQ(third_count->as_str(), "configure\nconfigure\n"_str);

    auto disabled_profile = lito::make_profile_spec(configuration(),
                                                    lito::ProjectProfile {
                                                        .exceptions = false,
                                                        .rtti       = false,
                                                    },
                                                    build_profile("debug"_str),
                                                    *parser);
    ASSERT_TRUE(disabled_profile.is_ok());
    auto profile_variant =
        resolve_cmake_fixtures(declarations, *disabled_profile, native_platform(), *parser);
    ASSERT_TRUE(profile_variant.is_ok());
    auto fourth_count = rstd::fs::read_to_string(count_path.as_path());
    ASSERT_TRUE(fourth_count.is_ok());
    EXPECT_EQ(fourth_count->as_str(), "configure\nconfigure\nconfigure\n"_str);
    EXPECT_TRUE(clear_output(count_directory.as_path()));
}

TEST(Contracts, CMakeProviderBuildsAndReadsBuildTreeTargetUsage) {
    auto parser = lito::make_clang_cpp_argument_parser();
    ASSERT_TRUE(parser.is_ok());
    auto targets = Vec<lito::CMakeTargetRequirement>::make();
    targets.push(lito::CMakeTargetRequirement {
        .name       = String::make("LitoBuildTree::fixture"_str),
        .visibility = lito::DependencyVisibility::Private,
    });
    auto declarations = Vec<lito::PreparedCMakeDependencyRequirement>::make();
    declarations.push(lito::PreparedCMakeDependencyRequirement {
        .alias   = String::make("fixture"_str),
        .package = String::make("LitoBuildTree"_str),
        .source  = lito::PreparedCMakeDependencySource::Directory(
            root("cmake/build-tree"_str), String::make("lito-test-cmake-build-tree-v1"_str), false),
        .integration = lito::CMakeIntegration::BuildTree,
        .adapter     = Some(root("manifest/cmake/build-tree/adapter.cmake"_str)),
        .targets     = rstd::move(targets),
    });
    auto target = pkg_config_target();
    auto resolved =
        resolve_cmake_fixtures(declarations, default_profile(*parser), native_platform(), *parser);
    if (resolved.is_err()) {
        auto error = rstd::move(resolved).unwrap_err();
        rstd::io::eprintln("{}", error.message.as_str());
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
                                         "LITO_CMAKE_BUILD_TREE_USAGE=1"_str;
        }
        if (occurrence.argument.is_IncludeDirectory()) has_include = true;
    }
    EXPECT_TRUE(has_macro);
    EXPECT_TRUE(has_include);

    auto has_archive = false;
    for (const auto& token : dependency.link_arguments.tokens) {
        if (token.as_str().contains("liblito_build_tree.a"_str)) {
            has_archive = rstd::path::PathBuf::from(token.as_str()).as_path().is_absolute();
        }
    }
    EXPECT_TRUE(has_archive);
}

TEST(Contracts, PkgConfigProviderSupportsVersionOperatorsAndReportsDependencyContext) {
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
    EXPECT_TRUE(error.message.as_str().contains("incompatible"_str));
    EXPECT_TRUE(error.message.as_str().contains("lito-fixture"_str));
}

TEST(Contracts, PkgConfigProviderFailsClosedForCrossTargetsAndMissingInputs) {
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
    EXPECT_TRUE(provider_error.message.as_str().contains("fixture"_str));
    EXPECT_TRUE(provider_error.message.as_str().contains("lito-fixture"_str));

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
    EXPECT_TRUE(module_error.message.as_str().contains("missing-module"_str));
    EXPECT_TRUE(module_error.message.as_str().contains("lito-module-does-not-exist"_str));
}

TEST(Contracts, PkgConfigProviderCachesEquivalentQueriesWithinResolution) {
    auto parser = lito::make_clang_cpp_argument_parser();
    ASSERT_TRUE(parser.is_ok());
    auto directory = output_root("pkg-config-counting-provider"_str);
    ASSERT_TRUE(clear_output(directory.as_path()));
    ASSERT_TRUE(rstd::fs::create_dir_all(directory.as_path()).is_ok());
    auto config = lito::PkgConfigProviderConfig {
        .executable        = root("pkg-config/counting-provider"_str),
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

TEST(Contracts, ExternalUsageSeparatesCompileVisibilityFromStaticLinkClosure) {
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

    auto runtime_metadata = external_usage_metadata(lito::DependencyVisibility::Runtime, *parser);
    ASSERT_TRUE(runtime_metadata.is_ok());
    auto runtime_plan =
        lito::resolve_source_discovery(*runtime_metadata, "debug"_str, Vec<String>::make());
    ASSERT_TRUE(runtime_plan.is_ok());
    EXPECT_FALSE(has_external_macro(runtime_plan->contexts[usize {}]));
    EXPECT_FALSE(has_external_macro(runtime_plan->contexts[usize(1)]));
    ASSERT_EQ(runtime_plan->link_inputs[usize(1)].len(), usize(2));
    EXPECT_TRUE(runtime_plan->link_inputs[usize(1)][usize(1)].is_External());
}

TEST(Contracts, InvalidDependencyGraphsAreRejectedByResolverOwner) {
    for (const auto path : INVALID_GRAPHS) {
        auto resolved = lito::resolve_package_graph(root(path).as_path());
        if (resolved.is_ok()) rstd::io::eprintln("unexpected valid graph: {}", path);
        EXPECT_TRUE(resolved.is_err());
    }
}

TEST(Contracts, ProjectNameComesFromRootManifest) {
    auto workspace = lito::resolve_package_graph(root("../demo/workspace"_str).as_path());
    ASSERT_TRUE(workspace.is_ok());
    EXPECT_TRUE(workspace->root_is_workspace);
    EXPECT_EQ(workspace->name.as_str(), "demo-workspace"_str);

    auto workspace_member =
        lito::resolve_package_graph(root("../demo/workspace/app-one"_str).as_path());
    ASSERT_TRUE(workspace_member.is_ok());
    EXPECT_TRUE(workspace_member->root_is_workspace);
    EXPECT_EQ(workspace_member->name.as_str(), "demo-workspace"_str);

    auto package =
        lito::resolve_package_graph(root("../demo/module-convention/demo-app"_str).as_path());
    ASSERT_TRUE(package.is_ok());
    EXPECT_FALSE(package->root_is_workspace);
    EXPECT_EQ(package->name.as_str(), "demo-app"_str);
}

TEST(Contracts, WorkspaceDiscoversAssociatedTestPackageWithInheritance) {
    auto graph =
        lito::resolve_package_graph(root("conventional-test/workspace-package"_str).as_path());
    ASSERT_TRUE(graph.is_ok());

    auto associated = false;
    for (const auto& project_root : graph->roots) {
        if (project_root.name.as_str() != "fixture-associated-workspace-test"_str) continue;
        associated = true;
        EXPECT_EQ(project_root.role, lito::ProjectRootRole::AssociatedTest);
        EXPECT_EQ(project_root.source_identity.as_str(), "path+tests"_str);
    }
    EXPECT_TRUE(associated);

    for (const auto& package : graph->packages) {
        if (package.manifest.name.as_str() != "fixture-associated-workspace-test"_str) continue;
        EXPECT_TRUE(package.manifest.workspace_dependencies.is_empty());
        EXPECT_EQ(package.manifest.version.source, lito::PackageVersionSource::Workspace);
        ASSERT_TRUE(package.manifest.version.value.is_some());
        EXPECT_EQ(package.manifest.version.value->as_str(), "0.1.0"_str);
        EXPECT_EQ(package.dependencies.len(), usize(1));
    }
}

TEST(Contracts, WorkspaceBenchmarkTargetsUseDevelopmentDependencies) {
    auto directory = root("conventional-test/workspace-package"_str);
    auto graph     = lito::resolve_package_graph(directory.as_path());
    ASSERT_TRUE(graph.is_ok());

    auto library_found = false;
    for (const auto& project_root : graph->roots) {
        EXPECT_NE(project_root.name.as_str(), "fixture-associated-workspace-benchmark"_str);
    }
    for (const auto& package : graph->packages) {
        EXPECT_NE(package.manifest.name.as_str(), "fixture-associated-workspace-benchmark"_str);
        if (package.manifest.name.as_str() == "fixture-associated-workspace-library"_str) {
            library_found = true;
            EXPECT_TRUE(package.manifest.workspace_dev_dependencies.is_empty());
            ASSERT_EQ(package.manifest.dev_dependencies.len(), usize(1));
            ASSERT_EQ(package.dev_dependencies.len(), usize(1));
            EXPECT_EQ(package.dev_dependencies[usize {}].name.as_str(),
                      "fixture-associated-workspace-bench-helper"_str);
            auto benchmark_found = false;
            for (const auto& target : package.manifest.targets) {
                if (target.is_Benchmark() && target.as_Benchmark().name.as_str() == "speed"_str) {
                    benchmark_found = true;
                    ASSERT_EQ(target.as_Benchmark().source.declared_sources.len(), usize(1));
                }
            }
            EXPECT_TRUE(benchmark_found);
        }
    }
    EXPECT_TRUE(library_found);

    auto production =
        lito::resolve_package_selection(lito::PackageSelection { .root = directory.clone() },
                                        lito::PackageSelectionPurpose::Production);
    ASSERT_TRUE(production.is_ok());
    EXPECT_TRUE(
        contains_name(production->selected_root_names, "fixture-associated-workspace-library"_str));
    EXPECT_FALSE(contains_name(production->selected_package_names,
                               "fixture-associated-workspace-bench-helper"_str));

    auto tests = lito::resolve_package_selection(
        lito::PackageSelection { .root = directory.clone() }, lito::PackageSelectionPurpose::Test);
    ASSERT_TRUE(tests.is_ok());
    EXPECT_TRUE(contains_name(tests->selected_root_names, "fixture-associated-workspace-test"_str));
    EXPECT_FALSE(contains_name(tests->selected_package_names,
                               "fixture-associated-workspace-bench-helper"_str));

    auto benchmark =
        lito::resolve_package_selection(lito::PackageSelection { .root = directory.clone() },
                                        lito::PackageSelectionPurpose::Benchmark);
    ASSERT_TRUE(benchmark.is_ok());
    EXPECT_TRUE(
        contains_name(benchmark->selected_root_names, "fixture-associated-workspace-library"_str));
    EXPECT_TRUE(contains_name(benchmark->selected_package_names,
                              "fixture-associated-workspace-bench-helper"_str));

    auto standalone = lito::resolve_package_graph(
        root("conventional-test/workspace-package/benches"_str).as_path());
    ASSERT_TRUE(standalone.is_ok());
    ASSERT_EQ(standalone->roots.len(), usize(1));
    EXPECT_EQ(standalone->roots[usize {}].name.as_str(),
              "fixture-associated-workspace-benchmark"_str);
    EXPECT_EQ(standalone->roots[usize {}].role, lito::ProjectRootRole::PrimaryPackage);
}

TEST(Contracts, PackageOnlyManifestDiscoversConventionalBenchmark) {
    auto directory = root("conventional-benchmark/package-only"_str);
    auto package   = lito::load_package_manifest(directory.as_path());
    ASSERT_TRUE(package.is_ok());
    ASSERT_EQ(package->targets.len(), usize(1));
    ASSERT_TRUE(package->targets[usize {}].is_Benchmark());
    EXPECT_EQ(package->targets[usize {}].as_Benchmark().name.as_str(), "only"_str);

    auto benchmark =
        lito::resolve_package_selection(lito::PackageSelection { .root = directory.clone() },
                                        lito::PackageSelectionPurpose::Benchmark);
    ASSERT_TRUE(benchmark.is_ok());
    ASSERT_EQ(benchmark->selected_targets.len(), usize(1));
    EXPECT_EQ(benchmark->selected_targets[usize {}].name.as_str(), "only"_str);

    auto production =
        lito::resolve_package_selection(lito::PackageSelection { .root = directory.clone() },
                                        lito::PackageSelectionPurpose::Production);
    EXPECT_TRUE(production.is_err());
}

TEST(Contracts, SinglePackageDiscoversAssociatedTestPackage) {
    auto directory = root("conventional-test/package"_str);
    EXPECT_TRUE(locked_graph_is_current("conventional-test/package"_str));
    auto graph = lito::resolve_package_graph(directory.as_path());
    ASSERT_TRUE(graph.is_ok());
    ASSERT_EQ(graph->roots.len(), usize(2));
    auto primary = project_root_role(*graph, "fixture-conventional-library"_str);
    auto test    = project_root_role(*graph, "fixture-conventional-test"_str);
    ASSERT_TRUE(primary.is_some());
    ASSERT_TRUE(test.is_some());
    EXPECT_EQ(*primary, lito::ProjectRootRole::PrimaryPackage);
    EXPECT_EQ(*test, lito::ProjectRootRole::AssociatedTest);
    for (const auto& package : graph->packages) {
        if (package.manifest.name.as_str() != "fixture-conventional-test"_str) continue;
        EXPECT_EQ(package.manifest.version.source, lito::PackageVersionSource::Unspecified);
        EXPECT_TRUE(package.manifest.version.value.is_none());
    }

    auto production =
        lito::resolve_package_selection(lito::PackageSelection { .root = directory.clone() },
                                        lito::PackageSelectionPurpose::Production);
    ASSERT_TRUE(production.is_ok());
    ASSERT_EQ(production->selected_root_names.len(), usize(1));
    EXPECT_EQ(production->selected_root_names[usize {}].as_str(),
              "fixture-conventional-library"_str);
    EXPECT_FALSE(
        contains_name(production->selected_package_names, "fixture-conventional-bench-helper"_str));

    auto benchmarks =
        lito::resolve_package_selection(lito::PackageSelection { .root = directory.clone() },
                                        lito::PackageSelectionPurpose::Benchmark);
    ASSERT_TRUE(benchmarks.is_ok());
    EXPECT_TRUE(contains_name(benchmarks->selected_root_names, "fixture-conventional-library"_str));
    EXPECT_TRUE(
        contains_name(benchmarks->selected_package_names, "fixture-conventional-bench-helper"_str));
    ASSERT_EQ(benchmarks->selected_targets.len(), usize(2));
    auto multi_source_count = usize {};
    for (const auto& package : benchmarks->graph.packages) {
        if (package.manifest.name.as_str() != "fixture-conventional-library"_str) continue;
        for (const auto& target : package.manifest.targets) {
            if (target.is_Benchmark() && target.as_Benchmark().name.as_str() == "multi"_str) {
                multi_source_count = target.as_Benchmark().source.declared_sources.len();
            }
        }
    }
    EXPECT_EQ(multi_source_count, usize(2));

    auto tests = lito::resolve_package_selection(
        lito::PackageSelection { .root = directory.clone() }, lito::PackageSelectionPurpose::Test);
    ASSERT_TRUE(tests.is_ok());
    ASSERT_EQ(tests->selected_root_names.len(), usize(1));
    EXPECT_EQ(tests->selected_root_names[usize {}].as_str(), "fixture-conventional-test"_str);
    EXPECT_TRUE(contains_name(tests->selected_package_names, "fixture-conventional-library"_str));

    auto selected = lito::resolve_package_selection(
        lito::PackageSelection {
            .root     = directory.clone(),
            .packages = strings("fixture-conventional-test"_str),
        },
        lito::PackageSelectionPurpose::Test);
    ASSERT_TRUE(selected.is_ok());
    ASSERT_EQ(selected->selected_root_names.len(), usize(1));

    auto selected_primary = lito::resolve_package_selection(
        lito::PackageSelection {
            .root     = directory.clone(),
            .packages = strings("fixture-conventional-library"_str),
        },
        lito::PackageSelectionPurpose::Production);
    ASSERT_TRUE(selected_primary.is_ok());
    ASSERT_EQ(selected_primary->selected_root_names.len(), usize(1));

    auto test_as_production = lito::resolve_package_selection(
        lito::PackageSelection {
            .root     = directory.clone(),
            .packages = strings("fixture-conventional-test"_str),
        },
        lito::PackageSelectionPurpose::Production);
    EXPECT_TRUE(test_as_production.is_err());
}

TEST(Contracts, WorkspaceDiscoversAssociatedTestWorkspace) {
    auto directory = root("conventional-test/workspace"_str);
    EXPECT_TRUE(locked_graph_is_current("conventional-test/workspace"_str));
    auto graph = lito::resolve_package_graph(directory.as_path());
    ASSERT_TRUE(graph.is_ok());
    ASSERT_EQ(graph->roots.len(), usize(3));
    auto library = project_root_role(*graph, "fixture-conventional-workspace-library"_str);
    auto runtime = project_root_role(*graph, "fixture-conventional-workspace-test"_str);
    auto compile = project_root_role(*graph, "fixture-conventional-workspace-compile-test"_str);
    ASSERT_TRUE(library.is_some());
    ASSERT_TRUE(runtime.is_some());
    ASSERT_TRUE(compile.is_some());
    EXPECT_EQ(*library, lito::ProjectRootRole::WorkspaceMember);
    EXPECT_EQ(*runtime, lito::ProjectRootRole::AssociatedTest);
    EXPECT_EQ(*compile, lito::ProjectRootRole::AssociatedTest);

    auto production =
        lito::resolve_package_selection(lito::PackageSelection { .root = directory.clone() },
                                        lito::PackageSelectionPurpose::Production);
    ASSERT_TRUE(production.is_ok());
    ASSERT_EQ(production->selected_root_names.len(), usize(1));
    EXPECT_EQ(production->selected_root_names[usize {}].as_str(),
              "fixture-conventional-workspace-library"_str);

    auto tests = lito::resolve_package_selection(
        lito::PackageSelection { .root = directory.clone() }, lito::PackageSelectionPurpose::Test);
    ASSERT_TRUE(tests.is_ok());
    EXPECT_EQ(tests->selected_root_names.len(), usize(2));
    EXPECT_TRUE(
        contains_name(tests->selected_root_names, "fixture-conventional-workspace-test"_str));
    EXPECT_TRUE(contains_name(tests->selected_root_names,
                              "fixture-conventional-workspace-compile-test"_str));
    EXPECT_TRUE(
        contains_name(tests->selected_package_names, "fixture-conventional-workspace-library"_str));
}

TEST(Contracts, DependencyTestsAreNotAssociatedWithTheRootProject) {
    auto graph = lito::resolve_package_graph(
        root("conventional-test/dependency-boundary/root"_str).as_path());
    ASSERT_TRUE(graph.is_ok());
    ASSERT_EQ(graph->roots.len(), usize(1));
    EXPECT_EQ(graph->roots[usize {}].name.as_str(), "fixture-conventional-boundary-root"_str);
    EXPECT_EQ(graph->packages.len(), usize(2));
    EXPECT_TRUE(
        project_root_role(*graph, "fixture-conventional-boundary-dependency-test"_str).is_none());
}

TEST(Contracts, WorkspaceNameIsRequiredAndValidatedByManifestOwner) {
    auto missing = lito::load_manifest_document(root("workspace/name-missing"_str).as_path());
    ASSERT_TRUE(missing.is_err());
    EXPECT_TRUE(missing.unwrap_err().message.as_str().contains("missing 'name'"_str));

    auto invalid = lito::load_manifest_document(root("workspace/name-invalid"_str).as_path());
    ASSERT_TRUE(invalid.is_err());
    EXPECT_TRUE(invalid.unwrap_err().message.as_str().contains("workspace.name"_str));

    auto valid = lito::load_manifest_document(root("../demo/workspace"_str).as_path());
    ASSERT_TRUE(valid.is_ok());
    ASSERT_TRUE(valid->workspace.is_some());
    EXPECT_EQ(valid->workspace->name.as_str(), "demo-workspace"_str);
}

TEST(Contracts, DevelopmentDependenciesHavePrivateDistinctScope) {
    auto visibility =
        lito::load_package_manifest(root("workspace/dev-dependency-visibility"_str).as_path());
    ASSERT_TRUE(visibility.is_err());
    EXPECT_TRUE(visibility.unwrap_err().message.as_str().contains("visibility"_str));

    auto duplicate =
        lito::load_package_manifest(root("workspace/dev-dependency-duplicate"_str).as_path());
    ASSERT_TRUE(duplicate.is_err());
    EXPECT_TRUE(duplicate.unwrap_err().message.as_str().contains(
        "both dependencies and dev-dependencies"_str));
}

TEST(Contracts, WorkspaceDependenciesAreDeclaredOnceAndMaterializedForMembers) {
    auto directory = root("workspace/inherited-dependencies"_str);
    auto member =
        lito::load_package_manifest(root("workspace/inherited-dependencies/app"_str).as_path());
    ASSERT_TRUE(member.is_ok());
    EXPECT_TRUE(member->dependencies.is_empty());
    EXPECT_TRUE(member->pkg_config_external_dependencies.is_empty());
    EXPECT_TRUE(member->cmake_external_dependencies.is_empty());
    ASSERT_EQ(member->workspace_dependencies.len(), usize(1));
    ASSERT_EQ(member->workspace_pkg_config_external_dependencies.len(), usize(1));
    ASSERT_EQ(member->workspace_cmake_external_dependencies.len(), usize(1));

    auto document = lito::load_manifest_document(directory.as_path());
    ASSERT_TRUE(document.is_ok());
    ASSERT_TRUE(document->workspace.is_some());
    ASSERT_EQ(document->workspace->dependencies.len(), usize(1));
    ASSERT_EQ(document->workspace->pkg_config_external_dependencies.len(), usize(1));
    ASSERT_EQ(document->workspace->cmake_external_dependencies.len(), usize(1));
    EXPECT_TRUE(document->workspace->dependencies[usize {}].source.is_Path());
    EXPECT_TRUE(document->workspace->cmake_external_dependencies[usize {}].source.is_Path());

    auto graph = lito::resolve_package_graph(directory.as_path());
    ASSERT_TRUE(graph.is_ok());
    ASSERT_EQ(graph->packages.len(), usize(2));
    const auto& app = graph->packages[usize {}];
    EXPECT_EQ(app.manifest.name.as_str(), "fixture-workspace-inherited-app"_str);
    EXPECT_TRUE(app.manifest.workspace_dependencies.is_empty());
    EXPECT_TRUE(app.manifest.workspace_pkg_config_external_dependencies.is_empty());
    EXPECT_TRUE(app.manifest.workspace_cmake_external_dependencies.is_empty());
    ASSERT_EQ(app.dependencies.len(), usize(1));
    EXPECT_EQ(app.dependencies[usize {}].name.as_str(), "fixture-workspace-inherited-library"_str);
    ASSERT_EQ(app.manifest.dependencies.len(), usize(1));
    ASSERT_TRUE(app.manifest.dependencies[usize {}].declaration_root.is_some());
    EXPECT_EQ(app.manifest.dependencies[usize {}].declaration_root->as_path(), directory.as_path());

    ASSERT_EQ(app.manifest.pkg_config_external_dependencies.len(), usize(1));
    const auto& curl = app.manifest.pkg_config_external_dependencies[usize {}];
    EXPECT_EQ(curl.alias.as_str(), "curl"_str);
    EXPECT_EQ(curl.requirement.module.as_str(), "libcurl"_str);
    EXPECT_EQ(curl.requirement.mode, lito::PkgConfigQueryMode::Static);
    EXPECT_EQ(curl.visibility, lito::DependencyVisibility::Public);

    ASSERT_EQ(app.manifest.cmake_external_dependencies.len(), usize(1));
    const auto& cmake = app.manifest.cmake_external_dependencies[usize {}];
    EXPECT_EQ(cmake.alias.as_str(), "fixture"_str);
    ASSERT_EQ(cmake.targets.len(), usize(1));
    EXPECT_EQ(cmake.targets[usize {}].name.as_str(), "LitoFixture::fixture"_str);
    EXPECT_EQ(cmake.integration, lito::CMakeIntegration::BuildTree);
    ASSERT_TRUE(cmake.adapter.is_some());
    EXPECT_EQ(cmake.adapter->as_path().to_str().unwrap(), "fixture-adapter.cmake"_str);
    ASSERT_TRUE(cmake.declaration_root.is_some());
    EXPECT_EQ(cmake.declaration_root->as_path(), directory.as_path());
    ASSERT_TRUE(cmake.adapter_root.is_some());
    EXPECT_EQ(cmake.adapter_root->as_path(), directory.as_path());

    ASSERT_TRUE(lito::prepare_external_dependency_sources(*graph, {}).is_ok());
    ASSERT_EQ(graph->packages[usize {}].cmake_external_dependencies.len(), usize(1));
    const auto& resolved = graph->packages[usize {}].cmake_external_dependencies[usize {}];
    ASSERT_TRUE(resolved.source.is_Directory());
    EXPECT_EQ(resolved.source.as_Directory().root.as_path(), root("cmake/package"_str).as_path());
    ASSERT_TRUE(resolved.adapter.is_some());
    EXPECT_EQ(resolved.adapter->as_path(),
              root("workspace/inherited-dependencies/fixture-adapter.cmake"_str).as_path());

    auto member_graph =
        lito::resolve_package_graph(root("workspace/inherited-dependencies/app"_str).as_path());
    ASSERT_TRUE(member_graph.is_ok());
    EXPECT_TRUE(member_graph->root_is_workspace);
    ASSERT_EQ(member_graph->packages.len(), usize(2));
    EXPECT_EQ(member_graph->packages[usize {}].dependencies.len(), usize(1));
}

TEST(Contracts, InvalidExplicitSourcesAreRejectedByDiscoveryOwner) {
    for (const auto path : INVALID_EXPLICIT_SOURCES) {
        auto loaded = lito::load_package_manifest(root(path).as_path());
        ASSERT_TRUE(loaded.is_ok());
        ASSERT_EQ(loaded->targets.len(), usize(1));
        auto target = lito::ResolvedTarget {
            .source      = rstd::move(lito::package_target_source(loaded->targets[usize {}])),
            .source_root = rstd::move(loaded->source_root),
        };
        auto discovered = lito::discover_explicit_sources(target);
        if (discovered.is_ok()) rstd::io::eprintln("unexpected valid sources: {}", path);
        EXPECT_TRUE(discovered.is_err());
    }
}

TEST(Contracts, TestAttachmentRequiresADirectLibraryDependency) {
    auto directory = root("manifest/test-attach-not-direct"_str);
    auto output    = output_root("test-attach-not-direct"_str);
    auto tested    = lito::test(lito::TestRequest {
        .build  = build_request(directory.as_path(), output.as_path(), Vec<String>::make()),
        .no_run = true,
    });
    ASSERT_TRUE(tested.is_err());
    EXPECT_TRUE(tested.unwrap_err().message.as_str().contains("direct dependency"_str));
    EXPECT_TRUE(clear_output(output.as_path()));
}

TEST(Contracts, OnlyCurrentLockVersionIsAcceptedByLockStore) {
    EXPECT_TRUE(locked_graph_is_current("lock/default-update"_str));
    for (const auto path : INVALID_LOCKS) {
        auto current = locked_graph_is_current(path);
        if (current) rstd::io::eprintln("unexpected current lock: {}", path);
        EXPECT_FALSE(current);
    }
    auto old_version = lito::load_lock_session(root("lock/v3"_str).as_path(), false);
    ASSERT_TRUE(old_version.is_err());
    EXPECT_TRUE(old_version.unwrap_err().message.as_str().contains("integer 4"_str));
}

TEST(Contracts, BuildResolutionReusesLockedGitSources) {
    auto directory = root("lock/git-update"_str);

    auto building = lito::load_lock_session(directory.as_path(), false);
    ASSERT_TRUE(building.is_ok());
    auto building_options = building->take_resolution_options();
    EXPECT_FALSE(building_options.locked);
    EXPECT_EQ(building_options.git, lito::GitResolutionMode::ReuseLocked);
    ASSERT_EQ(building_options.git_sources.len(), usize(1));
    EXPECT_EQ(building_options.git_sources[usize()].commit.as_str(),
              "0000000000000000000000000000000000000001"_str);

    auto updating =
        lito::load_lock_session(directory.as_path(), false, lito::GitResolutionMode::Refresh);
    ASSERT_TRUE(updating.is_ok());
    auto updating_options = updating->take_resolution_options();
    EXPECT_FALSE(updating_options.locked);
    EXPECT_EQ(updating_options.git, lito::GitResolutionMode::Refresh);
    ASSERT_EQ(updating_options.git_sources.len(), usize(1));

    auto locked = lito::load_lock_session(directory.as_path(), true);
    ASSERT_TRUE(locked.is_ok());
    auto locked_options = locked->take_resolution_options();
    EXPECT_TRUE(locked_options.locked);
    ASSERT_EQ(locked_options.git_sources.len(), usize(1));
    EXPECT_EQ(locked_options.git_sources[usize()].commit.as_str(),
              "0000000000000000000000000000000000000001"_str);

    auto pinned = lito::load_lock_session(root("lock/git-commit"_str).as_path(), false);
    ASSERT_TRUE(pinned.is_ok());
    auto pinned_options = pinned->take_resolution_options();
    ASSERT_EQ(pinned_options.git_sources.len(), usize(1));
    EXPECT_EQ(pinned_options.git_sources[usize()].reference.kind, lito::GitReferenceKind::Commit);
    EXPECT_EQ(pinned_options.git_sources[usize()].reference.value.as_str(),
              "1111111111111111111111111111111111111111"_str);
}

TEST(Contracts, GitUpdateRefreshesFloatingReferencesButKeepsCommitPins) {
    auto repository = output_root("git-resolution"_str);
    ASSERT_TRUE(clear_output(repository.as_path()));
    ASSERT_TRUE(rstd::fs::create_dir_all(repository.as_path()).is_ok());
    ASSERT_TRUE(git_succeeds(repository.as_path(), "init"_str));
    ASSERT_TRUE(git_succeeds(repository.as_path(), "config"_str, "user.name"_str, "Lito Test"_str));
    ASSERT_TRUE(git_succeeds(
        repository.as_path(), "config"_str, "user.email"_str, "lito@example.invalid"_str));
    auto content = repository.join(rstd::path::PathBuf::from("content.txt"_str).as_path());
    ASSERT_TRUE(rstd::fs::write(content.as_path(), "first\n"_str.as_bytes()).is_ok());
    ASSERT_TRUE(git_succeeds(repository.as_path(), "add"_str, "content.txt"_str));
    ASSERT_TRUE(git_succeeds(repository.as_path(),
                             "-c"_str,
                             "commit.gpgsign=false"_str,
                             "commit"_str,
                             "-m"_str,
                             "first"_str));
    auto previous = git_revision(repository.as_path(), "HEAD"_str);
    ASSERT_TRUE(previous.is_some());
    ASSERT_TRUE(rstd::fs::write(content.as_path(), "second\n"_str.as_bytes()).is_ok());
    ASSERT_TRUE(git_succeeds(repository.as_path(), "add"_str, "content.txt"_str));
    ASSERT_TRUE(git_succeeds(repository.as_path(),
                             "-c"_str,
                             "commit.gpgsign=false"_str,
                             "commit"_str,
                             "-m"_str,
                             "second"_str));
    auto url = repository.as_path().to_str();
    ASSERT_TRUE(url.is_some());
    auto current = git_revision(repository.as_path(), "HEAD"_str);
    ASSERT_TRUE(current.is_some());
    ASSERT_NE(current->as_str(), previous->as_str());

    auto locked_sources = Vec<lito::LockedGitSource>::make();
    locked_sources.push(lito::LockedGitSource {
        .git       = String::make(*url),
        .reference = lito::GitReference {},
        .commit    = previous->clone(),
    });
    auto reuse_graph = external_git_graph(*url, lito::GitReference {});
    reuse_graph.packages[usize {}].manifest.cmake_external_dependencies.push(
        lito::CMakeDependencyRequirement {
            .alias   = String::make("fixture-reuse"_str),
            .package = String::make("Fixture"_str),
            .source  = lito::CMakeDependencySource::Git(String::make(*url), lito::GitReference {}),
        });
    auto reused =
        lito::prepare_external_dependency_sources(reuse_graph,
                                                  lito::PackageResolutionOptions {
                                                      .git_sources = rstd::move(locked_sources),
                                                  },
                                                  usize(2));
    ASSERT_TRUE(reused.is_ok());
    ASSERT_EQ(reuse_graph.sources.len(), usize(1));
    ASSERT_EQ(reuse_graph.packages[usize {}].cmake_external_dependencies.len(), usize(2));
    EXPECT_EQ(reuse_graph.packages[usize {}]
                  .cmake_external_dependencies[usize {}]
                  .source.as_Directory()
                  .identity,
              reuse_graph.packages[usize {}]
                  .cmake_external_dependencies[usize(1)]
                  .source.as_Directory()
                  .identity);
    auto reused_commit = resolved_git_commit(reuse_graph);
    ASSERT_TRUE(reused_commit.is_some());
    EXPECT_EQ(*reused_commit, previous->as_str());

    locked_sources.push(lito::LockedGitSource {
        .git       = String::make(*url),
        .reference = lito::GitReference {},
        .commit    = previous->clone(),
    });
    auto update_graph = external_git_graph(*url, lito::GitReference {});
    auto updated =
        lito::prepare_external_dependency_sources(update_graph,
                                                  lito::PackageResolutionOptions {
                                                      .git = lito::GitResolutionMode::Refresh,
                                                      .git_sources = rstd::move(locked_sources),
                                                  });
    ASSERT_TRUE(updated.is_ok());
    auto updated_commit = resolved_git_commit(update_graph);
    ASSERT_TRUE(updated_commit.is_some());
    EXPECT_EQ(*updated_commit, current->as_str());

    auto pinned_graph = external_git_graph(*url,
                                           lito::GitReference {
                                               .kind  = lito::GitReferenceKind::Commit,
                                               .value = previous->clone(),
                                           });
    auto pinned =
        lito::prepare_external_dependency_sources(pinned_graph,
                                                  lito::PackageResolutionOptions {
                                                      .git = lito::GitResolutionMode::Refresh,
                                                  });
    ASSERT_TRUE(pinned.is_ok());
    auto pinned_commit = resolved_git_commit(pinned_graph);
    ASSERT_TRUE(pinned_commit.is_some());
    EXPECT_EQ(*pinned_commit, previous->as_str());
    EXPECT_TRUE(clear_output(repository.as_path()));
}

TEST(Contracts, DependencyUpdateOwnsExplicitLockRefresh) {
    auto updated = lito::update_dependencies(lito::UpdateRequest {
        .root = root("lock/default-update"_str),
    });
    ASSERT_TRUE(updated.is_ok());
    EXPECT_EQ(*updated, lito::LockStatus::Unchanged);
}

TEST(Contracts, DiscoveryAndModuleConventionsBuildExpectedCases) {
    auto output = output_root("contracts-build"_str);
    ASSERT_TRUE(clear_output(output.as_path()));
    for (const auto path : VALID_BUILD_CASES) {
        auto directory = root(path);
        auto built =
            lito::build(build_request(directory.as_path(), output.as_path(), Vec<String>::make()));
        if (built.is_err()) rstd::io::eprintln("unexpected build failure: {}", path);
        EXPECT_TRUE(built.is_ok());
    }
    for (const auto path : INVALID_BUILD_CASES) {
        auto directory = root(path);
        auto built =
            lito::build(build_request(directory.as_path(), output.as_path(), Vec<String>::make()));
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
        lito::build(build_request(directory.as_path(), output.as_path(), Vec<String>::make()));
    ASSERT_TRUE(debug.is_ok());
    auto debug_executable = executable(*debug);
    ASSERT_TRUE(debug_executable.is_some());
    auto debug_status = rstd::process::Command::make((*debug_executable).as_os_str())
                            .current_dir(directory.as_path())
                            .status();
    ASSERT_TRUE(debug_status.is_ok());
    ASSERT_TRUE(debug_status->code().is_some());
    EXPECT_EQ(*debug_status->code(), i32(1));

    auto release = lito::build(build_request(
        directory.as_path(), output.as_path(), Vec<String>::make(), build_profile("release"_str)));
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
