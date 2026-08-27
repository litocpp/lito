module;
#include <rstd/enum.hpp>

export module lito.core:lock.document;

import rstd;
import licrypto;
import lito.system;
import :source.git;
import :parse;
import :registry.archive;
import :registry.digest;
import :registry.identity;
import :registry.version;

using namespace rstd::prelude;

export namespace lito::lock
{

inline constexpr auto LOCK_FORMAT_VERSION = u64(1);

class LockedSource {
    RSTD_ENUM(LockedSource,
              (Git, (String url; String commit;)),
              (Archive, (lito::parse::FetchUrl url; licrypto::Sha256Digest sha256;)),
              (Registry,
               (lito::registry::RegistryPackageId package; lito::registry::SemanticVersion version;
                lito::registry::PackageChecksum checksum;)))
};

struct LockedPackageExternalSource {
    String                          name;
    Vec<lito::system::Architecture> architectures;
    LockedSource                    source;
};

struct LockedPackage {
    String                           name;
    Option<String>                   version;
    Option<LockedSource>             source;
    Vec<String>                      dependencies;
    Vec<String>                      runtime_dependencies;
    Vec<LockedPackageExternalSource> externals;
};

struct LockedProject {
    Vec<LockedPackage> packages;
};

} // namespace lito::lock
