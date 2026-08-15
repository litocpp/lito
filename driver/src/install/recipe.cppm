export module lito.driver:install.recipe;

import rstd;
import lito.core;
import :install.package;

using namespace rstd::prelude;

export namespace lito
{

struct PackageInstallTarget {
    PackageTargetId target;
    String          artifact_name;
};

struct PackageInstallInput {
    String                        name;
    String                        version;
    PathBuf                       root;
    PathBuf                       manifest_path;
    Option<PathBuf>               script;
    Vec<PackageInstallTarget>     binaries;
    ResolvedPackageSource         source;
    Vec<InstallRuntimeDependency> runtime_dependencies;
    bool                          direct { false };
};

struct InstallArtifactRecipe {
    PackageTargetId target;
    PathBuf         destination;
};

struct InstallExternalAssetRecipe {
    String  dependency;
    String  set;
    PathBuf destination;
};

struct InstallFileRecipe {
    PathBuf source;
    PathBuf destination;
};

struct InstallTemplateRecipe {
    PathBuf         input;
    PathBuf         destination;
    ConfigureValues values;
};

struct InstallInventoryRecipe {
    PathBuf destination;
    PathBuf relative_to;
};

struct InstallRecipe {
    String                          owner;
    String                          version;
    PathBuf                         root;
    Vec<InstallArtifactRecipe>      artifacts;
    Vec<InstallExternalAssetRecipe> external_assets;
    Vec<InstallFileRecipe>          files;
    Vec<InstallTemplateRecipe>      templates;
    Vec<InstallInventoryRecipe>     inventories;
    ResolvedPackageSource           source;
    Vec<InstallRuntimeDependency>   runtime_dependencies;
};

struct InstallBuildRequirements {
    Vec<PackageTargetId> targets;
};

struct InstallScriptContext {
    String profile;
    String target;
    String target_arch;
};

} // namespace lito
