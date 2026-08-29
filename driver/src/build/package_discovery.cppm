module lito.driver:build.package_discovery;

import rstd;
import lito.cpp;
import :build.event;
import :build.error;
import :build.frontend_analysis;

using namespace rstd::prelude;

namespace lito
{

enum class SourceDiscoveryScope
{
    Existing,
    Generated,
    All,
};

auto discover_package_sources(const cpp::PackageMetadata&          package,
                              const cpp::ResolvedNativeTargetPlan& plan,
                              cpp::SemanticScanGraphBuilder&       graph,
                              FrontendAnalysisService&             analysis_service,
                              const Option<BuildEventSink>&        observer,
                              usize                                jobs,
                              usize max_in_flight) -> BuildResult<Vec<cpp::ResolvedTargetSources>>;
auto discover_package_source_selection(const cpp::PackageMetadata&          package,
                                       const cpp::ResolvedNativeTargetPlan& plan,
                                       const Vec<cpp::TargetId>&            targets,
                                       cpp::SemanticScanGraphBuilder&       graph,
                                       FrontendAnalysisService&             analysis_service,
                                       const Option<BuildEventSink>&        observer,
                                       usize                                jobs,
                                       usize                                max_in_flight,
                                       bool                                 finish,
                                       SourceDiscoveryScope                 scope)
    -> BuildResult<Vec<cpp::ResolvedTargetSources>>;
auto discover_package_source_selection(const cpp::PackageMetadata&    package,
                                       const cpp::PackagePlan&        plan,
                                       const Vec<cpp::TargetId>&      targets,
                                       cpp::SemanticScanGraphBuilder& graph,
                                       FrontendAnalysisService&       analysis_service,
                                       const Option<BuildEventSink>&  observer,
                                       usize                          jobs,
                                       usize                          max_in_flight,
                                       bool                           finish,
                                       SourceDiscoveryScope           scope)
    -> BuildResult<Vec<cpp::ResolvedTargetSources>>;

} // namespace lito
