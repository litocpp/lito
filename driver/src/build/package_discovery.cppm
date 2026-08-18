module lito.driver:build.package_discovery;

import rstd;
import lito.cpp;
import :build.event;
import :build.error;
import :build.frontend_analysis;

using namespace rstd::prelude;

namespace lito
{

auto discover_package_sources(const cpp::PackageMetadata&          package,
                              const cpp::ResolvedNativeTargetPlan& plan,
                              FrontendAnalysisService&             analysis_service,
                              const Option<BuildEventSink>&        observer,
                              usize                                jobs,
                              usize max_in_flight) -> BuildResult<Vec<cpp::ResolvedTargetSources>>;

} // namespace lito
