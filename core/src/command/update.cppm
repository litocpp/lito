export module lito.command.update;

import rstd;
import lito.error;
import lito.command.error_contract;
import lito.command.project_contract;
import lito.lock.contract;
import lito.project;

using namespace rstd::prelude;

export namespace lito
{

auto update_dependencies(const UpdateRequest& request) -> CommandResult<LockStatus> {
    auto updated = update_project_dependencies(request);
    if (updated.is_err()) {
        return Err(rstd::into<CommandError>(rstd::move(updated).unwrap_err()));
    }
    return Ok(rstd::move(updated).unwrap());
}

} // namespace lito
