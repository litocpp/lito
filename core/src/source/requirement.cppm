module;
#include <rstd/enum.hpp>

export module lito.core:source.requirement;

import rstd;
import :source.git;
import :registry.archive;
import :registry.identity;
import :registry.release;

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

public:
    auto clone() const -> PackageSourceRequirement {
        if (is_Path()) {
            return PackageSourceRequirement::Path(as_Path().path.clone());
        }
        if (is_Builtin()) {
            return PackageSourceRequirement::Builtin(as_Builtin().id.clone());
        }
        if (is_Registry()) {
            return PackageSourceRequirement::Registry(as_Registry().registry.clone(),
                                                      as_Registry().package.clone(),
                                                      as_Registry().requirement.clone());
        }
        return PackageSourceRequirement::Git(as_Git().url.clone(), as_Git().reference.clone());
    }
};

struct PackageRegistryRequirement {
    Option<String>                      registry;
    lito::registry::RegistryPackageName package;
    lito::registry::VersionRequirement  requirement;

    auto clone() const -> PackageRegistryRequirement {
        return PackageRegistryRequirement {
            .registry    = registry.clone(),
            .package     = package.clone(),
            .requirement = requirement.clone(),
        };
    }
};

struct ResolvedPackageSource {
    String                                     identity;
    PackageSourceKind                          kind { PackageSourceKind::Path };
    PathBuf                                    root_directory;
    PathBuf                                    path;
    String                                     git;
    GitReference                               reference;
    String                                     commit;
    String                                     builtin;
    String                                     digest;
    Option<lito::registry::RegistryReleasePin> registry;

    auto clone() const -> ResolvedPackageSource {
        return ResolvedPackageSource {
            .identity       = identity.clone(),
            .kind           = kind,
            .root_directory = root_directory.clone(),
            .path           = path.clone(),
            .git            = git.clone(),
            .reference      = reference.clone(),
            .commit         = commit.clone(),
            .builtin        = builtin.clone(),
            .digest         = digest.clone(),
            .registry       = registry.is_some() ? Some(registry->clone())
                                                 : None<lito::registry::RegistryReleasePin>(),
        };
    }
};

auto registry_source_identity(const lito::registry::RegistryReleasePin& pin) -> String {
    return rstd::format("registry+{}{}@{}#sha256={}",
                        pin.release.package.registry.as_str(),
                        pin.release.package.name.as_str(),
                        pin.release.version.text().as_str(),
                        pin.checksum.text());
}

} // namespace lito::source
