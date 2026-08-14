module;
#include <rstd/enum.hpp>

export module lito.install.contract;

import rstd;
import rstd.json;
import lito.error;
import lito.build.contract;
import lito.build.error_contract;
import lito.config.contract;
import lito.package.identity;
import lito.source.contract;
import lito.install.package_contract;
import lito.workspace;
import lito.install.script_error_contract;
import lito.install.materialize_error_contract;

using namespace rstd::prelude;

export namespace lito
{

enum class InstallSourceStorage
{
    BorrowedLocal,
    ManagedCache,
};

class InstallSourceError {
    RSTD_ENUM(InstallSourceError,
              (Workspace, (WorkspaceError source;)),
              (Message, (String message;)))
};

template<typename T>
using InstallSourceResult = rstd::Result<T, InstallSourceError>;

class InstallStoreCause {
    RSTD_ENUM(InstallStoreCause,
              (Source, (InstallSourceError source;)),
              (Io, (String operation; PathBuf path; rstd::io::error::Error source;)),
              (Json, (PathBuf path; rstd::json::Error source;)),
              (Message, (String message;)))
};

struct InstallRollbackFailure {
    String                 operation;
    PathBuf                path;
    rstd::io::error::Error source;
};

class InstallStoreError {
    RSTD_ENUM(InstallStoreError,
              (Cause, (InstallStoreCause source;)),
              (Transaction,
               (String operation; rstd::boxed::Box<InstallStoreError> source;
                Vec<InstallRollbackFailure>                           rollback_failures;)))
};

template<typename T>
using InstallStoreResult = rstd::Result<T, InstallStoreError>;

class InstallError {
    RSTD_ENUM(InstallError,
              (Source, (InstallSourceError source;)),
              (Selection, (WorkspaceError source;)),
              (Script, (InstallScriptError source;)),
              (Build, (BuildError source;)),
              (Materialize, (InstallMaterializeError source;)),
              (Store, (InstallStoreError source;)),
              (Message, (String message;)))
};

template<typename T>
using InstallResult = rstd::Result<T, InstallError>;

class InstallSourceRequirement {
    RSTD_ENUM(InstallSourceRequirement, (LocalProject, (PathBuf requested_root;)))
};

struct ResolvedInstallSource {
    ResolvedProjectEntry    project;
    InstallSourceProvenance provenance;
    String                  identity;
    InstallSourceStorage    storage { InstallSourceStorage::BorrowedLocal };
};

struct InstallRoot {
    PathBuf path;
};

struct InstallPrefix {
    PathBuf path;
};

class InstallDestinationRequirement {
    RSTD_ENUM(InstallDestinationRequirement,
              (Managed, (Option<PathBuf> command_root;)),
              (Prefix, (PathBuf path;)))
};

class InstallDestination {
    RSTD_ENUM(InstallDestination, (Managed, (InstallRoot root;)), (Prefix, (InstallPrefix prefix;)))

public:
    auto clone() const -> InstallDestination {
        if (is_Managed()) {
            return InstallDestination::Managed(
                InstallRoot { .path = as_Managed().root.path.clone() });
        }
        return InstallDestination::Prefix(
            InstallPrefix { .path = as_Prefix().prefix.path.clone() });
    }

