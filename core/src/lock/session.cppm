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

export namespace lito
{

enum class LockStatus
{
    Unchanged,
    Updated,
};

class LockSession {
    bool                    locked_ { false };
    PathBuf                 root_;
    PathBuf                 destination_;
    Option<Json>            existing_;
    Option<LockedProject>   project_;
    SourceResolutionOptions options_;

public:
    LockSession() = default;

    auto take_resolution_options() -> SourceResolutionOptions { return rstd::move(options_); }

    friend auto load_lock_session(ref<rstd::path::Path> root,
                                  const LockConfig&     config,
                                  bool                  locked,
                                  GitResolutionMode     git) -> LockResult<LockSession>;
    friend auto sync_lock(const ResolvedPackageGraph& graph, LockSession session)
        -> LockResult<LockStatus>;
};

auto load_locked_project(ref<rstd::path::Path> root, const LockConfig& config = {})
    -> LockResult<LockedProject>;

auto load_lock_session(ref<rstd::path::Path> root,
                       const LockConfig&     config,
                       bool                  locked,
                       GitResolutionMode     git = GitResolutionMode::ReuseLocked)
    -> LockResult<LockSession>;

auto load_lock_session(ref<rstd::path::Path> root,
                       bool                  locked,
                       GitResolutionMode     git = GitResolutionMode::ReuseLocked)
    -> LockResult<LockSession>;

auto sync_lock(const ResolvedPackageGraph& graph, LockSession session) -> LockResult<LockStatus>;

} // namespace lito
