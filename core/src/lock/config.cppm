export module lito.core:lock.config;

import rstd;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;

export namespace lito
{

struct LockConfig {
    PathBuf path;
};

} // namespace lito
