export module lito.driver:build.artifact;

import rstd;
import licrypto;
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

struct ProcMacroProviderBinding {
    String package;

    auto clone() const -> ProcMacroProviderBinding {
        return ProcMacroProviderBinding {
            .package = package.clone(),
        };
    }
};

struct BuiltProcMacroProvider {
    lito::package::PackageTargetId target;
    PathBuf                        archive;
    String                         identity;

    auto clone() const -> BuiltProcMacroProvider {
        return BuiltProcMacroProvider {
            .target   = target.clone(),
            .archive  = archive.clone(),
            .identity = identity.clone(),
        };
    }
};

struct BuiltCompilerPlugin {
    lito::package::PackageTargetId target;
    PathBuf                        support_archive;
    PathBuf                        plugin;
    String                         identity;
    String                         content_identity;

    auto clone() const -> BuiltCompilerPlugin {
        return BuiltCompilerPlugin {
            .target           = target.clone(),
            .support_archive  = support_archive.clone(),
            .plugin           = plugin.clone(),
            .identity         = identity.clone(),
            .content_identity = content_identity.clone(),
        };
    }
};

struct ProcMacroAggregateRequest {
    String                        identity;
    Vec<ProcMacroProviderBinding> providers;

    auto clone() const -> ProcMacroAggregateRequest {
        auto copied = Vec<ProcMacroProviderBinding>::with_capacity(providers.len());
        for (const auto& provider : providers) copied.push(provider.clone());
        return ProcMacroAggregateRequest {
            .identity  = identity.clone(),
            .providers = rstd::move(copied),
        };
    }
};

struct BuiltProcMacroAggregate {
    String                        selection_identity;
    String                        identity;
    String                        content_identity;
    PathBuf                       plugin;
    Vec<ProcMacroProviderBinding> providers;

    auto clone() const -> BuiltProcMacroAggregate {
        auto copied = Vec<ProcMacroProviderBinding>::with_capacity(providers.len());
        for (const auto& provider : providers) copied.push(provider.clone());
        return BuiltProcMacroAggregate {
            .selection_identity = selection_identity.clone(),
            .identity           = identity.clone(),
            .content_identity   = content_identity.clone(),
            .plugin             = plugin.clone(),
            .providers          = rstd::move(copied),
        };
    }
};

struct BuiltProcMacroProducts {
    Vec<BuiltProcMacroProvider>  providers;
    Vec<BuiltProcMacroAggregate> aggregates;
};

auto proc_macro_aggregate_identity(const Vec<cpp::ProcMacroDependencySpec>& dependencies)
    -> String {
    auto identity = String::make("lito-proc-macro-aggregate-v4\ncontract:cpp2\n"_str);
    for (const auto& dependency : dependencies) {
        identity.push_str(
            rstd::format("{}:{}\n", dependency.package.size(), dependency.package.as_str())
                .as_str());
    }
    return licrypto::sha256_hex(identity.as_str());
}

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
