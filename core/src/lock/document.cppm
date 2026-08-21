module;
#include <rstd/enum.hpp>

export module lito.core:lock.document;

import rstd;
import :source.git;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;

export namespace lito::lock
{

inline constexpr auto LOCK_FORMAT_VERSION = u64(2);

class LockedSource {
    RSTD_ENUM(LockedSource,
              (Path, (PathBuf path;)),
              (Package, (PathBuf path;)),
              (Builtin, (String id; String digest;)),
              (Git, (String url; lito::source::GitReference reference; String commit;)),
              (Archive, (String url; String sha256;)))
};

struct LockedPackageExternalSource {
    String       name;
    Vec<String>  architectures;
    LockedSource source;
};

struct LockedPackage {
    String                           name;
    Option<String>                   version;
    LockedSource                     source;
    PathBuf                          manifest;
    Vec<String>                      dependencies;
    Vec<String>                      runtime_dependencies;
    Vec<LockedPackageExternalSource> externals;
};

struct LockedProject {
    Vec<LockedPackage> packages;
};

} // namespace lito::lock
