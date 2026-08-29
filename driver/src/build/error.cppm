module;
#include <rstd/enum.hpp>

export module lito.driver:build.error;

import rstd;
import lito.tools;
import lito.core;
import :project.error;
import lito.toolchain.common;
import lito.system;
import :cache.error;
import lito.cpp;
import :build.layout_error;
import :build.script_error;
import :build.product_error;
import :build.artifact_processor_error;

using namespace rstd::prelude;

using namespace lito::system;

export namespace lito
{

class BuildError {
    RSTD_ENUM(BuildError,
              (Project, (ProjectError source;)),
              (Package, (lito::package::PackageError source;)),
              (Toolchain, (ToolchainError source;)),
              (Tools, (lito::tools::ToolError source;)),
              (System, (SystemError source;)),
              (Cache, (CacheError source;)),
              (Module, (cpp::ModuleError source;)),
              (StandardLibrary, (cpp::StandardLibraryError source;)),
              (Discovery, (cpp::SourceDiscoveryError source;)),
              (Layout, (BuildLayoutError source;)),
              (Script, (BuildScriptError source;)),
              (Product, (BuildProductError source;)),
              (ArtifactProcessor, (ArtifactProcessorError source;)),
              (Message, (String message;)))
};

template<typename T>
using BuildResult = Result<T, BuildError>;

} // namespace lito

export namespace rstd
{

template<>
struct Impl<convert::From<lito::ProjectError>, lito::BuildError> {
    static auto from(lito::ProjectError error) -> lito::BuildError {
        return lito::BuildError::Project(rstd::move(error));
    }
};

template<>
struct Impl<convert::From<lito::package::PackageError>, lito::BuildError> {
    static auto from(lito::package::PackageError error) -> lito::BuildError {
        return lito::BuildError::Package(rstd::move(error));
    }
};

template<>
struct Impl<convert::From<lito::ToolchainError>, lito::BuildError> {
    static auto from(lito::ToolchainError error) -> lito::BuildError {
        return lito::BuildError::Toolchain(rstd::move(error));
    }
};

template<>
struct Impl<convert::From<lito::tools::ToolError>, lito::BuildError> {
    static auto from(lito::tools::ToolError error) -> lito::BuildError {
        return lito::BuildError::Tools(rstd::move(error));
    }
};

template<>
struct Impl<convert::From<lito::system::SystemError>, lito::BuildError> {
    static auto from(lito::system::SystemError error) -> lito::BuildError {
        return lito::BuildError::System(rstd::move(error));
    }
};

template<>
struct Impl<convert::From<lito::CacheError>, lito::BuildError> {
    static auto from(lito::CacheError error) -> lito::BuildError {
        return lito::BuildError::Cache(rstd::move(error));
    }
};

template<>
struct Impl<convert::From<lito::cpp::ModuleError>, lito::BuildError> {
    static auto from(lito::cpp::ModuleError error) -> lito::BuildError {
        return lito::BuildError::Module(rstd::move(error));
    }
};

template<>
struct Impl<convert::From<lito::cpp::StandardLibraryError>, lito::BuildError> {
    static auto from(lito::cpp::StandardLibraryError error) -> lito::BuildError {
        return lito::BuildError::StandardLibrary(rstd::move(error));
    }
};

template<>
struct Impl<convert::From<lito::cpp::SourceDiscoveryError>, lito::BuildError> {
    static auto from(lito::cpp::SourceDiscoveryError error) -> lito::BuildError {
        return lito::BuildError::Discovery(rstd::move(error));
    }
};

template<>
struct Impl<convert::From<lito::BuildLayoutError>, lito::BuildError> {
    static auto from(lito::BuildLayoutError error) -> lito::BuildError {
        return lito::BuildError::Layout(rstd::move(error));
    }
};

template<>
struct Impl<convert::From<lito::BuildScriptError>, lito::BuildError> {
    static auto from(lito::BuildScriptError error) -> lito::BuildError {
        return lito::BuildError::Script(rstd::move(error));
    }
};

template<>
struct Impl<convert::From<lito::BuildProductError>, lito::BuildError> {
    static auto from(lito::BuildProductError error) -> lito::BuildError {
        return lito::BuildError::Product(rstd::move(error));
    }
};

template<>
struct Impl<convert::From<lito::ArtifactProcessorError>, lito::BuildError> {
    static auto from(lito::ArtifactProcessorError error) -> lito::BuildError {
        return lito::BuildError::ArtifactProcessor(rstd::move(error));
    }
};

template<>
struct Impl<fmt::Display, lito::BuildError> : ImplBase<lito::BuildError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        const auto& error = this->self();
        if (error.is_Project()) {
            return formatter.write_raw("build project preparation failed",
                                       sizeof("build project preparation failed") - 1);
        }
        if (error.is_Package()) {
            return formatter.write_raw("build package planning failed",
                                       sizeof("build package planning failed") - 1);
        }
        if (error.is_Toolchain()) {
            return formatter.write_raw("build toolchain operation failed",
                                       sizeof("build toolchain operation failed") - 1);
        }
        if (error.is_Tools()) {
            return formatter.write_raw("build host tool operation failed",
                                       sizeof("build host tool operation failed") - 1);
        }
        if (error.is_System()) {
            return formatter.write_raw("build system operation failed",
                                       sizeof("build system operation failed") - 1);
        }
        if (error.is_Cache()) {
            return formatter.write_raw("build cache operation failed",
                                       sizeof("build cache operation failed") - 1);
        }
        if (error.is_Module()) {
            return formatter.write_raw("build module resolution failed",
                                       sizeof("build module resolution failed") - 1);
        }
        if (error.is_StandardLibrary()) {
            return formatter.write_raw("build standard library module resolution failed",
                                       sizeof("build standard library module resolution failed") -
                                           1);
        }
        if (error.is_Discovery()) {
            return formatter.write_raw("build source discovery failed",
                                       sizeof("build source discovery failed") - 1);
        }
        if (error.is_Layout()) {
            return formatter.write_raw("build layout preparation failed",
                                       sizeof("build layout preparation failed") - 1);
        }
        if (error.is_Script()) {
            return formatter.write_raw("build script execution failed",
                                       sizeof("build script execution failed") - 1);
        }
        if (error.is_Product()) {
            return formatter.write_raw("build product publication failed",
                                       sizeof("build product publication failed") - 1);
        }
        if (error.is_ArtifactProcessor()) {
            return formatter.write_raw("build artifact processing failed",
                                       sizeof("build artifact processing failed") - 1);
        }
        return formatter.write_str(error.as_Message().message.as_str());
    }
};

