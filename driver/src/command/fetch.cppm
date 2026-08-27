module;
#include <rstd/enum.hpp>

export module lito.driver:command.fetch;

import rstd;
import lito.tools;
import lito.tools.cargo;
import lito.core;
import :config.project;
import :config.registry;
import :command.error;
import :build.event;
import :package.selection;
import lito.system;

using namespace rstd::prelude;
using namespace lito::system;

export namespace lito
{

class FetchDestination {
    RSTD_ENUM(FetchDestination, (GlobalCache), (SourceBundle, (PathBuf path;)))
};

struct FetchRequest {
    lito::package::PackageSelection             selection;
    ProcessEnvironmentSpec                      environment;
    lito::tools::ToolSpec                       tools;
    Option<config::LitoBootstrapConfig>         registries;
    config::BuildConfigurationRequest           configuration;
    lito::lock::LockConfig                      lock;
    lito::source::PackageSourceConfig           sources;
    lito::tools::cargo::Configuration           cargo;
    lito::dependency::CMakeBuildOverrideSet     cmake_build_overrides;
    FetchDestination                            destination { FetchDestination::GlobalCache() };
    bool                                        locked { false };
    usize                                       jobs { usize(1) };
    Option<BuildEventSink>                      observer;
    Option<lito::tools::HostToolResolutionSink> tool_reporter;
};

struct FetchSummary {
    lito::lock::LockStatus lock { lito::lock::LockStatus::Unchanged };
    FetchDestination       destination;
    usize                  entries {};
};

auto fetch_dependencies(const FetchRequest& request) -> CommandResult<FetchSummary>;

} // namespace lito
