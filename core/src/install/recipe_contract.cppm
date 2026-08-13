export module lito.install.recipe_contract;

import rstd;
import lito.error;
import lito.manifest.contract;
import lito.package.identity;
import lito.configure_template;

using namespace rstd::prelude;

export namespace lito
{

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
    PathBuf          input;
    PathBuf          destination;
    ConfigureValues  values;
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
};

struct InstallBuildRequirements {
    Vec<String>          packages;
    Vec<PackageTargetId> targets;
};

struct InstallScriptContext {
    String profile;
    String target;
    String target_arch;
};

} // namespace lito
