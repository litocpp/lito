module;
#include <rstd/enum.hpp>

export module lito.cpp:package.target;

import rstd;
import lito.core;
import lito.frontend;
import :compiler.option;

using namespace rstd::prelude;

export namespace lito::cpp
{

enum class ArtifactKind
{
    StaticLibrary,
    TestAttachmentArchive,
    Executable,
    TestExecutable,
    BenchmarkExecutable,
    CompileTest,
};

struct DependencySpec {
    PackageTargetId      target;
    DependencyVisibility visibility { DependencyVisibility::Private };
};

struct TargetSource {
    PathBuf                            relative_path;
    PathBuf                            path;
    Option<String>                     expected_module;
    Option<frontend::FrontendAnalysis> frontend_analysis;
};

struct TestAttachmentTarget {
    PackageTargetId test_target;
    PackageTargetId library_target;
};

struct ResolvedCompileTestCase {
    String             name;
    PathBuf            source;
    CompileTestOutcome outcome { CompileTestOutcome::Failure };
    CppArgumentLayer   arguments;
    Vec<String>        diagnostic_contains;
    Vec<String>        diagnostic_contains_any;
};

} // namespace lito::cpp
