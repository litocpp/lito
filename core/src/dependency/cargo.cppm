export module lito.core:dependency.cargo;

import rstd;
import :dependency.condition;
import :dependency.visibility;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;

export namespace lito::dependency
{

enum class CargoDependencyUsage
{
    Link,
    Runtime,
};

struct CargoProfileName {
    String value;

    auto as_str() const noexcept -> ref<str> { return value.as_str(); }

    auto clone() const -> CargoProfileName { return CargoProfileName { .value = value.clone() }; }
};

struct CargoDependencyRecipe {
    String  package;
    String  source;
    PathBuf manifest_path;

    auto clone() const -> CargoDependencyRecipe {
        return CargoDependencyRecipe {
            .package       = package.clone(),
            .source        = source.clone(),
            .manifest_path = manifest_path.clone(),
        };
    }
};

struct CargoDependencyConsumption {
    Vec<String>                         features;
    bool                                default_features { true };
    Option<CargoProfileName>            profile;
    CargoDependencyUsage                usage { CargoDependencyUsage::Link };
    Option<DependencyVisibility>        visibility;
    Option<ExternalDependencyCondition> condition;

    auto clone() const -> CargoDependencyConsumption {
        auto result = CargoDependencyConsumption {
            .features         = as<Clone>(features).clone(),
            .default_features = default_features,
            .usage            = usage,
        };
        if (profile.is_some()) result.profile = Some(profile->clone());
        result.visibility = visibility;
        if (condition.is_some()) result.condition = Some(condition->clone());
        return result;
    }
};

struct CargoDependencyRequirement {
    String                     alias;
    CargoDependencyRecipe      recipe;
    CargoDependencyConsumption consumption;
    Option<PathBuf>            declaration_root;

    auto clone() const -> CargoDependencyRequirement {
        auto result = CargoDependencyRequirement {
            .alias       = alias.clone(),
            .recipe      = recipe.clone(),
            .consumption = consumption.clone(),
        };
        if (declaration_root.is_some()) {
            result.declaration_root = Some(declaration_root->clone());
        }
        return result;
    }
};

} // namespace lito::dependency
