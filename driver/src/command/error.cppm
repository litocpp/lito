module;
#include <rstd/enum.hpp>

export module lito.driver:command.error;

import rstd;
import lito.core;
import :project.error;
import :build.error;
import lito.system;
import lito.tools;
import lito.tools.cargo;
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
              (Lock, (lito::lock::LockError source;)),
              (Flatpak, (lito::flatpak::Error source;)),
              (CargoFlatpak, (lito::tools::cargo::FlatpakExportError source;)),
              (Source, (lito::source::SourceError source;)),
              (Package, (lito::package::PackageError source;)),
              (Build, (BuildError source;)),
              (System, (SystemError source;)),
              (Tools, (lito::tools::ToolError source;)),
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
struct Impl<convert::From<lito::lock::LockError>, lito::CommandError> {
    static auto from(lito::lock::LockError error) -> lito::CommandError {
        return lito::CommandError::Lock(rstd::move(error));
    }
};

template<>
struct Impl<convert::From<lito::flatpak::Error>, lito::CommandError> {
    static auto from(lito::flatpak::Error error) -> lito::CommandError {
        return lito::CommandError::Flatpak(rstd::move(error));
    }
};

template<>
struct Impl<convert::From<lito::tools::cargo::FlatpakExportError>, lito::CommandError> {
    static auto from(lito::tools::cargo::FlatpakExportError error) -> lito::CommandError {
        return lito::CommandError::CargoFlatpak(rstd::move(error));
    }
};

template<>
struct Impl<convert::From<lito::source::SourceError>, lito::CommandError> {
    static auto from(lito::source::SourceError error) -> lito::CommandError {
        return lito::CommandError::Source(rstd::move(error));
    }
};

template<>
struct Impl<convert::From<lito::ProjectError>, lito::CommandError> {
    static auto from(lito::ProjectError error) -> lito::CommandError {
        return lito::CommandError::Project(rstd::move(error));
    }
};

template<>
struct Impl<convert::From<lito::package::PackageError>, lito::CommandError> {
    static auto from(lito::package::PackageError error) -> lito::CommandError {
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
struct Impl<convert::From<lito::tools::ToolError>, lito::CommandError> {
    static auto from(lito::tools::ToolError error) -> lito::CommandError {
        return lito::CommandError::Tools(rstd::move(error));
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
            return formatter.write_raw("project preparation failed",
                                       sizeof("project preparation failed") - 1);
        }
        if (error.is_Lock()) {
            return formatter.write_raw("lock export failed", sizeof("lock export failed") - 1);
        }
        if (error.is_Flatpak()) {
            return formatter.write_raw("Flatpak source export failed",
                                       sizeof("Flatpak source export failed") - 1);
        }
        if (error.is_CargoFlatpak()) {
            return formatter.write_raw("Cargo lock attachment failed",
                                       sizeof("Cargo lock attachment failed") - 1);
        }
        if (error.is_Source()) {
            return formatter.write_raw("lock export source acquisition failed",
                                       sizeof("lock export source acquisition failed") - 1);
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
        if (error.is_Tools()) {
            return formatter.write_raw("command host tool operation failed",
                                       sizeof("command host tool operation failed") - 1);
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
        if (error.is_Lock()) {
            return Some(dyn<error::Error>::from_ref(error.as_Lock().source));
        }
        if (error.is_Flatpak()) {
            return Some(dyn<error::Error>::from_ref(error.as_Flatpak().source));
        }
        if (error.is_CargoFlatpak()) {
            return Some(dyn<error::Error>::from_ref(error.as_CargoFlatpak().source));
        }
        if (error.is_Source()) {
            return Some(dyn<error::Error>::from_ref(error.as_Source().source));
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
        if (error.is_Tools()) {
            return Some(dyn<error::Error>::from_ref(error.as_Tools().source));
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