    auto path() const noexcept -> ref<rstd::path::Path> {
        return is_Managed() ? as_Managed().root.path.as_path() : as_Prefix().prefix.path.as_path();
    }
};

struct InstallLayout {
    InstallRoot root;
    PathBuf     bin_directory;
    PathBuf     packages_directory;
    PathBuf     lock;
    PathBuf     transactions;
};

enum class InstallAction
{
    Created,
    Replaced,
    Unchanged,
};

struct InstallBinary {
    PackageTargetId target;
    PathBuf         source;
    PathBuf         destination;
    InstallAction   action { InstallAction::Created };
};

class InstallEntryOrigin {
    RSTD_ENUM(InstallEntryOrigin,
              (PackageFile, (String package; PathBuf path;)),
              (BuildArtifact, (PackageTargetId target;)),
              (ExternalAsset, (String dependency; String set; PathBuf path;)),
              (Template, (PathBuf input;)),
              (Inventory))
};

class InstallEntryPayload {
    RSTD_ENUM(InstallEntryPayload,
              (CopyFile, (PathBuf source;)),
              (Bytes, (Vec<u8> contents; u32 permissions;)))
};

struct InstallEntry {
    InstallEntryOrigin  origin;
    InstallEntryPayload payload;
    PathBuf             relative_destination;
    PathBuf             destination;
    InstallAction       action { InstallAction::Created };
};

enum class InstallManagedPackageLayout
{
    DirectBin,
    IsolatedPrefix,
};

enum class InstallOwnedEntryKind
{
    File,
    SoftLink,
};

struct InstallOwnedEntry {
    PathBuf               logical_destination;
    PathBuf               physical_destination;
    InstallOwnedEntryKind kind { InstallOwnedEntryKind::File };
    String                origin;
    Option<PathBuf>       link_target;

    auto clone() const -> InstallOwnedEntry {
        return InstallOwnedEntry {
            .logical_destination  = logical_destination.clone(),
            .physical_destination = physical_destination.clone(),
            .kind                 = kind,
            .origin               = origin.clone(),
            .link_target          = link_target.is_some() ? Some(link_target->clone()) : None(),
        };
    }
};

struct InstallStoredRuntimeDependency {
    String package_id;
    String name;
    String source_identity;

    auto clone() const -> InstallStoredRuntimeDependency {
        return InstallStoredRuntimeDependency {
            .package_id      = package_id.clone(),
            .name            = name.clone(),
            .source_identity = source_identity.clone(),
        };
    }
};

struct InstallPackageInfo {
    InstallPackageIdentity              identity;
    String                              version;
    InstallSourceProvenance             provenance;
    String                              profile;
    String                              target;
    InstallManagedPackageLayout         layout { InstallManagedPackageLayout::DirectBin };
    Vec<InstallOwnedEntry>              entries;
    Vec<InstallStoredRuntimeDependency> runtime_dependencies;

    auto clone() const -> InstallPackageInfo {
        auto copied_entries = Vec<InstallOwnedEntry>::with_capacity(entries.len());
        for (const auto& entry : entries) copied_entries.push(entry.clone());
        auto copied_dependencies =
            Vec<InstallStoredRuntimeDependency>::with_capacity(runtime_dependencies.len());
        for (const auto& dependency : runtime_dependencies) {
            copied_dependencies.push(dependency.clone());
        }
        return InstallPackageInfo {
            .identity             = identity.clone(),
            .version              = version.clone(),
            .provenance           = provenance.clone(),
            .profile              = profile.clone(),
            .target               = target.clone(),
            .layout               = layout,
            .entries              = rstd::move(copied_entries),
            .runtime_dependencies = rstd::move(copied_dependencies),
        };
    }
};

struct InstallCatalog {
    Vec<InstallPackageInfo> packages;
};

struct InstallLink {
    PackageTargetId target;
    PathBuf         destination;
    PathBuf         relative_target;
    InstallAction   action { InstallAction::Created };
};

struct InstallPackageRecord {
    String                        name;
    String                        version;
    String                        profile;
    String                        target;
    Vec<InstallBinary>            binaries;
    Vec<InstallEntry>             entries;
    InstallSourceProvenance       provenance;
    Vec<InstallRuntimeDependency> runtime_dependencies;
};

struct InstallStoreRequest {
    InstallDestination        destination;
    Vec<InstallPackageRecord> packages;
    bool                      force { false };
};

struct InstallStoreSummary {
    InstallDestination    destination;
    Option<InstallLayout> managed_layout;
    Vec<String>           packages;
    Vec<InstallBinary>    binaries;
    Vec<InstallEntry>     entries;
    Vec<InstallLink>      links;
};

struct InstallRequest {
    ResolvedInstallSource source;
    BuildRequest          build;
    InstallDestination    destination;
    Vec<String>           binaries;
    bool                  force { false };
};

struct InstallSummary {
    BuildSummary       build;
    InstallDestination destination;
    Vec<String>        packages;
    Vec<InstallBinary> binaries;
    Vec<InstallEntry>  entries;
    Vec<InstallLink>   links;
};

} // namespace lito

