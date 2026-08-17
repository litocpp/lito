export module lito.cpp:package.metadata;

import rstd;
import lito.core;
import :build.profile;
import :package.target;
import :usage;

using namespace rstd::prelude;

export namespace lito::cpp
{

struct ResolvedTarget {
    lito::package::PackageTargetId               id;
    ArtifactKind                                 artifact_kind { ArtifactKind::StaticLibrary };
    String                                       artifact_name;
    bool                                         link_stdlib { true };
    lito::manifest::TargetSourceManifest         source;
    PathBuf                                      root;
    PathBuf                                      source_root;
    UsageRequirements                            usage;
    Vec<ResolvedCompileTestCase>                 compile_tests;
    Vec<lito::manifest::TestAttachmentManifest>  attachments;
    Vec<lito::manifest::RuntimeResourceManifest> runtime_resources;
    Vec<DependencySpec>                          dependencies;
    Vec<ResolvedExternalDependency>              external_dependencies;
    Option<TestAttachmentTarget>                 test_attachment;
};

struct PackageBuildToolRequirement {
    String                               package;
    PathBuf                              root;
    lito::manifest::BuildToolRequirement requirement;
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
    Vec<String>                         build_script_packages;
    String                              default_profile;
    Vec<lito::package::PackageTargetId> default_targets;
    Vec<SelectedPackageMetadata>        selected_packages;
    Vec<PackageBuildToolRequirement>    build_tools;
    lito::config::ToolchainSpec         toolchain;
    Vec<ProfileSpec>                    profiles;
    Vec<ResolvedTarget>                 targets;
};

} // namespace lito::cpp
