export module lito.driver:command.update;

import rstd;
import lito.core;
import :command.error;
import :build.event;
import lito.system;
import :project;

using namespace rstd::prelude;
using namespace lito::system;

export namespace lito
{

struct UpdateRequest {
    PathBuf                root;
    ProcessEnvironmentSpec environment;
    LockConfig             lock;
    PackageSourceConfig    sources;
    Option<BuildEventSink> observer;
};

auto update_dependencies(const UpdateRequest& request) -> CommandResult<LockStatus> {
    auto updated = update_project_dependencies(request.root.as_path(),
                                               request.environment,
                                               request.lock,
                                               request.sources,
                                               request.observer);
    if (updated.is_err()) {
        return Err(rstd::into<CommandError>(rstd::move(updated).unwrap_err()));
    }
    return Ok(rstd::move(updated).unwrap());
}

} // namespace lito
