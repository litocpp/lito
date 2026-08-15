module;
#include <rstd/enum.hpp>

export module lito.driver:install.package;

import rstd;
import lito.core;

using namespace rstd::prelude;

export namespace lito
{

struct InstallRuntimeDependency {
    String name;
    String source_identity;
};

struct InstallPackageIdentity {
    String id;
    String name;
    String source_identity;

    auto clone() const -> InstallPackageIdentity {
        return InstallPackageIdentity {
            .id              = id.clone(),
            .name            = name.clone(),
            .source_identity = source_identity.clone(),
        };
    }
};

class InstallSourceProvenance {
    RSTD_ENUM(InstallSourceProvenance,
              (Local, (PathBuf root; String identity;)),
              (Git, (String url; GitReference reference; String commit; String identity;)))

public:
    auto clone() const -> InstallSourceProvenance {
        if (is_Local()) {
            return InstallSourceProvenance::Local(as_Local().root.clone(),
                                                  as_Local().identity.clone());
        }
        return InstallSourceProvenance::Git(as_Git().url.clone(),
                                            as_Git().reference.clone(),
                                            as_Git().commit.clone(),
                                            as_Git().identity.clone());
    }
};

} // namespace lito
