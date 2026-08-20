export module lito.driver:build;

import rstd;
export import :build.event;
export import :build.request;
export import :build.artifact;
export import :build.documentation;
export import :build.result;
export import :build.error;
export import :build.setup_report;
export import :build.layout;
export import :build.layout_error;
export import :build.discovery;
export import :build.host_tool;
export import :build.host_tool_error;
export import :build.script_error;
export import :build.tool_action_error;
export import :build.compile_executor;
export import :build.profiling;
export import :build.product;
export import :build.product_error;
export import :cache.error;
export import :project.error;
import lito.core;
import lito.system;

using namespace rstd::prelude;
using namespace lito::system;

namespace lito
{

auto build_with_environment(const BuildRequest&               request,
                            const ResolvedProcessEnvironment& process_environment)
    -> BuildResult<BuildSummary>;

} // namespace lito

export namespace lito
{

auto build_resolved_project(BuildRequest request, lito::workspace::ResolvedProjectEntry project)
    -> BuildResult<BuildSummary>;
auto build(const BuildRequest& request) -> BuildResult<BuildSummary>;

} // namespace lito
