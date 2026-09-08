module;
#include <rstd/enum.hpp>

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

struct ArchiveBuildTool {
    String                        version;
    PathBuf                       executable;
    Vec<BuildToolArchiveManifest> archives;
};

class BuildToolSource {
    RSTD_ENUM(BuildToolSource, (Path, (PathBuf requested;)), (Archive, (ArchiveBuildTool recipe;)))
};

struct BuildToolRequirement {
    String          alias;
    BuildToolSource source;
};

} // namespace lito::manifest
