module;
#include <rstd/enum.hpp>

export module lito.driver:command.clean;

import rstd;
import lito.core;
import :command.error;

using namespace rstd::prelude;

export namespace lito
{

class CleanTarget {
    RSTD_ENUM(CleanTarget,
              (All),
              (Profile, (lito::manifest::BuildProfileName profile;)),
              (Directory, (PathBuf path;)))
};

struct CleanRequest {
    PathBuf     root;
    CleanTarget target;
};

struct CleanSummary {
    PathBuf path;
    bool    removed {};
};

auto clean(const CleanRequest& request) -> CommandResult<CleanSummary>;

} // namespace lito
