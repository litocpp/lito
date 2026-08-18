export module lito.cpp:build.unit;

import rstd;
import lito.frontend;
import :bmi.artifact;
import :package.spec;
import :package.target;

using namespace rstd::prelude;

export namespace lito::cpp
{

using UnitId = usize;

struct UnitSpec {
    UnitId                         id {};
    TargetId                       target {};
    PathBuf                        relative_source;
    String                         source_origin_identity;
    PathBuf                        source;
    PathBuf                        object;
    PathBuf                        cache_record;
    Option<PathBuf>                compile_test_record;
    Option<BmiArtifact>            bmi;
    const CompileContext*          context {};
    const ResolvedCompileTestCase* compile_test {};
};

struct PreparedUnit {
    UnitSpec                           unit;
    PathBuf                            working_directory;
    Option<frontend::FrontendAnalysis> frontend_analysis;
};

} // namespace lito::cpp
