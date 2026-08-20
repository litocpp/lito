export module lito.driver:build.artifact;

import rstd;
import lito.core;
import lito.cpp;
import :build.request;

using namespace rstd::prelude;

export namespace lito
{

struct BuiltArtifact {
    lito::package::PackageTargetId    target;
    cpp::ArtifactKind                 kind { cpp::ArtifactKind::StaticLibrary };
    PathBuf                           path;
    PathBuf                           package_root;
    Option<InstallArtifactLinkPolicy> install_link;
    String                            link_identity;
};

struct BuiltRuntimeResource {
    lito::package::PackageTargetId target;
    String                         name;
    PathBuf                        root;
    String                         identity;
    Vec<PathBuf>                   files;
};

struct BuiltTargetRuntime {
    String  name;
    PathBuf path;
    String  identity;
};

struct CompileTestExecution {
    String                             package;
    String                             name;
    PathBuf                            source;
    lito::manifest::CompileTestOutcome expected { lito::manifest::CompileTestOutcome::Failure };
    i32                                exit_code {};
    String                             standard_output;
    String                             standard_error;
    Option<String>                     mismatch;
    rstd::time::Duration               elapsed;

    auto success() const noexcept -> bool { return mismatch.is_none(); }
};

struct ConfiguredFile {
    PathBuf                output;
    rstd::fs::WriteOutcome write { rstd::fs::WriteOutcome::Unchanged };
};

struct BuildScriptExecution {
    String               owner;
    PathBuf              script;
    rstd::time::Duration elapsed;
};

struct BuildScriptReport {
    bool                      executed { false };
    rstd::time::Duration      elapsed;
    usize                     created {};
    usize                     replaced {};
    usize                     unchanged {};
    usize                     stale_removed {};
    Vec<ConfiguredFile>       files;
    Vec<BuildScriptExecution> executions;
};

} // namespace lito
