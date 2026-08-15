export module lito.driver:build.artifact;

import rstd;
import lito.core;
import lito.cpp;

using namespace rstd::prelude;

export namespace lito
{

struct BuiltArtifact {
    PackageTargetId   target;
    cpp::ArtifactKind kind { cpp::ArtifactKind::StaticLibrary };
    PathBuf           path;
    PathBuf           package_root;
};

struct BuiltRuntimeResource {
    PackageTargetId target;
    String          name;
    PathBuf         root;
    String          identity;
    Vec<PathBuf>    files;
};

struct CompileTestExecution {
    String               package;
    String               name;
    PathBuf              source;
    CompileTestOutcome   expected { CompileTestOutcome::Failure };
    i32                  exit_code {};
    String               standard_output;
    String               standard_error;
    Option<String>       mismatch;
    rstd::time::Duration elapsed;

    auto success() const noexcept -> bool { return mismatch.is_none(); }
};

struct ConfiguredFile {
    PathBuf                output;
    rstd::fs::WriteOutcome write { rstd::fs::WriteOutcome::Unchanged };
};

struct BuildScriptReport {
    bool                 executed { false };
    rstd::time::Duration elapsed;
    usize                created {};
    usize                replaced {};
    usize                unchanged {};
    usize                stale_removed {};
    Vec<ConfiguredFile>  files;
};

} // namespace lito
