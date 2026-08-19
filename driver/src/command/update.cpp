module lito.driver;

import rstd;
import lito.core;
import :command.error;
import :build.event;
import lito.system;
import :project;

using namespace rstd::prelude;
using namespace lito::system;

namespace lito
{

auto update_dependencies(const UpdateRequest& request) -> CommandResult<lito::lock::LockStatus> {
    auto updated = update_project_dependencies(request.root.as_path(),
                                               request.environment,
                                               request.tools,
                                               request.lock,
                                               request.sources,
                                               request.observer,
                                               request.tool_reporter);
    if (updated.is_err()) {
        return Err(rstd::into<CommandError>(rstd::move(updated).unwrap_err()));
    }
    return Ok(rstd::move(updated).unwrap());
}

} // namespace lito
