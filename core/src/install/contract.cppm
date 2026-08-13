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
import lito.workspace;

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
              (Io,
               (String operation; PathBuf path; rstd::io::error::Error source;)),
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
               (String operation;
                rstd::boxed::Box<InstallStoreError> source;
                Vec<InstallRollbackFailure> rollback_failures;)))
};

template<typename T>
using InstallStoreResult = rstd::Result<T, InstallStoreError>;

class InstallError {
    RSTD_ENUM(InstallError,
              (Source, (InstallSourceError source;)),
              (Build, (BuildError source;)),
              (Store, (InstallStoreError source;)),
              (Message, (String message;)))
};

template<typename T>
using InstallResult = rstd::Result<T, InstallError>;

class InstallSourceRequirement {
    RSTD_ENUM(InstallSourceRequirement, (LocalProject, (PathBuf requested_root;)))
};

class InstallSourceProvenance {
    RSTD_ENUM(InstallSourceProvenance, (Local, (PathBuf root;)))

public:
    auto clone() const -> InstallSourceProvenance {
        return InstallSourceProvenance::Local(as_Local().root.clone());
    }
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

struct InstallLayout {
    InstallRoot root;
    PathBuf     bin_directory;
    PathBuf     state_directory;
    PathBuf     metadata;
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

struct InstallPackageRecord {
    String             name;
    String             version;
    String             profile;
    String             target;
    Vec<InstallBinary> binaries;
};

struct InstallStoreRequest {
    InstallRoot               root;
    InstallSourceProvenance   provenance;
    Vec<InstallPackageRecord> packages;
    bool                      force { false };
};

struct InstallStoreSummary {
    InstallLayout      layout;
    Vec<String>        packages;
    Vec<InstallBinary> binaries;
};

struct InstallRequest {
    ResolvedInstallSource source;
    BuildRequest          build;
    InstallRoot           root;
    Vec<String>           binaries;
    bool                  force { false };
};

struct InstallSummary {
    BuildSummary       build;
    InstallRoot        root;
    Vec<String>        packages;
    Vec<InstallBinary> binaries;
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
        return lito::InstallStoreError::Cause(
            lito::InstallStoreCause::Source(rstd::move(error)));
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
            return formatter.write_fmt(fmt::Arguments::make(
                "cannot parse install metadata '{}'", error.as_Json().path.as_path()));
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
            return Some(dyn<error::Error>::from_ref(error.as_Cause().source));
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
struct Impl<convert::From<lito::BuildError>, lito::InstallError> {
    static auto from(lito::BuildError error) -> lito::InstallError {
        return lito::InstallError::Build(rstd::move(error));
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
        if (error.is_Build()) {
            return formatter.write_raw("install build failed", sizeof("install build failed") - 1);
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
        if (error.is_Build()) {
            return Some(dyn<error::Error>::from_ref(error.as_Build().source));
        }
        if (error.is_Store()) {
            return Some(dyn<error::Error>::from_ref(error.as_Store().source));
        }
        return None();
    }
};

} // namespace rstd
