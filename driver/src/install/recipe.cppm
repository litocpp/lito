export module lito.driver:install.recipe;

import rstd;
import lito.core;
import :install.package;
import :build.request;

using namespace rstd::prelude;

export namespace lito
{

struct PackageInstallTarget {
    lito::package::PackageTargetId target;
    String                         artifact_name;
};

struct PackageInstallInput {
    String                              name;
    String                              version;
    PathBuf                             root;
    PathBuf                             manifest_path;
    Option<PathBuf>                     script;
    Vec<PackageInstallTarget>           binaries;
    lito::source::ResolvedPackageSource source;
    Vec<InstallRuntimeDependency>       runtime_dependencies;
    bool                                direct { false };
};

struct InstallArtifactRecipe {
    lito::package::PackageTargetId target;
    PathBuf                        destination;
    struct RuntimeSearchReference {
        String dependency;
        String set;

        auto clone() const -> RuntimeSearchReference {
            return RuntimeSearchReference {
                .dependency = dependency.clone(),
                .set        = set.clone(),
            };
        }
    };
    Vec<RuntimeSearchReference> runtime_search;
};

struct InstallStripRecipe {
    lito::artifact::StripMode mode { lito::artifact::StripMode::None };
    Vec<PathBuf>              files;
};

struct InstallExternalAssetRecipe {
    String                     dependency;
    String                     set;
    PathBuf                    destination;
    Option<InstallStripRecipe> strip;
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
    String                              owner;
    String                              version;
    PathBuf                             root;
    Vec<InstallArtifactRecipe>          artifacts;
    Vec<InstallExternalAssetRecipe>     external_assets;
    Vec<InstallFileRecipe>              files;
    Vec<InstallTemplateRecipe>          templates;
    Vec<InstallInventoryRecipe>         inventories;
    lito::source::ResolvedPackageSource source;
    Vec<InstallRuntimeDependency>       runtime_dependencies;
};

struct InstallRuntimeSearchAsset {
    String  dependency;
    String  set;
    PathBuf destination;
};

struct InstallArtifactRuntimeSearchRequirement {
    lito::package::PackageTargetId target;
    PathBuf                        destination;
    Vec<InstallRuntimeSearchAsset> assets;
};

struct InstallBuildRequirements {
    Vec<lito::package::PackageTargetId>          targets;
    Vec<InstallArtifactRuntimeSearchRequirement> runtime_search;
    Vec<RequestedArtifactLinkVariant>            artifact_link_variants;
};

struct InstallScriptContext {
    String profile;
    String target;
    String target_arch;
};

} // namespace lito
