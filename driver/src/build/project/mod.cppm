module lito.driver:build.project;

import rstd;
import lito.core;
import :build.request;
import :build.result;
import :build.error;
import :build.product;
import :package.module_catalog;
import :project;
import lito.system;

using namespace lito::system;

namespace lito
{

auto build_prepared_project(const BuildRequest&               request,
                            const ResolvedProcessEnvironment& environment,
                            PreparedBuildProject              project) -> BuildResult<BuildSummary>;

auto resolve_build_for_recipe(const BuildRequest&               request,
                              lito::workspace::WorkspaceCatalog catalog,
                              const ResolvedProcessEnvironment& environment)
    -> BuildResult<ResolvedBuildProject>;

auto build_and_publish_resolved_project(const BuildRequest&               request,
                                        const ResolvedProcessEnvironment& environment,
                                        ResolvedBuildProject project) -> BuildResult<BuildSummary>;

auto resolve_pack_script_dependencies(const BuildRequest&               request,
                                      lito::workspace::WorkspaceCatalog catalog,
                                      const ResolvedProcessEnvironment& environment,
                                      const CompletedBuildProduct&      product,
                                      rstd::ref<rstd::str>              name)
    -> BuildResult<PackScriptDependencies>;

} // namespace lito
