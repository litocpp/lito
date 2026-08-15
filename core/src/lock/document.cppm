module;
#include <rstd/enum.hpp>

export module lito.core:lock.document;

import rstd;
import :source.git;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;

export namespace lito
{

inline constexpr auto LOCK_FORMAT_VERSION = u64(1);

class LockedPackageSource {
    RSTD_ENUM(LockedPackageSource,
              (Path, (PathBuf path;)),
              (Git, (String url; GitReference reference; String commit;)))
};

class LockedExternalSource {
    RSTD_ENUM(LockedExternalSource,
              (Path, (PathBuf path;)),
              (Package, (PathBuf path;)),
              (Git, (String url; GitReference reference; String commit;)),
              (Archive, (String url; String sha256;)))
};

struct LockedBuildToolSourceMetadata {
    String  version;
    PathBuf executable;
    String  operating_system;
    u64     schema_version { u64(1) };
};

struct LockedPackage {
    String              name;
    Option<String>      version;
    LockedPackageSource source;
    PathBuf             manifest;
    Vec<String>         dependencies;
    Vec<String>         runtime_dependencies;
};

struct LockedExternal {
    String                                package;
    String                                alias;
    String                                provider;
    Vec<String>                           architectures;
    Option<LockedBuildToolSourceMetadata> build_tool;
    LockedExternalSource                  source;
};

struct LockedProject {
    Vec<LockedPackage>  packages;
    Vec<LockedExternal> externals;
};

} // namespace lito
