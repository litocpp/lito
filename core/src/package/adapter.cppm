module;
#include <rstd/macro.hpp>

export module lito.package:adapter;

import rstd;
import lito.error;
import lito.cpp;
import lito.manifest.contract;
import lito.workspace.contract;
import lito.source.discovery_contract;
import lito.toolchain.spec;
import lito.package.identity;
import lito.build.profile_contract;
import lito.build.configuration;
import lito.platform.contract;
import lito.package.graph_contract;
import lito.package.target_contract;
import lito.package.error_contract;
import lito.dependency;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace lito
{

template<typename T>
auto adapter_failure(String message) -> PackageResult<T> {
    return Err(PackageError::Message(rstd::move(message)));
}

auto parse_options(const CppArgumentParser& parser, const Vec<String>& options, String source)
    -> PackageResult<CppArgumentLayer> {
    auto parsed = rstd_try(parser.parse(options, source.as_str()));
    return Ok(rstd::move(parsed));
}

auto output_name(ArtifactKind kind, ref<str> declared_name) -> String {
    if (kind != ArtifactKind::StaticLibrary && kind != ArtifactKind::TestAttachmentArchive) {
        return String::make(declared_name);
    }
    auto result = String::make("lib"_str);
    result.push_str(declared_name);
    result.push_str(kind == ArtifactKind::StaticLibrary ? ".a"_str : ".test.a"_str);
    return result;
}

auto contains_source(const Vec<PathBuf>& sources, ref<rstd::path::Path> candidate) -> bool {
    for (const auto& source : sources) {
        if (source.as_path() == candidate) return true;
    }
    return false;
}

auto clone_usage(const UsageRequirements& usage) -> UsageRequirements {
    auto include_requirements = Vec<IncludeDirectoryRequirement>::with_capacity(
        usage.private_include_directory_requirements.len());
    for (const auto& requirement : usage.private_include_directory_requirements) {
        include_requirements.push(requirement.clone());
    }
    return UsageRequirements {
        .public_include_directories =
            as<rstd::clone::Clone>(usage.public_include_directories).clone(),
        .private_include_directories =
            as<rstd::clone::Clone>(usage.private_include_directories).clone(),
        .public_definitions     = as<rstd::clone::Clone>(usage.public_definitions).clone(),
        .private_definitions    = as<rstd::clone::Clone>(usage.private_definitions).clone(),
        .public_options         = as<rstd::clone::Clone>(usage.public_options).clone(),
        .private_options        = as<rstd::clone::Clone>(usage.private_options).clone(),
        .public_arguments       = as<rstd::clone::Clone>(usage.public_arguments).clone(),
        .private_arguments      = as<rstd::clone::Clone>(usage.private_arguments).clone(),
        .private_linker_options = as<rstd::clone::Clone>(usage.private_linker_options).clone(),
        .private_include_directory_requirements = rstd::move(include_requirements),
    };
}

auto clone_dependencies(const Vec<DependencySpec>& dependencies) -> Vec<DependencySpec> {
    auto result = Vec<DependencySpec>::with_capacity(dependencies.len());
    for (const auto& dependency : dependencies) {
        result.push(DependencySpec {
            .target     = dependency.target.clone(),
            .visibility = dependency.visibility,
        });
    }
    return result;
}

auto clone_external_dependencies(const Vec<ResolvedExternalDependency>& dependencies)
    -> Vec<ResolvedExternalDependency> {
    auto result = Vec<ResolvedExternalDependency>::with_capacity(dependencies.len());
    for (const auto& dependency : dependencies) result.push(dependency.clone());
    return result;
}

auto target_artifact_kind(PackageTargetKind kind) -> ArtifactKind {
    switch (kind) {
    case PackageTargetKind::Library: return ArtifactKind::StaticLibrary;
    case PackageTargetKind::Binary: return ArtifactKind::Executable;
    case PackageTargetKind::Test: return ArtifactKind::TestExecutable;
    case PackageTargetKind::Benchmark: return ArtifactKind::BenchmarkExecutable;
    case PackageTargetKind::TestAttachment: return ArtifactKind::TestAttachmentArchive;
    case PackageTargetKind::CompileTest: return ArtifactKind::CompileTest;
    }
    return ArtifactKind::Executable;
}

auto development_target(PackageTargetKind kind) noexcept -> bool {
    return kind == PackageTargetKind::Test || kind == PackageTargetKind::Benchmark ||
           kind == PackageTargetKind::CompileTest;
}

auto library_targets(const ResolvedPackageGraph& graph)
    -> rstd::collections::BTreeMap<String, PackageTargetId> {
    auto result = rstd::collections::BTreeMap<String, PackageTargetId>::make();
    for (const auto& package : graph.packages) {
        for (const auto& target : package.manifest.targets) {
            if (package_target_kind(target) != PackageTargetKind::Library) continue;
            result.insert(package.manifest.name.clone(),
                          PackageTargetId {
                              .package = package.manifest.name.clone(),
                              .kind    = PackageTargetKind::Library,
                              .name    = String::make(package_target_name(target)),
                          });
            break;
        }
    }
    return result;
}

auto selected_target(const Vec<PackageTargetId>& selected, const PackageTargetId& target) -> bool {
    for (const auto& candidate : selected) {
        if (candidate == target) return true;
    }
    return false;
}

} // namespace lito

