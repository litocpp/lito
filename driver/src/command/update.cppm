export module lito.driver:command.update;

import rstd;
import lito.core;
import :command.error;
import :build.event;
import lito.system;

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

auto update_dependencies(const UpdateRequest& request) -> CommandResult<LockStatus>;

} // namespace lito
