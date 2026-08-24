module;
#include <rstd/enum.hpp>

export module lito.cpp:package.target;

import rstd;
import lito.core;
import :build.scan;
import :compiler.option;

using namespace rstd::prelude;

export namespace lito::cpp
{

enum class ArtifactKind
{
    StaticLibrary,
    SharedLibrary,
    TestAttachmentArchive,
    Executable,
    TestExecutable,
    BenchmarkExecutable,
    CompileTest,
};

struct DependencySpec {
    lito::package::PackageTargetId         target;
    lito::dependency::DependencyVisibility visibility {
        lito::dependency::DependencyVisibility::Private
    };
};

struct TargetSource {
    PathBuf                    relative_path;
    PathBuf                    path;
    PathBuf                    source_root;
    String                     origin_identity;
    bool                       external { false };
    Option<String>             expected_module;
    Option<SourceScanArtifact> scan_artifact;
};

struct TestAttachmentTarget {
    lito::package::PackageTargetId test_target;
    lito::package::PackageTargetId library_target;
};

struct ResolvedCompileTestCase {
    String                             name;
    PathBuf                            source;
    lito::manifest::CompileTestOutcome outcome { lito::manifest::CompileTestOutcome::Failure };
    CppArgumentLayer                   arguments;
    Vec<String>                        diagnostic_contains;
    Vec<String>                        diagnostic_contains_any;
};

} // namespace lito::cpp