export namespace lito
{

auto adapt_package_graph_metadata(ResolvedPackageGraph        graph,
                                  const Vec<String>&          selected_package_names,
                                  const Vec<PackageTargetId>& selected_targets,
                                  const BuildConfiguration&   configuration,
                                  ProfileSpec                 profile,
                                  const BuildPlatform&        platform,
                                  ExternalUsageCatalog        external_usage,
                                  const CppArgumentParser&    argument_parser)
    -> PackageResult<PackageMetadata> {
    if (! is_supported_cpp_standard(configuration.language_standard.as_str()) ||
        configuration.toolchain.cxx.is_empty() || configuration.toolchain.ld.is_empty() ||
        configuration.toolchain.ar.is_empty()) {
        return adapter_failure<PackageMetadata>(
            String::make("invalid build configuration for package graph"_str));
    }
    auto libraries = library_targets(graph);

    auto selected = rstd::collections::BTreeMap<String, empty>::make();
    for (const auto& name : selected_package_names) selected.insert(name.clone(), empty {});

    auto external_by_package =
        Vec<Vec<ResolvedExternalDependency>>::with_capacity(graph.packages.len());
    for (usize index {}; index < graph.packages.len(); ++index) {
        external_by_package.emplace_back();
        if (! selected.contains_key(graph.packages[index].manifest.name.as_str())) continue;
        external_by_package[index] =
            rstd_try(external_usage.take(graph.packages[index].manifest.name.as_str()));
    }
    if (! external_usage.all_consumed()) {
        return adapter_failure<PackageMetadata>(
            String::make("external usage catalog contains an unselected package"_str));
    }

    for (const auto& package : graph.packages) {
        for (const auto& dependency : package.dependencies) {
            if (! libraries.contains_key(dependency.name.as_str())) {
                return adapter_failure<PackageMetadata>(
                    rstd::format("package '{}' depends on package '{}' which does not expose a "
                                 "library target",
                                 package.manifest.name.as_str(),
                                 dependency.name.as_str()));
            }
        }
        for (const auto& dependency : package.dev_dependencies) {
            if (! libraries.contains_key(dependency.name.as_str())) {
                return adapter_failure<PackageMetadata>(
                    rstd::format("package '{}' has development dependency '{}' which does not "
                                 "expose a library target",
                                 package.manifest.name.as_str(),
                                 dependency.name.as_str()));
            }
        }
    }

    auto targets     = Vec<ResolvedTarget>::make();
    auto build_tools = Vec<PackageBuildToolRequirement>::make();
    for (usize package_index {}; package_index < graph.packages.len(); ++package_index) {
        auto& package = graph.packages[package_index];
        if (! selected.contains_key(package.manifest.name.as_str())) continue;
        for (auto& requirement : package.manifest.build_tools) {
            build_tools.push(PackageBuildToolRequirement {
                .package     = package.manifest.name.clone(),
                .root        = package.manifest.root.clone(),
                .requirement = rstd::move(requirement),
            });
        }
        auto public_arguments                    = rstd_try(parse_options(
            argument_parser,
            package.manifest.usage.public_options,
            rstd::format("package '{}'.public-options", package.manifest.name.as_str())));
        auto private_arguments                   = rstd_try(parse_options(
            argument_parser,
            package.manifest.usage.private_options,
            rstd::format("package '{}'.private-options", package.manifest.name.as_str())));
        package.manifest.usage.public_arguments  = rstd::move(public_arguments);
        package.manifest.usage.private_arguments = rstd::move(private_arguments);
        package.manifest.usage.public_options.clear();
        package.manifest.usage.private_options.clear();
        for (auto& test : package.manifest.compile_tests) {
            auto arguments = rstd_try(parse_options(argument_parser,
                                                    test.options,
                                                    rstd::format("package '{}'.compile-test '{}'",
                                                                 package.manifest.name.as_str(),
                                                                 test.name.as_str())));
            test.arguments = rstd::move(arguments);
            test.options.clear();
        }
        auto dependencies = Vec<DependencySpec>::with_capacity(package.dependencies.len());
        for (const auto& dependency : package.dependencies) {
            if (! selected.contains_key(dependency.name.as_str())) {
                return adapter_failure<PackageMetadata>(
                    rstd::format("resolved dependency '{}' is missing", dependency.name.as_str()));
            }
            auto library = libraries.get(dependency.name.as_str());
            if (library.is_none()) {
                return adapter_failure<PackageMetadata>(rstd::format(
                    "resolved dependency '{}' has no library target", dependency.name.as_str()));
            }
            dependencies.push(DependencySpec {
                .target     = (**library).clone(),
                .visibility = dependency.visibility,
            });
        }
        auto development_selected = false;
        for (const auto& selected_target : selected_targets) {
            if (selected_target.package == package.manifest.name.as_str() &&
                development_target(selected_target.kind)) {
                development_selected = true;
                break;
            }
        }
        auto dev_dependencies = Vec<DependencySpec>::with_capacity(package.dev_dependencies.len());
        if (development_selected) {
            for (const auto& dependency : package.dev_dependencies) {
                if (! selected.contains_key(dependency.name.as_str())) {
                    return adapter_failure<PackageMetadata>(
                        rstd::format("resolved development dependency '{}' is missing",
                                     dependency.name.as_str()));
                }
                auto library = libraries.get(dependency.name.as_str());
                if (library.is_none()) {
                    return adapter_failure<PackageMetadata>(
                        rstd::format("resolved development dependency '{}' has no library target",
                                     dependency.name.as_str()));
                }
                dev_dependencies.push(DependencySpec {
                    .target     = (**library).clone(),
                    .visibility = DependencyVisibility::Private,
                });
            }
        }
        auto own_library = libraries.get(package.manifest.name.as_str());
        for (auto& manifest_target : package.manifest.targets) {
            const auto kind = package_target_kind(manifest_target);
            auto       id   = PackageTargetId {
                .package = package.manifest.name.clone(),
                .kind    = kind,
                .name    = String::make(package_target_name(manifest_target)),
            };
            auto& source = package_target_source(manifest_target);
            for (auto& group : source.conditional_source_groups) {
                if (! group.predicate.matches(platform.effective_target)) continue;
                for (auto& path : group.sources) source.declared_sources.push(rstd::move(path));
            }
            source.conditional_source_groups.clear();
            auto attachments = Vec<TestAttachmentManifest>::make();
            if (manifest_target.is_Test()) {
                attachments = rstd::move(manifest_target.as_Test().attachments);
                for (auto& attachment : attachments) {
                    for (auto& group : attachment.conditional_source_groups) {
                        if (! group.predicate.matches(platform.effective_target)) continue;
                        for (auto& path : group.sources) {
                            attachment.sources.push(rstd::move(path));
                        }
                    }
                    attachment.conditional_source_groups.clear();
                }
            }
            auto runtime_resources = Vec<RuntimeResourceManifest>::make();
            if (manifest_target.is_Binary())
                runtime_resources = rstd::move(manifest_target.as_Binary().resources);
            auto target_dependencies = clone_dependencies(dependencies);
            if (development_target(kind)) {
                for (const auto& dependency : dev_dependencies) {
                    target_dependencies.push(DependencySpec {
                        .target     = dependency.target.clone(),
                        .visibility = DependencyVisibility::Private,
                    });
                }
            }
            if (kind != PackageTargetKind::Library && own_library.is_some()) {
                target_dependencies.push(DependencySpec {
                    .target     = (**own_library).clone(),
                    .visibility = DependencyVisibility::Private,
                });
            }
            targets.push(ResolvedTarget {
                .id            = rstd::move(id),
                .artifact_kind = target_artifact_kind(kind),
                .artifact_name = String::make(package_target_artifact_name(manifest_target)),
                .link_stdlib   = package_target_links_stdlib(manifest_target),
                .source        = rstd::move(source),
                .root          = package.manifest.root.clone(),
                .source_root   = package.manifest.source_root.clone(),
                .usage         = clone_usage(package.manifest.usage),
                .attachments   = rstd::move(attachments),
                .runtime_resources = rstd::move(runtime_resources),
                .dependencies  = rstd::move(target_dependencies),
                .external_dependencies =
                    clone_external_dependencies(external_by_package[package_index]),
            });
        }
        if (! package.manifest.compile_tests.is_empty()) {
            auto sources = Vec<PathBuf>::with_capacity(package.manifest.compile_tests.len());
            for (const auto& test : package.manifest.compile_tests)
                sources.push(test.source.clone());
            auto compile_dependencies = clone_dependencies(dependencies);
            for (const auto& dependency : dev_dependencies) {
                compile_dependencies.push(DependencySpec {
                    .target     = dependency.target.clone(),
                    .visibility = DependencyVisibility::Private,
                });
            }
            if (own_library.is_some()) {
                compile_dependencies.push(DependencySpec {
                    .target     = (**own_library).clone(),
                    .visibility = DependencyVisibility::Private,
                });
            }
            targets.push(ResolvedTarget {
                .id =
                    PackageTargetId {
                        .package = package.manifest.name.clone(),
                        .kind    = PackageTargetKind::CompileTest,
                        .name    = package.manifest.name.clone(),
                    },
                .artifact_kind = ArtifactKind::CompileTest,
                .artifact_name = package.manifest.name.clone(),
                .source =
                    TargetSourceManifest {
                        .discovery        = SourceDiscoveryMode::Explicit,
                        .declared_sources = rstd::move(sources),
                    },
                .root          = package.manifest.root.clone(),
                .source_root   = package.manifest.source_root.clone(),
                .usage         = clone_usage(package.manifest.usage),
                .compile_tests = rstd::move(package.manifest.compile_tests),
                .dependencies  = rstd::move(compile_dependencies),
                .external_dependencies =
                    clone_external_dependencies(external_by_package[package_index]),
            });
        }
    }

    const auto real_target_count = targets.len();
    auto       attachments       = Vec<ResolvedTarget>::make();
    for (usize test_index {}; test_index < real_target_count; ++test_index) {
        const auto& test = targets[test_index];
        if (test.artifact_kind != ArtifactKind::TestExecutable ||
            ! selected_target(selected_targets, test.id)) {
            continue;
        }
        for (const auto& declaration : test.attachments) {
            auto direct = false;
            for (const auto& dependency : test.dependencies) {
                if (dependency.target.package == declaration.package.as_str()) {
                    direct = true;
                    break;
                }
            }
            if (! direct) {
                return adapter_failure<PackageMetadata>(rstd::format(
                    "test target '{}::{}' can only attach a direct dependency, but '{}' is not one",
                    test.id.package.as_str(),
                    test.id.name.as_str(),
                    declaration.package.as_str()));
            }
            auto library_id = libraries.get(declaration.package.as_str());
            if (library_id.is_none()) {
                return adapter_failure<PackageMetadata>(rstd::format(
                    "test attachment dependency '{}' is missing", declaration.package.as_str()));
            }
            const ResolvedTarget* library = nullptr;
            for (const auto& candidate : targets) {
                if (candidate.id == **library_id) {
                    library = rstd::addressof(candidate);
                    break;
                }
            }
            if (library == nullptr || library->artifact_kind != ArtifactKind::StaticLibrary) {
                return adapter_failure<PackageMetadata>(
                    rstd::format("test target '{}::{}' cannot attach non-library package '{}'",
                                 test.id.package.as_str(),
                                 test.id.name.as_str(),
                                 declaration.package.as_str()));
            }
            auto sources = Vec<PathBuf>::make();
            for (const auto& source : declaration.sources) {
                if (contains_source(sources, source.as_path())) {
                    return adapter_failure<PackageMetadata>(
                        rstd::format("test attachment '{}' repeats source '{}'",
                                     declaration.package.as_str(),
                                     source.as_path()));
                }
                if (contains_source(test.source.declared_sources, source.as_path())) {
                    return adapter_failure<PackageMetadata>(
                        rstd::format("test source '{}' cannot also attach to package '{}'",
                                     source.as_path(),
                                     declaration.package.as_str()));
                }
                sources.push(source.clone());
            }
            if (sources.is_empty()) continue;
            auto synthetic_name = rstd::format("{}@test-attach@{}@{}",
                                               test.id.name.as_str(),
                                               library->id.package.as_str(),
                                               library->id.name.as_str());
            attachments.push(ResolvedTarget {
                .id =
                    PackageTargetId {
                        .package = test.id.package.clone(),
                        .kind    = PackageTargetKind::TestAttachment,
                        .name    = rstd::move(synthetic_name),
                    },
                .artifact_kind = ArtifactKind::TestAttachmentArchive,
                .artifact_name = library->artifact_name.clone(),
                .source =
                    TargetSourceManifest {
                        .module           = library->source.module.clone(),
                        .discovery        = SourceDiscoveryMode::Explicit,
                        .declared_sources = rstd::move(sources),
                    },
                .root            = test.root.clone(),
                .source_root     = test.source_root.clone(),
                .test_attachment = Some(TestAttachmentTarget {
                    .test_target    = test.id.clone(),
                    .library_target = library->id.clone(),
                }),
            });
        }
    }
    for (auto& attachment : attachments) targets.push(rstd::move(attachment));

    auto default_targets = Vec<PackageTargetId>::with_capacity(selected_targets.len());
    for (const auto& target : selected_targets) {
        if (! selected.contains_key(target.package.as_str())) {
            return adapter_failure<PackageMetadata>(
                rstd::format("selected root package '{}' is missing", target.package.as_str()));
        }
        default_targets.push(target.clone());
    }
    auto build_script_packages = Vec<String>::make();
    for (const auto& root : graph.roots) {
        if (root.role == ProjectRootRole::AssociatedTest) continue;
        for (const auto& target : selected_targets) {
            if (target.package != root.name.as_str()) continue;
            build_script_packages.push(root.name.clone());
            break;
        }
    }
    auto selected_packages      = Vec<SelectedPackageMetadata>::make();
    auto selected_root_packages = rstd::collections::BTreeMap<String, empty>::make();
    for (const auto& target : selected_targets) {
        selected_root_packages.insert(target.package.clone(), empty {});
    }
    for (const auto& package : graph.packages) {
        if (! selected_root_packages.contains_key(package.manifest.name.as_str())) continue;
        auto version = Option<String> {};
        if (package.manifest.version.value.is_some()) {
            version = Some(package.manifest.version.value->clone());
        }
        selected_packages.push(SelectedPackageMetadata {
            .name            = package.manifest.name.clone(),
            .version         = rstd::move(version),
            .source_identity = package.source_identity.clone(),
            .root            = package.manifest.root.clone(),
        });
    }
    auto profiles        = Vec<ProfileSpec>::make();
    auto default_profile = profile.name.clone();
    profiles.push(rstd::move(profile));
    return Ok(PackageMetadata {
        .name                  = rstd::move(graph.name),
        .root                  = rstd::move(graph.root_directory),
        .manifest_path         = rstd::move(graph.manifest_path),
        .build_script_packages = rstd::move(build_script_packages),
        .default_profile       = rstd::move(default_profile),
        .default_targets       = rstd::move(default_targets),
        .selected_packages     = rstd::move(selected_packages),
        .build_tools           = rstd::move(build_tools),
        .toolchain =
            ToolchainSpec {
                .cc     = configuration.toolchain.cc.clone(),
                .cxx    = configuration.toolchain.cxx.clone(),
                .ld     = configuration.toolchain.ld.clone(),
                .ar     = configuration.toolchain.ar.clone(),
                .strip  = configuration.toolchain.strip.clone(),
                .format = configuration.toolchain.format.clone(),
            },
        .profiles = rstd::move(profiles),
        .targets  = rstd::move(targets),
    });
}

auto finalize_package(PackageMetadata metadata, Vec<ResolvedTargetSources> source_sets)
    -> PackageResult<PackageSpec> {
    for (usize index {}; index < source_sets.len(); ++index) {
        for (usize prior {}; prior < index; ++prior) {
            if (source_sets[prior].target == source_sets[index].target) {
                return adapter_failure<PackageSpec>(
                    rstd::format("source discovery repeated target '{}::{}::{}'",
                                 source_sets[index].target.package.as_str(),
                                 package_target_kind_name(source_sets[index].target.kind),
                                 source_sets[index].target.name.as_str()));
            }
        }
    }

    auto targets = Vec<TargetSpec>::with_capacity(metadata.targets.len());
    for (auto& target : metadata.targets) {
        auto source_position = Option<usize> {};
        for (usize index {}; index < source_sets.len(); ++index) {
            if (source_sets[index].target == target.id) {
                source_position = Some(index);
                break;
            }
        }
        auto source_set = ResolvedSourceSet {};
        if (source_position.is_some()) {
            source_set = rstd::move(source_sets[*source_position].sources);
        }
        auto sources = Vec<TargetSource>::with_capacity(source_set.sources.len());
        for (auto& source : source_set.sources) {
            sources.push(TargetSource {
                .relative_path     = rstd::move(source.relative_path),
                .path              = rstd::move(source.canonical_path),
                .expected_module   = rstd::move(source.expected_module),
                .frontend_analysis = rstd::move(source.frontend_analysis),
            });
        }
        auto artifact_name = output_name(target.artifact_kind, target.artifact_name.as_str());
        auto archive_stem  = target.artifact_name.clone();
        targets.push(TargetSpec {
            .id                    = rstd::move(target.id),
            .artifact_kind         = target.artifact_kind,
            .artifact_name         = rstd::move(artifact_name),
            .link_stdlib           = target.link_stdlib,
            .archive_stem          = rstd::move(archive_stem),
            .module_affiliation    = rstd::move(target.source.module),
            .root                  = rstd::move(target.root),
            .source_root           = rstd::move(target.source_root),
            .sources               = rstd::move(sources),
            .dependencies          = rstd::move(target.dependencies),
            .external_dependencies = rstd::move(target.external_dependencies),
            .usage                 = rstd::move(target.usage),
            .compile_tests         = rstd::move(target.compile_tests),
            .test_attachment       = rstd::move(target.test_attachment),
        });
    }

    return Ok(PackageSpec {
        .name            = rstd::move(metadata.name),
        .root            = rstd::move(metadata.root),
        .manifest_path   = rstd::move(metadata.manifest_path),
        .default_profile = rstd::move(metadata.default_profile),
        .default_targets = rstd::move(metadata.default_targets),
        .toolchain       = rstd::move(metadata.toolchain),
        .profiles        = rstd::move(metadata.profiles),
        .targets         = rstd::move(targets),
    });
}

} // namespace lito
