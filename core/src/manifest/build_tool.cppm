export module lito.core:manifest.build_tool;

import rstd;
import lito.system;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;
using namespace lito::system;

export namespace lito
{

struct BuildToolArchiveManifest {
    HostInfo host;
    String   url;
    String   sha256;
};

struct BuildToolRequirement {
    String                        alias;
    String                        version;
    PathBuf                       executable;
    Vec<BuildToolArchiveManifest> archives;
};

} // namespace lito
