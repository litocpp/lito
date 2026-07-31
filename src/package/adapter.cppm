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

auto validate_options(const Vec<String>& options) -> Result<empty> {
    for (const auto& option : options) {
        auto value = option.as_str();
        if (value == "-frtti"_str || value == "-fexceptions"_str ||
            value.starts_with("-stdlib="_str) || value.starts_with("-std="_str) ||
            value == "-fmodules-reduced-bmi"_str ||
            value == "-fno-modules-reduced-bmi"_str) {
            return adapter_failure<empty>(rstd::format(
                "build option '{}' overrides a Tenon-owned setting", value));
        }
    }
    return Ok(empty {});
}

auto output_name(ArtifactKind kind, ref<str> declared_name) -> String {
    if (kind == ArtifactKind::Executable) return String::make(declared_name);
    auto result = String::make("lib"_str);
    result.push_str(declared_name);
    result.push_str(".a"_str);
    return result;
}

} // namespace tenon

export namespace tenon
{

auto adapt_single_artifact(PackageManifest manifest,
                           ResolvedSourceSet source_set,
                           const BuildConfiguration& configuration) -> Result<PackageSpec> {
    if (! manifest.dependencies.is_empty()) {
        return adapter_failure<PackageSpec>(
            String::make(
                "path dependencies require the package resolver and are not yet supported"_str));
    }
    if (configuration.language_standard.as_str() != "c++20"_str) {
        return adapter_failure<PackageSpec>(
            String::make("Tenon currently requires build configuration standard c++20"_str));
    }
    if (configuration.toolchain.compiler.is_empty() || configuration.toolchain.scanner.is_empty() ||
        configuration.toolchain.archiver.is_empty()) {
        return adapter_failure<PackageSpec>(
            String::make("build configuration requires Clang tool paths"_str));
    }
    auto options_valid = validate_options(configuration.options);
    if (options_valid.is_err()) return Err(rstd::move(options_valid).unwrap_err());
    auto profile = make_profile_spec(configuration);
    if (profile.is_err()) return Err(rstd::move(profile).unwrap_err());

    auto sources = Vec<PathBuf>::with_capacity(source_set.sources.len());
    auto expectations = Vec<ModuleExpectation>::make();
    for (auto& source : source_set.sources) {
        if (source.expected_module.is_some()) {
            expectations.push(ModuleExpectation {
                .source = source.canonical_path.clone(),
                .logical_name = rstd::move(source.expected_module).unwrap(),
            });
        }
        sources.push(rstd::move(source.canonical_path));
    }

    auto target_name = manifest.name.clone();
    auto artifact_name = output_name(manifest.artifact_kind, manifest.artifact_name.as_str());
    auto default_targets = Vec<String>::make();
    default_targets.push(target_name.clone());
    auto profiles = Vec<ProfileSpec>::make();
    auto default_profile = profile->name.clone();
    profiles.push(rstd::move(profile).unwrap());
    auto targets = Vec<TargetSpec>::make();
    targets.push(TargetSpec {
        .name = rstd::move(target_name),
        .artifact_kind = manifest.artifact_kind,
        .artifact_name = rstd::move(artifact_name),
        .module_affiliation = rstd::move(manifest.root_module),
        .root = manifest.root.clone(),
        .sources = rstd::move(sources),
        .module_expectations = rstd::move(expectations),
        .dependencies = Vec<DependencySpec>::make(),
        .usage = rstd::move(manifest.usage),
    });

    return Ok(PackageSpec {
        .name = rstd::move(manifest.name),
        .root = rstd::move(manifest.root),
        .manifest_path = rstd::move(manifest.manifest_path),
        .default_profile = rstd::move(default_profile),
        .default_targets = rstd::move(default_targets),
        .toolchain = ToolchainSpec {
            .compiler = configuration.toolchain.compiler.clone(),
            .scanner = configuration.toolchain.scanner.clone(),
            .archiver = configuration.toolchain.archiver.clone(),
        },
        .profiles = rstd::move(profiles),
        .targets = rstd::move(targets),
    });
}

auto adapt_package_graph(ResolvedPackageGraph graph,
                         Vec<ResolvedSourceSet> source_sets,
                         const Vec<String>& selected_package_ids,
                         const Vec<String>& selected_root_ids,
                         const BuildConfiguration& configuration) -> Result<PackageSpec> {
    if (selected_package_ids.len() != source_sets.len()) {
        return adapter_failure<PackageSpec>(String::make(
            "selected package graph and source sets have different lengths"_str));
    }
    if (configuration.language_standard.as_str() != "c++20"_str ||
        configuration.toolchain.compiler.is_empty() || configuration.toolchain.scanner.is_empty() ||
        configuration.toolchain.archiver.is_empty()) {
        return adapter_failure<PackageSpec>(
            String::make("invalid build configuration for package graph"_str));
    }
    auto options_valid = validate_options(configuration.options);
    if (options_valid.is_err()) return Err(rstd::move(options_valid).unwrap_err());
    auto profile = make_profile_spec(configuration);
    if (profile.is_err()) return Err(rstd::move(profile).unwrap_err());

    auto selected = rstd::collections::BTreeMap<String, empty>::make();
    auto roots = rstd::collections::BTreeMap<String, empty>::make();
    for (const auto& id : selected_package_ids) selected.insert(id.clone(), empty {});
    for (const auto& id : graph.root_ids) roots.insert(id.clone(), empty {});

    auto target_names = rstd::collections::BTreeMap<String, String>::make();
    auto artifact_kinds = rstd::collections::BTreeMap<String, ArtifactKind>::make();
    for (const auto& package : graph.packages) {
        artifact_kinds.insert(package.id.clone(), package.manifest.artifact_kind);
        if (selected.contains_key(package.id.as_str())) {
            target_names.insert(package.id.clone(), package.manifest.name.clone());
        }
    }

    for (const auto& package : graph.packages) {
        if (package.manifest.artifact_kind == ArtifactKind::Executable &&
            ! roots.contains_key(package.id.as_str())) {
            return adapter_failure<PackageSpec>(rstd::format(
                "dependency package '{}' cannot produce an executable",
                package.manifest.name.as_str()));
        }
        for (const auto& dependency : package.dependencies) {
            auto kind = artifact_kinds.get(dependency.package_id.as_str());
            if (kind.is_some() && **kind == ArtifactKind::Executable) {
                return adapter_failure<PackageSpec>(rstd::format(
                    "package '{}' cannot depend on executable package '{}'",
                    package.manifest.name.as_str(),
                    dependency.package_id.as_str()));
            }
        }
    }

    auto targets = Vec<TargetSpec>::with_capacity(selected_package_ids.len());
    auto source_index = usize {};
    for (usize index {}; index < graph.packages.len(); ++index) {
        auto& package = graph.packages[index];
        if (! selected.contains_key(package.id.as_str())) continue;
        auto& source_set = source_sets[source_index];
        ++source_index;
        auto sources = Vec<PathBuf>::with_capacity(source_set.sources.len());
        auto expectations = Vec<ModuleExpectation>::make();
        for (auto& source : source_set.sources) {
            if (source.expected_module.is_some()) {
                expectations.push(ModuleExpectation {
                    .source = source.canonical_path.clone(),
                    .logical_name = rstd::move(source.expected_module).unwrap(),
                });
            }
            sources.push(rstd::move(source.canonical_path));
        }
        auto dependencies = Vec<DependencySpec>::with_capacity(package.dependencies.len());
        for (const auto& dependency : package.dependencies) {
            auto target_name = target_names.get(dependency.package_id.as_str());
            if (target_name.is_none()) {
                return adapter_failure<PackageSpec>(rstd::format(
                    "resolved dependency '{}' is missing", dependency.package_id.as_str()));
            }
            dependencies.push(DependencySpec {
                .target = (**target_name).clone(),
                .visibility = dependency.visibility,
            });
        }
        auto artifact_name = output_name(
            package.manifest.artifact_kind, package.manifest.artifact_name.as_str());
        targets.push(TargetSpec {
            .name = rstd::move(package.manifest.name),
            .artifact_kind = package.manifest.artifact_kind,
            .artifact_name = rstd::move(artifact_name),
            .module_affiliation = rstd::move(package.manifest.root_module),
            .root = rstd::move(package.manifest.root),
            .sources = rstd::move(sources),
            .module_expectations = rstd::move(expectations),
            .dependencies = rstd::move(dependencies),
            .usage = rstd::move(package.manifest.usage),
        });
    }

    auto default_targets = Vec<String>::make();
    for (const auto& id : selected_root_ids) {
        auto target_name = target_names.get(id.as_str());
        if (target_name.is_none()) {
            return adapter_failure<PackageSpec>(rstd::format(
                "selected root package '{}' is missing", id.as_str()));
        }
        default_targets.push((**target_name).clone());
    }
    auto package_name = String::make("workspace"_str);
    if (graph.root_ids.len() == usize(1)) {
        auto name = target_names.get(graph.root_ids[usize {}].as_str());
        if (name.is_some()) package_name = (**name).clone();
    }
    auto profiles = Vec<ProfileSpec>::make();
    auto default_profile = profile->name.clone();
    profiles.push(rstd::move(profile).unwrap());
    return Ok(PackageSpec {
        .name = rstd::move(package_name),
        .root = rstd::move(graph.root_directory),
        .manifest_path = rstd::move(graph.manifest_path),
        .default_profile = rstd::move(default_profile),
        .default_targets = rstd::move(default_targets),
        .toolchain = ToolchainSpec {
            .compiler = configuration.toolchain.compiler.clone(),
            .scanner = configuration.toolchain.scanner.clone(),
            .archiver = configuration.toolchain.archiver.clone(),
        },
        .profiles = rstd::move(profiles),
        .targets = rstd::move(targets),
    });
}

} // namespace tenon
