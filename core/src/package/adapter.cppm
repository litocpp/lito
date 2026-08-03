module;
#include <rstd/macro.hpp>

export module tenon.package:adapter;

import rstd;
import tenon.model;
import tenon.build_profile;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace tenon
{

template<typename T>
auto adapter_failure(String message) -> Result<T> {
    return Err(Error::make(ErrorKind::Manifest, rstd::move(message)));
}

auto parse_options(const CppArgumentParser& parser, const Vec<String>& options, String source)
    -> Result<CppArgumentLayer> {
    auto parsed = rstd_try(parser.parse(options, source.as_str()), [](String error) {
        return Error::make(ErrorKind::Manifest, rstd::move(error));
    });
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

} // namespace tenon

export namespace tenon
{

auto adapt_package_graph_metadata(ResolvedPackageGraph      graph,
                                  const Vec<String>&        selected_package_names,
                                  const Vec<String>&        selected_root_names,
                                  const BuildConfiguration& configuration,
                                  const TargetInfo&         target_info,
                                  const CppArgumentParser&  argument_parser)
    -> Result<PackageMetadata> {
    if (! is_supported_cpp_standard(configuration.language_standard.as_str()) ||
        configuration.toolchain.compiler.is_empty() ||
        configuration.toolchain.archiver.is_empty()) {
        return adapter_failure<PackageMetadata>(
            String::make("invalid build configuration for package graph"_str));
    }
    auto profile = rstd_try(make_profile_spec(configuration, argument_parser));

    auto selected        = rstd::collections::BTreeMap<String, empty>::make();
    auto roots           = rstd::collections::BTreeMap<String, empty>::make();
    auto requested_roots = rstd::collections::BTreeMap<String, empty>::make();
    for (const auto& name : selected_package_names) selected.insert(name.clone(), empty {});
    for (const auto& name : graph.root_names) roots.insert(name.clone(), empty {});
    for (const auto& name : selected_root_names) requested_roots.insert(name.clone(), empty {});

    auto artifact_kinds = rstd::collections::BTreeMap<String, ArtifactKind>::make();
    for (const auto& package : graph.packages) {
        artifact_kinds.insert(package.manifest.name.clone(), package.manifest.artifact_kind);
    }

    for (const auto& package : graph.packages) {
        if (package.manifest.artifact_kind != ArtifactKind::StaticLibrary &&
            ! roots.contains_key(package.manifest.name.as_str())) {
            return adapter_failure<PackageMetadata>(
                rstd::format("dependency package '{}' cannot produce an executable artifact",
                             package.manifest.name.as_str()));
        }
        for (const auto& dependency : package.dependencies) {
            auto kind = artifact_kinds.get(dependency.name.as_str());
            if (kind.is_none()) {
                return adapter_failure<PackageMetadata>(
                    rstd::format("resolved dependency '{}' is missing", dependency.name.as_str()));
            }
            if (**kind != ArtifactKind::StaticLibrary) {
                return adapter_failure<PackageMetadata>(
                    rstd::format("package '{}' cannot depend on non-library package '{}'",
                                 package.manifest.name.as_str(),
                                 dependency.name.as_str()));
            }
        }
    }

    auto targets = Vec<TargetMetadata>::with_capacity(selected_package_names.len());
    for (auto& package : graph.packages) {
        if (! selected.contains_key(package.manifest.name.as_str())) continue;
        for (auto& group : package.manifest.conditional_source_groups) {
            if (! group.predicate.matches(target_info)) continue;
            for (auto& source : group.sources) {
                package.manifest.declared_sources.push(rstd::move(source));
            }
        }
        package.manifest.conditional_source_groups.clear();
        for (auto& attachment : package.manifest.test_attachments) {
            for (auto& group : attachment.conditional_source_groups) {
                if (! group.predicate.matches(target_info)) continue;
                for (auto& source : group.sources) {
                    attachment.sources.push(rstd::move(source));
                }
            }
            attachment.conditional_source_groups.clear();
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
            dependencies.push(DependencySpec {
                .target     = dependency.name.clone(),
                .visibility = dependency.visibility,
            });
        }
        targets.push(TargetMetadata {
            .manifest     = rstd::move(package.manifest),
            .dependencies = rstd::move(dependencies),
        });
    }

    auto target_ids = rstd::collections::BTreeMap<String, usize>::make();
    for (usize index {}; index < targets.len(); ++index) {
        target_ids.insert(targets[index].manifest.name.clone(), index);
    }
    const auto real_target_count = targets.len();
    auto       attachments       = Vec<TargetMetadata>::make();
    for (usize test_index {}; test_index < real_target_count; ++test_index) {
        const auto& test = targets[test_index];
        if (test.manifest.artifact_kind != ArtifactKind::TestExecutable ||
            ! requested_roots.contains_key(test.manifest.name.as_str())) {
            continue;
        }
        for (const auto& declaration : test.manifest.test_attachments) {
            auto direct = false;
            for (const auto& dependency : test.dependencies) {
                if (dependency.target == declaration.package.as_str()) {
                    direct = true;
                    break;
                }
            }
            if (! direct) {
                return adapter_failure<PackageMetadata>(rstd::format(
                    "test package '{}' can only attach a direct dependency, but '{}' is not one",
                    test.manifest.name.as_str(),
                    declaration.package.as_str()));
            }
            auto library_index = target_ids.get(declaration.package.as_str());
            if (library_index.is_none()) {
                return adapter_failure<PackageMetadata>(rstd::format(
                    "test attachment dependency '{}' is missing", declaration.package.as_str()));
            }
            const auto& library = targets[**library_index];
            if (library.manifest.artifact_kind != ArtifactKind::StaticLibrary) {
                return adapter_failure<PackageMetadata>(
                    rstd::format("test package '{}' cannot attach non-library package '{}'",
                                 test.manifest.name.as_str(),
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
                if (contains_source(test.manifest.declared_sources, source.as_path())) {
                    return adapter_failure<PackageMetadata>(
                        rstd::format("test source '{}' cannot also attach to package '{}'",
                                     source.as_path(),
                                     declaration.package.as_str()));
                }
                sources.push(source.clone());
            }
            if (sources.is_empty()) continue;
            auto synthetic_name = rstd::format(
                "{}@test-attach@{}", test.manifest.name.as_str(), library.manifest.name.as_str());
            attachments.push(TargetMetadata {
                .manifest =
                    PackageManifest {
                        .name             = rstd::move(synthetic_name),
                        .version          = PackageVersion {},
                        .root_module      = library.manifest.root_module.clone(),
                        .root             = test.manifest.root.clone(),
                        .manifest_path    = test.manifest.manifest_path.clone(),
                        .artifact_kind    = ArtifactKind::TestAttachmentArchive,
                        .artifact_name    = library.manifest.artifact_name.clone(),
                        .discovery        = SourceDiscoveryMode::Explicit,
                        .declared_sources = rstd::move(sources),
                    },
                .test_attachment = Some(TestAttachmentTarget {
                    .test_target    = test.manifest.name.clone(),
                    .library_target = library.manifest.name.clone(),
                }),
            });
        }
    }
    for (auto& attachment : attachments) targets.push(rstd::move(attachment));

    auto default_targets = Vec<String>::make();
    for (const auto& name : selected_root_names) {
        if (! selected.contains_key(name.as_str())) {
            return adapter_failure<PackageMetadata>(
                rstd::format("selected root package '{}' is missing", name.as_str()));
        }
        default_targets.push(name.clone());
    }
    auto package_name = String::make("workspace"_str);
    if (graph.root_names.len() == usize(1)) {
        package_name = graph.root_names[usize {}].clone();
    }
    auto profiles        = Vec<ProfileSpec>::make();
    auto default_profile = profile.name.clone();
    profiles.push(rstd::move(profile));
    return Ok(PackageMetadata {
        .name            = rstd::move(package_name),
        .root            = rstd::move(graph.root_directory),
        .manifest_path   = rstd::move(graph.manifest_path),
        .default_profile = rstd::move(default_profile),
        .default_targets = rstd::move(default_targets),
        .toolchain =
            ToolchainSpec {
                .compiler  = configuration.toolchain.compiler.clone(),
                .archiver  = configuration.toolchain.archiver.clone(),
                .formatter = configuration.toolchain.formatter.clone(),
            },
        .profiles = rstd::move(profiles),
        .targets  = rstd::move(targets),
    });
}

auto finalize_package(PackageMetadata metadata, Vec<ResolvedPackageSources> source_sets)
    -> Result<PackageSpec> {
    if (metadata.targets.len() != source_sets.len()) {
        return adapter_failure<PackageSpec>(
            String::make("selected package graph and source sets have different lengths"_str));
    }

    auto source_indices = rstd::collections::BTreeMap<String, usize>::make();
    for (usize index {}; index < source_sets.len(); ++index) {
        if (source_indices.contains_key(source_sets[index].package_name.as_str())) {
            return adapter_failure<PackageSpec>(
                rstd::format("source discovery repeated package '{}'",
                             source_sets[index].package_name.as_str()));
        }
        source_indices.insert(source_sets[index].package_name.clone(), index);
    }

    auto targets = Vec<TargetSpec>::with_capacity(metadata.targets.len());
    for (auto& target : metadata.targets) {
        auto source_position = source_indices.get(target.manifest.name.as_str());
        if (source_position.is_none()) {
            return adapter_failure<PackageSpec>(rstd::format(
                "source discovery is missing package '{}'", target.manifest.name.as_str()));
        }
        auto source_set = rstd::move(source_sets[**source_position].sources);
        auto sources    = Vec<TargetSource>::with_capacity(source_set.sources.len());
        for (auto& source : source_set.sources) {
            sources.push(TargetSource {
                .relative_path     = rstd::move(source.relative_path),
                .path              = rstd::move(source.canonical_path),
                .expected_module   = rstd::move(source.expected_module),
                .frontend_analysis = rstd::move(source.frontend_analysis),
                .documentation     = rstd::move(source.documentation),
            });
        }
        auto artifact_name =
            output_name(target.manifest.artifact_kind, target.manifest.artifact_name.as_str());
        auto archive_stem  = target.manifest.artifact_name.clone();
        auto compile_tests = rstd::move(target.manifest.compile_tests);
        targets.push(TargetSpec {
            .name               = rstd::move(target.manifest.name),
            .artifact_kind      = target.manifest.artifact_kind,
            .artifact_name      = rstd::move(artifact_name),
            .archive_stem       = rstd::move(archive_stem),
            .module_affiliation = rstd::move(target.manifest.root_module),
            .root               = rstd::move(target.manifest.root),
            .sources            = rstd::move(sources),
            .dependencies       = rstd::move(target.dependencies),
            .usage              = rstd::move(target.manifest.usage),
            .compile_tests      = rstd::move(compile_tests),
            .test_attachment    = rstd::move(target.test_attachment),
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

} // namespace tenon
