export module lito.cpp:package.metadata;

import rstd;
import lito.core;
import :build.profile;
import :package.target;
import :usage;

using namespace rstd::prelude;

export namespace lito::cpp
{

struct PackageFeatureState {
    String name;
    String macro_name;
    bool   enabled { false };

    auto clone() const -> PackageFeatureState {
        return PackageFeatureState {
            .name       = name.clone(),
            .macro_name = macro_name.clone(),
            .enabled    = enabled,
        };
    }
};

struct PackageCompileMetadata {
    Option<String>           version;
    Vec<PackageFeatureState> features;

    auto clone() const -> PackageCompileMetadata {
        auto copied_features = Vec<PackageFeatureState>::with_capacity(features.len());
        for (const auto& feature : features) copied_features.push(feature.clone());
        return PackageCompileMetadata {
            .version  = as<Clone>(version).clone(),
            .features = rstd::move(copied_features),
        };
    }
};

struct ExternalSourceRoot {
    usize   package {};
    String  package_name;
    String  name;
    PathBuf root;
    String  identity;
};

struct ExternalSourceRootCatalog {
    Vec<ExternalSourceRoot> sources;
};

struct ResolvedSourceGroup {
    String       name;
    PathBuf      root;
    String       identity;
    Vec<PathBuf> sources;
    bool         generated { false };
    bool         external { false };
};

enum class GeneratedArtifactRole
{
    Resource,
    Metadata,
    Auxiliary,
};

struct GeneratedArtifactContribution {
    GeneratedArtifactRole role { GeneratedArtifactRole::Metadata };
    PathBuf               path;
    String                action_identity;
};

struct ResolvedTarget {
    lito::package::PackageTargetId               id;
    String                                       package_source_identity;
    ArtifactKind                                 artifact_kind { ArtifactKind::StaticLibrary };
    lito::manifest::PackageLanguage              language { lito::manifest::PackageLanguage::Cpp };
    String                                       artifact_name;
    bool                                         link_stdlib { true };
    bool                                         host_tool { false };
    lito::manifest::TargetSourceManifest         source;
    Vec<ResolvedSourceGroup>                     source_groups;
    PathBuf                                      root;
    PathBuf                                      source_root;
    UsageRequirements                            usage;
    Vec<ResolvedCompileTestCase>                 compile_tests;
    Vec<lito::manifest::TestAttachmentManifest>  attachments;
    Vec<lito::manifest::RuntimeResourceManifest> runtime_resources;
    Vec<DependencySpec>                          dependencies;
    Vec<CompilerPluginDependencySpec>            plugin_dependencies;
    Vec<ProcMacroDependencySpec>                 proc_macro_dependencies;
    Vec<ResolvedExternalDependency>              external_dependencies;
    Vec<GeneratedArtifactContribution>           generated_artifacts;
    Option<TestAttachmentTarget>                 test_attachment;
    PackageCompileMetadata                       compile_metadata;
};

auto add_generated_source(ResolvedTarget& target, PathBuf source) -> bool {
    for (const auto& group : target.source_groups) {
        for (const auto& existing : group.sources) {
            if (existing.as_path() == source.as_path()) return false;
        }
    }
    for (auto& group : target.source_groups) {
        if (! group.generated || group.name != "build-script"_str) continue;
        group.sources.push(rstd::move(source));
        return true;
    }
    auto sources = Vec<PathBuf>::make();
    sources.push(rstd::move(source));
    target.source_groups.push(ResolvedSourceGroup {
        .name      = String::make("build-script"_str),
        .root      = target.root.clone(),
        .identity  = String::make("build-script-generated"_str),
        .sources   = rstd::move(sources),
        .generated = true,
    });
    return true;
}

auto add_generated_include_directory(ResolvedTarget& target, PathBuf path) -> bool {
    for (const auto& requirement : target.usage.private_include_directory_requirements) {
        if (requirement.root == lito::dependency::IncludeDirectoryRoot::Generated &&
            requirement.path.as_path() == path.as_path()) {
            return false;
        }
    }
    target.usage.private_include_directory_requirements.push(
        lito::dependency::IncludeDirectoryRequirement {
            .root = lito::dependency::IncludeDirectoryRoot::Generated,
            .path = rstd::move(path),
        });
    return true;
}

auto add_generated_artifact(ResolvedTarget&       target,
                            GeneratedArtifactRole role,
                            PathBuf               path,
                            String                action_identity) -> bool {
    for (const auto& artifact : target.generated_artifacts) {
        if (artifact.role == role && artifact.path.as_path() == path.as_path()) return false;
    }
    target.generated_artifacts.push(GeneratedArtifactContribution {
        .role            = role,
        .path            = rstd::move(path),
        .action_identity = rstd::move(action_identity),
    });
    return true;
}

struct PackageBuildToolRequirement {
    String                               package;
    PathBuf                              root;
    lito::manifest::BuildToolRequirement requirement;
};

enum class BuildScriptOwnerKind
{
    Workspace,
    Package,
};

struct BuildScriptOwner {
    BuildScriptOwnerKind                          kind { BuildScriptOwnerKind::Package };
    Option<String>                                package;
    String                                        source_identity;
    PathBuf                                       root;
    PathBuf                                       script;
    Vec<String>                                   script_dependencies;
    Vec<lito::package::ResolvedScriptPackageView> script_packages;
};

struct SelectedPackageMetadata {
    String         name;
    Option<String> version;
    String         source_identity;
    PathBuf        root;
};

struct PackageMetadata {
    String                              name;
    PathBuf                             root;
    PathBuf                             manifest_path;
    Vec<BuildScriptOwner>               build_scripts;
    String                              default_profile;
    Vec<lito::package::PackageTargetId> default_targets;
    Vec<lito::package::PackageTargetId> available_targets;
    Vec<SelectedPackageMetadata>        selected_packages;
    Vec<ExternalSourceRoot>             external_sources;
    Vec<PackageBuildToolRequirement>    build_tools;
    lito::config::ToolchainSpec         toolchain;
    Vec<ProfileSpec>                    profiles;
    Vec<ResolvedTarget>                 targets;
};

} // namespace lito::cpp