export namespace rstd
{

template<>
struct Impl<convert::From<lito::WorkspaceError>, lito::InstallSourceError> {
    static auto from(lito::WorkspaceError error) -> lito::InstallSourceError {
        return lito::InstallSourceError::Workspace(rstd::move(error));
    }
};

template<>
struct Impl<fmt::Display, lito::InstallSourceError> : ImplBase<lito::InstallSourceError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        const auto& error = this->self();
        if (error.is_Workspace()) {
            return as<fmt::Display>(error.as_Workspace().source).fmt(formatter);
        }
        return formatter.write_str(error.as_Message().message.as_str());
    }
};

template<>
struct Impl<fmt::Debug, lito::InstallSourceError> : ImplBase<lito::InstallSourceError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, lito::InstallSourceError> : ImplBase<lito::InstallSourceError> {
    auto source() const noexcept -> Option<error::ErrorRef> {
        const auto& error = this->self();
        if (error.is_Workspace()) {
            return Some(dyn<error::Error>::from_ref(error.as_Workspace().source));
        }
        return None();
    }
};

template<>
struct Impl<convert::From<lito::InstallSourceError>, lito::InstallStoreError> {
    static auto from(lito::InstallSourceError error) -> lito::InstallStoreError {
        return lito::InstallStoreError::Cause(lito::InstallStoreCause::Source(rstd::move(error)));
    }
};

template<>
struct Impl<fmt::Display, lito::InstallStoreCause> : ImplBase<lito::InstallStoreCause> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        const auto& error = this->self();
        if (error.is_Source()) {
            return as<fmt::Display>(error.as_Source().source).fmt(formatter);
        }
        if (error.is_Io()) {
            const auto& value = error.as_Io();
            return formatter.write_str(
                rstd::format("cannot {} '{}'", value.operation.as_str(), value.path.as_path())
                    .as_str());
        }
        if (error.is_Json()) {
            return formatter.write_fmt(fmt::Arguments::make("cannot parse install metadata '{}'",
                                                            error.as_Json().path.as_path()));
        }
        return formatter.write_str(error.as_Message().message.as_str());
    }
};

template<>
struct Impl<fmt::Debug, lito::InstallStoreCause> : ImplBase<lito::InstallStoreCause> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, lito::InstallStoreCause> : ImplBase<lito::InstallStoreCause> {
    auto source() const noexcept -> Option<error::ErrorRef> {
        const auto& error = this->self();
        if (error.is_Source()) {
            return Some(dyn<error::Error>::from_ref(error.as_Source().source));
        }
        if (error.is_Io()) {
            return Some(dyn<error::Error>::from_ref(error.as_Io().source));
        }
        if (error.is_Json()) {
            return Some(dyn<error::Error>::from_ref(error.as_Json().source));
        }
        return None();
    }
};

template<>
struct Impl<fmt::Display, lito::InstallStoreError> : ImplBase<lito::InstallStoreError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        const auto& error = this->self();
        if (error.is_Cause()) {
            return as<fmt::Display>(error.as_Cause().source).fmt(formatter);
        }
        const auto& transaction = error.as_Transaction();
        if (transaction.rollback_failures.is_empty()) {
            return formatter.write_str(
                rstd::format("{} failed", transaction.operation.as_str()).as_str());
        }
        auto text = rstd::format("{} failed", transaction.operation.as_str());
        for (const auto& failure : transaction.rollback_failures) {
            text.push_str(rstd::format("; rollback cannot {} '{}': {}",
                                       failure.operation.as_str(),
                                       failure.path.as_path(),
                                       failure.source)
                              .as_str());
        }
        return formatter.write_str(text.as_str());
    }
};

