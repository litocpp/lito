export module lito.package.graph_contract;

import rstd;
import lito.error;
import lito.build.profile_contract;
import lito.source.contract;
import lito.dependency.contract;
import lito.manifest.contract;
import lito.package.identity;
import lito.workspace.contract;

export namespace lito
{

struct ResolvedDependency {
    String               name;
    DependencyVisibility visibility { DependencyVisibility::Private };
};

struct ResolvedPackage {
    String                                  source_identity;
    PathBuf                                 source_manifest;
    PackageManifest                         manifest;
    Vec<ResolvedDependency>                 dependencies;
    Vec<ResolvedDependency>                 dev_dependencies;
    Vec<PreparedCMakeDependencyRequirement> cmake_external_dependencies;
};

struct ResolvedProjectRoot {
    String          name;
    String          source_identity;
    ProjectRootRole role { ProjectRootRole::PrimaryPackage };
};

struct ResolvedPackageGraph {
    String                     name;
    Vec<ResolvedProjectRoot>   roots;
    PathBuf                    root_directory;
    PathBuf                    manifest_path;
    bool                       root_is_workspace { false };
    ProjectProfile             profile;
    Vec<ResolvedPackageSource> sources;
    Vec<ResolvedPackage>       packages;
};

struct ResolvedPackageSelection {
    ResolvedPackageGraph graph;
    Vec<String>          selected_root_names;
    Vec<String>          selected_package_names;
    Vec<PackageTargetId> selected_targets;
};

} // namespace lito
