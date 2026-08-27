export module lito.core:manifest.build_tool;

import rstd;
import licrypto;
import lito.system;
import :parse;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;
using namespace lito::system;

export namespace lito::manifest
{

struct BuildToolArchiveManifest {
    HostInfo               host;
    lito::parse::HttpsUrl  url;
    licrypto::Sha256Digest sha256;
};

struct BuildToolRequirement {
    String                        alias;
    String                        version;
    PathBuf                       executable;
    Vec<BuildToolArchiveManifest> archives;
};

} // namespace lito::manifest
