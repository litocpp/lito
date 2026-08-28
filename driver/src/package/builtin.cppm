export module lito.driver:package.builtin;

import rstd;
import lito.core;
import :config.registry;
import :registry.graph;
import :registry.index;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;

export namespace lito::package
{

class EmbeddedRegistryPackages {
    PathBuf                                   cache_root_;
    const lito::config::LitoBootstrapConfig*  config_ {};
    lito::registry::EmbeddedPackageProvider   provider_;
    Vec<lito::registry::RegistryPackageIndex> indices_;

public:
    EmbeddedRegistryPackages(PathBuf                                  cache_root,
                             const lito::config::LitoBootstrapConfig& config,
                             lito::registry::EmbeddedPackageProvider  provider)
        : cache_root_(rstd::move(cache_root)),
          config_(rstd::addressof(config)),
          provider_(provider) {}

    auto resolve(ref<str> id)
        -> lito::registry::RegistryGraphResult<lito::registry::BuiltinRegistryPackage>;
    auto add_indices(lito::registry::RegistryGraphClient& client) const -> void;
};

} // namespace lito::package
