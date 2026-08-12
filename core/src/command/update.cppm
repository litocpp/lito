export module lito.command.update;

import lito.error;
import lito.command.project_contract;
import lito.lock.contract;
import lito.project;

export namespace lito
{

auto update_dependencies(const UpdateRequest& request) -> Result<LockStatus> {
    return update_project_dependencies(request);
}

} // namespace lito
