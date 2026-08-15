module;
#include <rstd/enum.hpp>

export module lito.driver:install.destination;

import rstd;
import lito.core;

using namespace rstd::prelude;

export namespace lito
{

struct InstallRoot {
    PathBuf path;
};

struct InstallPrefix {
    PathBuf path;
};

class InstallDestinationRequirement {
    RSTD_ENUM(InstallDestinationRequirement,
              (Managed, (Option<PathBuf> command_root;)),
              (Prefix, (PathBuf path;)))
};

class InstallDestination {
    RSTD_ENUM(InstallDestination, (Managed, (InstallRoot root;)), (Prefix, (InstallPrefix prefix;)))

public:
    auto clone() const -> InstallDestination {
        if (is_Managed()) {
            return InstallDestination::Managed(
                InstallRoot { .path = as_Managed().root.path.clone() });
        }
        return InstallDestination::Prefix(
            InstallPrefix { .path = as_Prefix().prefix.path.clone() });
    }

    auto path() const noexcept -> ref<rstd::path::Path> {
        return is_Managed() ? as_Managed().root.path.as_path() : as_Prefix().prefix.path.as_path();
    }
};

struct InstallLayout {
    InstallRoot root;
    PathBuf     bin_directory;
    PathBuf     packages_directory;
    PathBuf     lock;
    PathBuf     transactions;
};

} // namespace lito
