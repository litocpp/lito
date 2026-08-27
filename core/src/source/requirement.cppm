module;
#include <rstd/enum.hpp>

export module lito.core:source.requirement;

import rstd;
import :source.git;
import :registry.archive;
import :registry.digest;
import :registry.identity;
import :registry.version;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;

export namespace lito::source
{

enum class PackageSourceKind
{
    Path,
    Git,
    Builtin,
    Registry,
};

class PackageSourceRequirement {
    RSTD_ENUM(PackageSourceRequirement,
              (Path, (PathBuf path;)),
              (Git, (String url; GitReference reference;)),
              (Builtin, (String id;)),
              (Registry,
               (Option<String> registry; lito::registry::RegistryPackageName package;
                lito::registry::VersionRequirement                           requirement;)))
};

struct ResolvedPackageSource {
    String                                    identity;
    PackageSourceKind                         kind { PackageSourceKind::Path };
    PathBuf                                   root_directory;
    PathBuf                                   path;
    String                                    git;
    GitReference                              reference;
    String                                    commit;
    String                                    builtin;
    String                                    digest;
    Option<lito::registry::RegistryPackageId> registry_package;
    Option<lito::registry::SemanticVersion>   registry_version;
    Option<lito::registry::PackageChecksum>   package_checksum;

    auto clone() const -> ResolvedPackageSource {
        return ResolvedPackageSource {
            .identity         = identity.clone(),
            .kind             = kind,
            .root_directory   = root_directory.clone(),
            .path             = path.clone(),
            .git              = git.clone(),
            .reference        = reference.clone(),
            .commit           = commit.clone(),
            .builtin          = builtin.clone(),
            .digest           = digest.clone(),
            .registry_package = registry_package.is_some()
                                    ? Some(registry_package->clone())
                                    : None<lito::registry::RegistryPackageId>(),
            .registry_version = registry_version.is_some()
                                    ? Some(registry_version->clone())
                                    : None<lito::registry::SemanticVersion>(),
            .package_checksum = package_checksum.is_some()
                                    ? Some(package_checksum->clone())
                                    : None<lito::registry::PackageChecksum>(),
        };
    }
};

auto registry_source_identity(const lito::registry::RegistryPackageId& package,
                              const lito::registry::SemanticVersion&   version) -> String {
    return rstd::format("registry+{}{}@{}",
                        package.registry.as_str(),
                        package.name.as_str(),
                        version.text().as_str());
}

} // namespace lito::source
