module;
#include <rstd/enum.hpp>

export module lito.driver:install.entry;

import rstd;
import lito.core;

using namespace rstd::prelude;

export namespace lito
{

enum class InstallAction
{
    Created,
    Replaced,
    Unchanged,
};

struct InstallBinary {
    lito::package::PackageTargetId target;
    PathBuf                        source;
    PathBuf                        destination;
    InstallAction                  action { InstallAction::Created };
};

class InstallEntryOrigin {
    RSTD_ENUM(InstallEntryOrigin,
              (PackageFile, (String package; PathBuf path;)),
              (BuildArtifact, (lito::package::PackageTargetId target;)),
              (ExternalAsset, (String dependency; String set; PathBuf path;)),
              (Template, (PathBuf input;)),
              (Inventory))
};

class InstallEntryPayload {
    RSTD_ENUM(InstallEntryPayload,
              (CopyFile, (PathBuf source;)),
              (Bytes, (Vec<u8> contents; u32 permissions;)))
};

struct InstallEntry {
    InstallEntryOrigin  origin;
    InstallEntryPayload payload;
    PathBuf             relative_destination;
    PathBuf             destination;
    InstallAction       action { InstallAction::Created };
};

} // namespace lito
