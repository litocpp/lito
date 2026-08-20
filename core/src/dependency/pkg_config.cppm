export module lito.core:dependency.pkg_config;

import rstd;
import :dependency.condition;
import :dependency.visibility;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;

export namespace lito::dependency
{

enum class PkgConfigVersionOperator
{
    Equal,
    Less,
    Greater,
    LessEqual,
    GreaterEqual,
};

enum class PkgConfigQueryMode
{
    Shared,
    Static,
};

struct PkgConfigVersionRequirement {
    PkgConfigVersionOperator comparison { PkgConfigVersionOperator::Equal };
    String                   value;
};

struct PkgConfigDependencyRequirement {
    String                              module;
    Option<PkgConfigVersionRequirement> version;
    PkgConfigQueryMode                  mode { PkgConfigQueryMode::Shared };
};

struct PkgConfigExternalDependency {
    String                              alias;
    PkgConfigDependencyRequirement      requirement;
    DependencyVisibility                visibility { DependencyVisibility::Private };
    Option<ExternalDependencyCondition> condition;
};

struct PkgConfigProviderConfig {
    PathBuf         executable;
    Vec<PathBuf>    search_paths;
    Vec<PathBuf>    library_paths;
    Option<PathBuf> sysroot;
    bool            target_configured { false };

    auto clone() const -> PkgConfigProviderConfig {
        auto result = PkgConfigProviderConfig {
            .executable        = executable.clone(),
            .search_paths      = as<Clone>(search_paths).clone(),
            .library_paths     = as<Clone>(library_paths).clone(),
            .target_configured = target_configured,
        };
        if (sysroot.is_some()) result.sysroot = Some(sysroot->clone());
        return result;
    }
};

} // namespace lito::dependency
