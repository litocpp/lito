export module lito.source.discovery_contract;

import rstd;
import lito.error;
import lito.frontend;
import lito.source.contract;
import lito.package.identity;

using namespace rstd::prelude;

export namespace lito
{

enum class SourceOrigin
{
    Explicit,
    Convention,
};

using ProvidedModule = frontend::ProvidedModule;
using SourceLocation = frontend::DependencyLocation;
using ModuleImport   = frontend::ModuleImport;
using FrontendResult = frontend::FrontendResult;

struct ResolvedSource {
    PathBuf                            relative_path;
    PathBuf                            canonical_path;
    SourceOrigin                       origin { SourceOrigin::Explicit };
    Option<String>                     expected_module;
    Option<frontend::FrontendAnalysis> frontend_analysis;
};

struct ResolvedSourceSet {
    Vec<ResolvedSource> sources;
};

struct ResolvedTargetSources {
    PackageTargetId   target;
    ResolvedSourceSet sources;
};

} // namespace lito
