module;
#include <rstd/enum.hpp>

export module lito.core:lock.document;

import rstd;
import lito.crypto;
import :source.git;
import :parse;
import :registry.archive;
import :registry.digest;
import :registry.identity;
import :registry.version;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;

export namespace lito::lock
{

inline constexpr auto LOCK_FORMAT_VERSION = u64(3);

class LockedSource {
    RSTD_ENUM(LockedSource,
              (Path, (PathBuf path;)),
              (Package, (PathBuf path;)),
              (Builtin, (String id; String digest;)),
              (Git, (String url; lito::source::GitReference reference; String commit;)),
              (Archive, (lito::parse::FetchUrl url; lito::crypto::Sha256Digest sha256;)),
              (Registry,
               (lito::registry::RegistryPackageId package; lito::registry::SemanticVersion version;
                lito::registry::ReleaseDigest                                              release;
                lito::registry::SourceDigest                                               source;
                lito::registry::ManifestDigest                                             manifest;
                lito::registry::BlobDigest                                                 blob;
                lito::registry::RegistryBlobSize      blob_size;
                lito::registry::RegistryArchiveFormat format;)))
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
