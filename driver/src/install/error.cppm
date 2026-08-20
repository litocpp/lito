module;
#include <rstd/enum.hpp>

export module lito.driver:install.error;

import rstd;
import rstd.json;
import lito.core;
import :build.error;
import :build.product_error;
import :install.script_error;
import :install.materialize_error;
import lito.toolchain.common;

using namespace rstd::prelude;

export namespace lito
{

class InstallSourceError {
    RSTD_ENUM(InstallSourceError,
              (Workspace, (lito::workspace::WorkspaceError source;)),
              (Message, (String message;)))
};

template<typename T>
using InstallSourceResult = Result<T, InstallSourceError>;

class InstallStoreCause {
    RSTD_ENUM(InstallStoreCause,
              (Source, (InstallSourceError source;)),
              (Io, (String operation; PathBuf path; rstd::io::error::Error source;)),
              (Json, (PathBuf path; rstd::json::Error source;)),
              (Transform, (String package; String entry; String operation; ToolchainError source;)),
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
               (String operation; Box<InstallStoreError> source;
                Vec<InstallRollbackFailure>              rollback_failures;)))
};

template<typename T>
using InstallStoreResult = Result<T, InstallStoreError>;

class InstallError {
    RSTD_ENUM(InstallError,
              (Source, (InstallSourceError source;)),
              (Selection, (lito::workspace::WorkspaceError source;)),
              (Script, (InstallScriptError source;)),
              (Build, (BuildError source;)),
              (Product, (BuildProductError source;)),
              (Materialize, (InstallMaterializeError source;)),
              (Store, (InstallStoreError source;)),
              (Message, (String message;)))
};

template<typename T>
using InstallResult = Result<T, InstallError>;

} // namespace lito

export namespace rstd
{

template<>
struct Impl<convert::From<lito::workspace::WorkspaceError>, lito::InstallSourceError> {
    static auto from(lito::workspace::WorkspaceError error) -> lito::InstallSourceError {
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
        if (error.is_Transform()) {
            const auto& value = error.as_Transform();
            return formatter.write_fmt(
                fmt::Arguments::make("cannot {} install entry '{}' for package '{}'",
                                     value.operation,
                                     value.entry,
                                     value.package));
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
        if (error.is_Io()) return Some(dyn<error::Error>::from_ref(error.as_Io().source));
        if (error.is_Json()) return Some(dyn<error::Error>::from_ref(error.as_Json().source));
        if (error.is_Transform()) {
            return Some(dyn<error::Error>::from_ref(error.as_Transform().source));
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
        if (error.is_Cause()) return as<error::Error>(error.as_Cause().source).source();
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
struct Impl<convert::From<lito::BuildProductError>, lito::InstallError> {
    static auto from(lito::BuildProductError error) -> lito::InstallError {
        return lito::InstallError::Product(rstd::move(error));
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
        if (error.is_Product()) {
            return formatter.write_raw("install build product reuse failed",
                                       sizeof("install build product reuse failed") - 1);
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
        if (error.is_Script()) return Some(dyn<error::Error>::from_ref(error.as_Script().source));
        if (error.is_Build()) return Some(dyn<error::Error>::from_ref(error.as_Build().source));
        if (error.is_Product()) {
            return Some(dyn<error::Error>::from_ref(error.as_Product().source));
        }
        if (error.is_Materialize()) {
            return Some(dyn<error::Error>::from_ref(error.as_Materialize().source));
        }
        if (error.is_Store()) return Some(dyn<error::Error>::from_ref(error.as_Store().source));
        return None();
    }
};

} // namespace rstd
