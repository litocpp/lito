module lito.driver:build.prepared_project;

import rstd;
import :build.request;
import :build.result;
import :build.error;
import :project;
import lito.system;

using namespace lito::system;

namespace lito
{

auto build_prepared_project(const BuildRequest&               request,
                            const ResolvedProcessEnvironment& environment,
                            PreparedBuildProject              project) -> BuildResult<BuildSummary>;

} // namespace lito
