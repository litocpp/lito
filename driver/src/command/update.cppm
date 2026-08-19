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
    PathBuf                           root;
    ProcessEnvironmentSpec            environment;
    ToolSpec                          tools;
    lito::lock::LockConfig            lock;
    lito::source::PackageSourceConfig sources;
    Option<BuildEventSink>            observer;
    Option<HostToolResolutionSink>    tool_reporter;
};

auto update_dependencies(const UpdateRequest& request) -> CommandResult<lito::lock::LockStatus>;

} // namespace lito
