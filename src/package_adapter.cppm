export module tenon.package_adapter;

import rstd;
import tenon.model;

using namespace rstd::literals;

namespace tenon::package_adapter_detail
{

template<typename T>
auto failure(String message) -> Result<T> {
    return rstd::Err(Error::make(ErrorKind::Manifest, rstd::move(message)));
}

auto validate_options(const Vec<String>& options) -> Result<rstd::empty> {
    for (const auto& option : options) {
        auto value = option.as_str();
        if (value == "-frtti"_str || value == "-fexceptions"_str ||
            value.starts_with("-stdlib="_str) || value.starts_with("-std="_str) ||
            value == "-fmodules-reduced-bmi"_str ||
            value == "-fno-modules-reduced-bmi"_str) {
            return failure<rstd::empty>(rstd::format(
                "build option '{}' overrides a Tenon-owned setting", value));
        }
    }
    return rstd::Ok(rstd::empty {});
}

auto output_name(ArtifactKind kind, rstd::ref<rstd::str> declared_name) -> String {
    if (kind == ArtifactKind::Executable) return String::make(declared_name);
    auto result = String::make("lib"_str);
    result.push_str(declared_name);
    result.push_str(".a"_str);
    return result;
}

} // namespace tenon::package_adapter_detail

export namespace tenon
{

auto adapt_single_artifact(PackageManifest manifest,
                           ResolvedSourceSet source_set,
                           const BuildConfiguration& configuration) -> Result<PackageSpec> {
    using namespace package_adapter_detail;

    if (! manifest.dependencies.is_empty()) {
        return failure<PackageSpec>(
            String::make(
                "path dependencies require the package resolver and are not yet supported"_str));
    }
    if (configuration.profile_name.is_empty()) {
        return failure<PackageSpec>(
            String::make("build configuration profile name is required"_str));
    }
    if (configuration.language_standard.as_str() != "c++20"_str) {
        return failure<PackageSpec>(
            String::make("Tenon currently requires build configuration standard c++20"_str));
    }
    if (configuration.toolchain.compiler.is_empty() || configuration.toolchain.scanner.is_empty() ||
        configuration.toolchain.archiver.is_empty()) {
        return failure<PackageSpec>(
            String::make("build configuration requires Clang tool paths"_str));
    }
    auto options_valid = validate_options(configuration.options);
    if (options_valid.is_err()) return rstd::Err(rstd::move(options_valid).unwrap_err());

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
    profiles.push(ProfileSpec {
        .name = configuration.profile_name.clone(),
        .standard_library = configuration.standard_library,
        .bmi_mode = configuration.bmi_mode,
        .language_standard = configuration.language_standard.clone(),
        .options = configuration.options.clone(),
    });
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

    return rstd::Ok(PackageSpec {
        .name = rstd::move(manifest.name),
        .root = rstd::move(manifest.root),
        .manifest_path = rstd::move(manifest.manifest_path),
        .default_profile = configuration.profile_name.clone(),
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
                         const BuildConfiguration& configuration) -> Result<PackageSpec> {
    using namespace package_adapter_detail;

    if (graph.packages.len() != source_sets.len()) {
        return failure<PackageSpec>(String::make(
            "resolved package graph and source sets have different lengths"_str));
    }
    if (configuration.profile_name.is_empty() ||
        configuration.language_standard.as_str() != "c++20"_str ||
        configuration.toolchain.compiler.is_empty() || configuration.toolchain.scanner.is_empty() ||
        configuration.toolchain.archiver.is_empty()) {
        return failure<PackageSpec>(
            String::make("invalid build configuration for package graph"_str));
    }
    auto options_valid = validate_options(configuration.options);
    if (options_valid.is_err()) return rstd::Err(rstd::move(options_valid).unwrap_err());

    auto target_names = rstd::collections::BTreeMap<String, String>::make();
    const ResolvedPackage* root_package = nullptr;
    for (const auto& package : graph.packages) {
        target_names.insert(package.id.clone(), package.manifest.name.clone());
        if (package.id == graph.root_id.as_str()) root_package = rstd::addressof(package);
    }
    if (root_package == nullptr) {
        return failure<PackageSpec>(String::make("resolved graph root package is missing"_str));
    }
    auto package_name = root_package->manifest.name.clone();
    auto package_root = root_package->manifest.root.clone();
    auto manifest_path = root_package->manifest.manifest_path.clone();
    auto root_target = root_package->manifest.name.clone();

    for (const auto& package : graph.packages) {
        if (package.manifest.artifact_kind == ArtifactKind::Executable &&
            package.id != graph.root_id.as_str()) {
            return failure<PackageSpec>(rstd::format(
                "dependency package '{}' cannot produce an executable",
                package.manifest.name.as_str()));
        }
    }

    auto targets = Vec<TargetSpec>::with_capacity(graph.packages.len());
    for (rstd::usize index {}; index < graph.packages.len(); ++index) {
        auto& package = graph.packages[index];
        auto& source_set = source_sets[index];
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
                return failure<PackageSpec>(rstd::format(
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
    default_targets.push(rstd::move(root_target));
    auto profiles = Vec<ProfileSpec>::make();
    profiles.push(ProfileSpec {
        .name = configuration.profile_name.clone(),
        .standard_library = configuration.standard_library,
        .bmi_mode = configuration.bmi_mode,
        .language_standard = configuration.language_standard.clone(),
        .options = configuration.options.clone(),
    });
    return rstd::Ok(PackageSpec {
        .name = rstd::move(package_name),
        .root = rstd::move(package_root),
        .manifest_path = rstd::move(manifest_path),
        .default_profile = configuration.profile_name.clone(),
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