template<>
struct Impl<fmt::Debug, lito::BuildError> : ImplBase<lito::BuildError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, lito::BuildError> : ImplBase<lito::BuildError> {
    auto source() const noexcept -> Option<error::ErrorRef> {
        const auto& error = this->self();
        if (error.is_Project()) {
            return Some(dyn<error::Error>::from_ref(error.as_Project().source));
        }
        if (error.is_Package()) {
            return Some(dyn<error::Error>::from_ref(error.as_Package().source));
        }
        if (error.is_Toolchain()) {
            return Some(dyn<error::Error>::from_ref(error.as_Toolchain().source));
        }
        if (error.is_Tools()) {
            return Some(dyn<error::Error>::from_ref(error.as_Tools().source));
        }
        if (error.is_System()) {
            return Some(dyn<error::Error>::from_ref(error.as_System().source));
        }
        if (error.is_Cache()) {
            return Some(dyn<error::Error>::from_ref(error.as_Cache().source));
        }
        if (error.is_Module()) {
            return Some(dyn<error::Error>::from_ref(error.as_Module().source));
        }
        if (error.is_StandardLibrary()) {
            return Some(dyn<error::Error>::from_ref(error.as_StandardLibrary().source));
        }
        if (error.is_Discovery()) {
            return Some(dyn<error::Error>::from_ref(error.as_Discovery().source));
        }
        if (error.is_Layout()) {
            return Some(dyn<error::Error>::from_ref(error.as_Layout().source));
        }
        if (error.is_Script()) {
            return Some(dyn<error::Error>::from_ref(error.as_Script().source));
        }
        if (error.is_Product()) {
            return Some(dyn<error::Error>::from_ref(error.as_Product().source));
        }
        if (error.is_ArtifactProcessor()) {
            return Some(dyn<error::Error>::from_ref(error.as_ArtifactProcessor().source));
        }
        return None();
    }
};

} // namespace rstd
