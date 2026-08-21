export module lito.core:lock.session;

import rstd;
import rstd.json;
import :lock.error;
import :lock.config;
import :lock.document;
import :source.git;
import :source.resolution;
import :package.graph;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;
using Json    = rstd::json::Value;

export namespace lito::lock
{

enum class LockStatus
{
    Unchanged,
    Updated,
};

enum class InvalidLockPolicy
{
    Reject,
    Replace,
};

class LockSession {
    bool                                  locked_ { false };
    PathBuf                               root_;
    PathBuf                               destination_;
    Option<Json>                          existing_;
    Option<LockedProject>                 project_;
    lito::source::SourceResolutionOptions options_;

public:
    LockSession() = default;

    auto take_resolution_options() -> lito::source::SourceResolutionOptions {
        return rstd::move(options_);
    }

    friend auto load_lock_session(ref<rstd::path::Path>           root,
                                  const LockConfig&               config,
                                  bool                            locked,
                                  lito::source::GitResolutionMode git,
                                  InvalidLockPolicy invalid) -> LockResult<LockSession>;
    friend auto sync_lock(const lito::package::ResolvedPackageGraph& graph, LockSession session)
        -> LockResult<LockStatus>;
};

auto load_locked_project(ref<rstd::path::Path> root, const LockConfig& config = {})
    -> LockResult<LockedProject>;

auto load_lock_session(
    ref<rstd::path::Path>           root,
    const LockConfig&               config,
    bool                            locked,
    lito::source::GitResolutionMode git     = lito::source::GitResolutionMode::ReuseLocked,
    InvalidLockPolicy               invalid = InvalidLockPolicy::Reject) -> LockResult<LockSession>;

auto load_lock_session(
    ref<rstd::path::Path>           root,
    bool                            locked,
    lito::source::GitResolutionMode git     = lito::source::GitResolutionMode::ReuseLocked,
    InvalidLockPolicy               invalid = InvalidLockPolicy::Reject) -> LockResult<LockSession>;

auto sync_lock(const lito::package::ResolvedPackageGraph& graph, LockSession session)
    -> LockResult<LockStatus>;

} // namespace lito::lock
