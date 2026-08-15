module;
#include <rstd/enum.hpp>

export module lito.core:dependency.source;

import rstd;
import lito.system;
import :source.git;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;
using namespace lito::system;

export namespace lito
{

class ResolvedExternalSource {
    RSTD_ENUM(ResolvedExternalSource,
              (Path, (PathBuf path;)),
              (Package, (PathBuf path;)),
              (Git, (String url; GitReference reference; String commit;)),
              (Archive, (String url; String sha256;)))
};

struct ResolvedBuildToolSourceMetadata {
    String  version;
    PathBuf executable;
    String  operating_system;
    u64     schema_version { u64(1) };
};

struct ResolvedExternalSourceRecord {
    String                                  package;
    String                                  alias;
    String                                  provider;
    Vec<Architecture>                       architectures;
    Option<ResolvedBuildToolSourceMetadata> build_tool;
    ResolvedExternalSource                  source;
};

} // namespace lito
