export module lito.driver:build;

import rstd;
export import :build.event;
export import :build.action_graph;
export import :build.request;
export import :build.artifact;
export import :build.artifact.processor.error;
export import :build.documentation;
export import :build.result;
export import :build.error;
export import :build.setup;
export import :build.layout;
export import :build.layout.error;
export import :build.discovery;
export import :build.host_tool;
export import :build.host_tool.error;
export import :build.script.error;
export import :build.generated.error;
export import :build.compile;
export import :build.profiling;
export import :build.product;
export import :build.product.error;
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
