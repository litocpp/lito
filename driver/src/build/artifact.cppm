export module lito.driver:build.artifact;

import rstd;
import licrypto;
import lito.core;
import lito.cpp;
import :build.request;

using namespace rstd::prelude;

export namespace lito
{

enum class ArtifactFileRole
{
    LinkInput,
    Runtime,
    ImportLibrary,
    JavaScriptModule,
    TypeScriptDeclaration,
    DebugInformation,
    Metadata,
};

auto artifact_file_role_name(ArtifactFileRole role) noexcept -> ref<str> {
    using namespace rstd::literals;
    switch (role) {
    case ArtifactFileRole::LinkInput: return "link-input"_str;
    case ArtifactFileRole::Runtime: return "runtime"_str;
    case ArtifactFileRole::ImportLibrary: return "import-library"_str;
    case ArtifactFileRole::JavaScriptModule: return "javascript-module"_str;
    case ArtifactFileRole::TypeScriptDeclaration: return "typescript-declaration"_str;
    case ArtifactFileRole::DebugInformation: return "debug-information"_str;
    case ArtifactFileRole::Metadata: return "metadata"_str;
    }
    return "unknown"_str;
}

auto artifact_file_role_from_name(ref<str> value) noexcept -> Option<ArtifactFileRole> {
    using namespace rstd::literals;
    if (value == "link-input"_str) return Some(ArtifactFileRole::LinkInput);
    if (value == "runtime"_str) return Some(ArtifactFileRole::Runtime);
    if (value == "import-library"_str) return Some(ArtifactFileRole::ImportLibrary);
    if (value == "javascript-module"_str) return Some(ArtifactFileRole::JavaScriptModule);
    if (value == "typescript-declaration"_str) return Some(ArtifactFileRole::TypeScriptDeclaration);
    if (value == "debug-information"_str) return Some(ArtifactFileRole::DebugInformation);
    if (value == "metadata"_str) return Some(ArtifactFileRole::Metadata);
    return None();
}

struct BuiltArtifactFile {
    ArtifactFileRole role { ArtifactFileRole::Runtime };
    PathBuf          path;
    String           content_type;
    String           content_identity;
    bool             publish { true };

    auto clone() const -> BuiltArtifactFile {
        return BuiltArtifactFile {
            .role             = role,
            .path             = path.clone(),
            .content_type     = content_type.clone(),
            .content_identity = content_identity.clone(),
            .publish          = publish,
        };
    }
};

struct BuiltArtifact {
    lito::package::PackageTargetId    target;
    cpp::ArtifactKind                 kind { cpp::ArtifactKind::StaticLibrary };
    lito::artifact::Format            format { lito::artifact::Format::Archive };
    BuiltArtifactFile                 primary;
    Vec<BuiltArtifactFile>            companions;
    PathBuf                           package_root;
    Option<InstallArtifactLinkPolicy> install_link;
    String                            link_identity;

    auto clone() const -> BuiltArtifact {
        auto copied = Vec<BuiltArtifactFile>::with_capacity(companions.len());
        for (const auto& file : companions) copied.push(file.clone());
        return BuiltArtifact {
            .target        = target.clone(),
            .kind          = kind,
            .format        = format,
            .primary       = primary.clone(),
            .companions    = rstd::move(copied),
            .package_root  = package_root.clone(),
            .install_link  = as<Clone>(install_link).clone(),
            .link_identity = link_identity.clone(),
        };
    }
};

auto artifact_file(const BuiltArtifact& artifact, ArtifactFileRole role)
    -> Option<ref<BuiltArtifactFile>> {
    if (artifact.primary.role == role) {
        return Some(ref<BuiltArtifactFile>::from_raw_parts(rstd::addressof(artifact.primary)));
    }
    for (const auto& file : artifact.companions) {
        if (file.role == role) {
            return Some(ref<BuiltArtifactFile>::from_raw_parts(rstd::addressof(file)));
        }
    }
    return None();
}

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
