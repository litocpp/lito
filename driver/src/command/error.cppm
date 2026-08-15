module;
#include <rstd/enum.hpp>

export module lito.driver:command.error;

import rstd;
import lito.core;
import :project.error;
import :build.error;
import lito.system;
import lito.toolchain.common;
import lito.cpp;
import :build.layout_error;

using namespace rstd::prelude;

using namespace lito::system;

export namespace lito
{

class CommandError {
    RSTD_ENUM(CommandError,
              (Project, (ProjectError source;)),
              (Package, (PackageError source;)),
              (Build, (BuildError source;)),
              (System, (SystemError source;)),
              (Toolchain, (ToolchainError source;)),
              (Discovery, (cpp::SourceDiscoveryError source;)),
              (Layout, (BuildLayoutError source;)),
              (Message, (String message;)))
};

template<typename T>
using CommandResult = Result<T, CommandError>;

} // namespace lito

export namespace rstd
{

template<>
struct Impl<convert::From<lito::ProjectError>, lito::CommandError> {
    static auto from(lito::ProjectError error) -> lito::CommandError {
        return lito::CommandError::Project(rstd::move(error));
    }
};

template<>
struct Impl<convert::From<lito::PackageError>, lito::CommandError> {
    static auto from(lito::PackageError error) -> lito::CommandError {
        return lito::CommandError::Package(rstd::move(error));
    }
};

template<>
struct Impl<convert::From<lito::BuildError>, lito::CommandError> {
    static auto from(lito::BuildError error) -> lito::CommandError {
        return lito::CommandError::Build(rstd::move(error));
    }
};

template<>
struct Impl<convert::From<lito::system::SystemError>, lito::CommandError> {
    static auto from(lito::system::SystemError error) -> lito::CommandError {
        return lito::CommandError::System(rstd::move(error));
    }
};

template<>
struct Impl<convert::From<lito::ToolchainError>, lito::CommandError> {
    static auto from(lito::ToolchainError error) -> lito::CommandError {
        return lito::CommandError::Toolchain(rstd::move(error));
    }
};

template<>
struct Impl<convert::From<lito::cpp::SourceDiscoveryError>, lito::CommandError> {
    static auto from(lito::cpp::SourceDiscoveryError error) -> lito::CommandError {
        return lito::CommandError::Discovery(rstd::move(error));
    }
};

template<>
struct Impl<convert::From<lito::BuildLayoutError>, lito::CommandError> {
    static auto from(lito::BuildLayoutError error) -> lito::CommandError {
        return lito::CommandError::Layout(rstd::move(error));
    }
};

template<>
struct Impl<fmt::Display, lito::CommandError> : ImplBase<lito::CommandError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        const auto& error = this->self();
        if (error.is_Project()) {
            return formatter.write_raw("scan project preparation failed",
                                       sizeof("scan project preparation failed") - 1);
        }
        if (error.is_Package()) {
            return formatter.write_raw("command package resolution failed",
                                       sizeof("command package resolution failed") - 1);
        }
        if (error.is_Build()) {
            return formatter.write_raw("command build failed", sizeof("command build failed") - 1);
        }
        if (error.is_System()) {
            return formatter.write_raw("command environment operation failed",
                                       sizeof("command environment operation failed") - 1);
        }
        if (error.is_Toolchain()) {
            return formatter.write_raw("command toolchain operation failed",
                                       sizeof("command toolchain operation failed") - 1);
        }
        if (error.is_Discovery()) {
            return formatter.write_raw("command source discovery failed",
                                       sizeof("command source discovery failed") - 1);
        }
        if (error.is_Layout()) {
            return formatter.write_raw("command build layout operation failed",
                                       sizeof("command build layout operation failed") - 1);
        }
        return formatter.write_str(error.as_Message().message.as_str());
    }
};

template<>
struct Impl<fmt::Debug, lito::CommandError> : ImplBase<lito::CommandError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, lito::CommandError> : ImplBase<lito::CommandError> {
    auto source() const noexcept -> Option<error::ErrorRef> {
        const auto& error = this->self();
        if (error.is_Project()) {
            return Some(dyn<error::Error>::from_ref(error.as_Project().source));
        }
        if (error.is_Package()) {
            return Some(dyn<error::Error>::from_ref(error.as_Package().source));
        }
        if (error.is_Build()) {
            return Some(dyn<error::Error>::from_ref(error.as_Build().source));
        }
        if (error.is_System()) {
            return Some(dyn<error::Error>::from_ref(error.as_System().source));
        }
        if (error.is_Toolchain()) {
            return Some(dyn<error::Error>::from_ref(error.as_Toolchain().source));
        }
        if (error.is_Discovery()) {
            return Some(dyn<error::Error>::from_ref(error.as_Discovery().source));
        }
        if (error.is_Layout()) {
            return Some(dyn<error::Error>::from_ref(error.as_Layout().source));
        }
        return None();
    }
};

} // namespace rstd