template<>
struct Impl<fmt::Debug, lito::InstallStoreError> : ImplBase<lito::InstallStoreError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, lito::InstallStoreError> : ImplBase<lito::InstallStoreError> {
    auto source() const noexcept -> Option<error::ErrorRef> {
        const auto& error = this->self();
        if (error.is_Cause()) {
            return as<error::Error>(error.as_Cause().source).source();
        }
        return Some(dyn<error::Error>::from_ref(*error.as_Transaction().source));
    }
};

template<>
struct Impl<convert::From<lito::InstallSourceError>, lito::InstallError> {
    static auto from(lito::InstallSourceError error) -> lito::InstallError {
        return lito::InstallError::Source(rstd::move(error));
    }
};

template<>
struct Impl<convert::From<lito::InstallScriptError>, lito::InstallError> {
    static auto from(lito::InstallScriptError error) -> lito::InstallError {
        return lito::InstallError::Script(rstd::move(error));
    }
};

template<>
struct Impl<convert::From<lito::BuildError>, lito::InstallError> {
    static auto from(lito::BuildError error) -> lito::InstallError {
        return lito::InstallError::Build(rstd::move(error));
    }
};

template<>
struct Impl<convert::From<lito::InstallMaterializeError>, lito::InstallError> {
    static auto from(lito::InstallMaterializeError error) -> lito::InstallError {
        return lito::InstallError::Materialize(rstd::move(error));
    }
};

template<>
struct Impl<convert::From<lito::InstallStoreError>, lito::InstallError> {
    static auto from(lito::InstallStoreError error) -> lito::InstallError {
        return lito::InstallError::Store(rstd::move(error));
    }
};

template<>
struct Impl<fmt::Display, lito::InstallError> : ImplBase<lito::InstallError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        const auto& error = this->self();
        if (error.is_Source()) {
            return formatter.write_raw("install source resolution failed",
                                       sizeof("install source resolution failed") - 1);
        }
        if (error.is_Selection()) {
            return formatter.write_raw("install package selection failed",
                                       sizeof("install package selection failed") - 1);
        }
        if (error.is_Script()) {
            return formatter.write_raw("install script failed",
                                       sizeof("install script failed") - 1);
        }
        if (error.is_Build()) {
            return formatter.write_raw("install build failed", sizeof("install build failed") - 1);
        }
        if (error.is_Materialize()) {
            return formatter.write_raw("install plan materialization failed",
                                       sizeof("install plan materialization failed") - 1);
        }
        if (error.is_Store()) {
            return formatter.write_raw("install store update failed",
                                       sizeof("install store update failed") - 1);
        }
        return formatter.write_str(error.as_Message().message.as_str());
    }
};

template<>
struct Impl<fmt::Debug, lito::InstallError> : ImplBase<lito::InstallError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, lito::InstallError> : ImplBase<lito::InstallError> {
    auto source() const noexcept -> Option<error::ErrorRef> {
        const auto& error = this->self();
        if (error.is_Source()) {
            return Some(dyn<error::Error>::from_ref(error.as_Source().source));
        }
        if (error.is_Selection()) {
            return Some(dyn<error::Error>::from_ref(error.as_Selection().source));
        }
        if (error.is_Script()) {
            return Some(dyn<error::Error>::from_ref(error.as_Script().source));
        }
        if (error.is_Build()) {
            return Some(dyn<error::Error>::from_ref(error.as_Build().source));
        }
        if (error.is_Materialize()) {
            return Some(dyn<error::Error>::from_ref(error.as_Materialize().source));
        }
        if (error.is_Store()) {
            return Some(dyn<error::Error>::from_ref(error.as_Store().source));
        }
        return None();
    }
};

} // namespace rstd
