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

    auto clone() const -> BuiltArtifact {
        return BuiltArtifact {
            .target        = target.clone(),
            .kind          = kind,
            .path          = path.clone(),
            .package_root  = package_root.clone(),
            .install_link  = as<Clone>(install_link).clone(),
            .link_identity = link_identity.clone(),
        };
    }
};

struct BuiltRuntimeResource {
    lito::package::PackageTargetId target;
    String                         name;
    PathBuf                        root;
    String                         identity;
    Vec<PathBuf>                   files;

    auto clone() const -> BuiltRuntimeResource {
        return BuiltRuntimeResource {
            .target   = target.clone(),
            .name     = name.clone(),
            .root     = root.clone(),
            .identity = identity.clone(),
            .files    = as<Clone>(files).clone(),
        };
    }
};

struct BuiltTargetRuntime {
    String  name;
    PathBuf path;
    String  identity;

    auto clone() const -> BuiltTargetRuntime {
        return BuiltTargetRuntime { .name     = name.clone(),
                                    .path     = path.clone(),
                                    .identity = identity.clone() };
    }
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
    PathBuf                input;
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
