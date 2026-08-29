module;
#include <rstd/enum.hpp>

export module lito.driver:build.artifact_processor_error;

import rstd;
import rstd.json;
import lito.core;
import lito.system;

using namespace rstd::prelude;

export namespace lito
{

class ArtifactProcessorError {
    RSTD_ENUM(ArtifactProcessorError,
              (System, (lito::system::SystemError source;)),
              (Json, (PathBuf path; rstd::json::Error source;)),
              (Invalid, (String message;)),
              (Execution, (PathBuf executable; i32 exit_code;)))
};

template<typename T>
using ArtifactProcessorResult = Result<T, ArtifactProcessorError>;

} // namespace lito

export namespace rstd
{

template<>
struct Impl<convert::From<lito::system::SystemError>, lito::ArtifactProcessorError> {
    static auto from(lito::system::SystemError error) -> lito::ArtifactProcessorError {
        return lito::ArtifactProcessorError::System(rstd::move(error));
    }
};

template<>
struct Impl<fmt::Display, lito::ArtifactProcessorError> : ImplBase<lito::ArtifactProcessorError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        const auto& error = this->self();
        if (error.is_System()) {
            return formatter.write_raw("artifact processor system operation failed",
                                       sizeof("artifact processor system operation failed") - 1);
        }
        if (error.is_Json()) {
            return formatter.write_fmt(fmt::Arguments::make(
                "cannot parse artifact processor response '{}'", error.as_Json().path.as_path()));
        }
        if (error.is_Invalid()) return formatter.write_str(error.as_Invalid().message.as_str());
        const auto& execution = error.as_Execution();
        return formatter.write_fmt(fmt::Arguments::make("artifact processor '{}' exited with {}",
                                                        execution.executable.as_path(),
                                                        execution.exit_code));
    }
};

template<>
struct Impl<fmt::Debug, lito::ArtifactProcessorError> : ImplBase<lito::ArtifactProcessorError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, lito::ArtifactProcessorError> : ImplBase<lito::ArtifactProcessorError> {
    auto source() const noexcept -> Option<error::ErrorRef> {
        const auto& error = this->self();
        if (error.is_System()) {
            return Some(dyn<error::Error>::from_ref(error.as_System().source));
        }
        if (error.is_Json()) return Some(dyn<error::Error>::from_ref(error.as_Json().source));
        return None();
    }
};

} // namespace rstd
