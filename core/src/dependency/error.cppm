module;
#include <rstd/enum.hpp>

export module lito.core:dependency.error;

import rstd;
import rstd.json;
import :source.error;
import lito.system;

using namespace rstd::prelude;
using PathBuf  = rstd::path::PathBuf;
using ErrorBox = Box<dyn<rstd::error::Error>>;
using namespace lito::system;

export namespace lito
{

class DependencyError {
    RSTD_ENUM(DependencyError,
              (Source, (SourceError source;)),
              (System, (SystemError source;)),
              (Operation, (String operation; SystemError source;)),
              (Io, (String operation; PathBuf path; rstd::io::error::Error source;)),
              (Json, (String context; PathBuf path; rstd::json::Error source;)),
              (Configuration, (String dependency; ErrorBox source;)),
              (CMakeOperation,
               (String dependency; String operation; PathBuf work_area;
                Box<DependencyError>                         source;)),
              (Message, (String message;)))
};

template<typename T>
using DependencyResult = Result<T, DependencyError>;

template<typename T>
auto dependency_failure(String message) -> DependencyResult<T> {
    return Err(DependencyError::Message(rstd::move(message)));
}

template<typename T>
auto dependency_failure(ref<str> message) -> DependencyResult<T> {
    return Err(DependencyError::Message(String::make(message)));
}

} // namespace lito

export namespace rstd
{

template<>
struct Impl<convert::From<lito::SourceError>, lito::DependencyError> {
    static auto from(lito::SourceError error) -> lito::DependencyError {
        return lito::DependencyError::Source(rstd::move(error));
    }
};

template<>
struct Impl<convert::From<lito::system::SystemError>, lito::DependencyError> {
    static auto from(lito::system::SystemError error) -> lito::DependencyError {
        return lito::DependencyError::System(rstd::move(error));
    }
};

template<>
struct Impl<fmt::Display, lito::DependencyError> : ImplBase<lito::DependencyError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        const auto& error = this->self();
        if (error.is_Source()) return as<fmt::Display>(error.as_Source().source).fmt(formatter);
        if (error.is_System()) {
            return formatter.write_raw("dependency system operation failed",
                                       sizeof("dependency system operation failed") - 1);
        }
        if (error.is_Operation()) {
            return formatter.write_fmt(
                fmt::Arguments::make("{} could not execute", error.as_Operation().operation));
        }
        if (error.is_Io()) {
            const auto& value = error.as_Io();
            return formatter.write_fmt(fmt::Arguments::make(
                "cannot {} dependency path '{}'", value.operation, value.path.as_path()));
        }
        if (error.is_Json()) {
            const auto& value = error.as_Json();
            return formatter.write_fmt(
                fmt::Arguments::make("cannot parse {} '{}'", value.context, value.path.as_path()));
        }
        if (error.is_Configuration()) {
            return formatter.write_fmt(
                fmt::Arguments::make("dependency '{}' build configuration is invalid",
                                     error.as_Configuration().dependency));
        }
        if (error.is_CMakeOperation()) {
            const auto& value = error.as_CMakeOperation();
            return formatter.write_fmt(
                fmt::Arguments::make("CMake dependency '{}' {} failed in '{}'",
                                     value.dependency,
                                     value.operation,
                                     value.work_area.as_path()));
        }
        return formatter.write_str(error.as_Message().message.as_str());
    }
};

template<>
struct Impl<fmt::Debug, lito::DependencyError> : ImplBase<lito::DependencyError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, lito::DependencyError> : ImplBase<lito::DependencyError> {
    auto source() const noexcept -> Option<error::ErrorRef> {
        const auto& error = this->self();
        if (error.is_Source()) {
            return Some(dyn<error::Error>::from_ref(error.as_Source().source));
        }
        if (error.is_System()) {
            return Some(dyn<error::Error>::from_ref(error.as_System().source));
        }
        if (error.is_Operation()) {
            return Some(dyn<error::Error>::from_ref(error.as_Operation().source));
        }
        if (error.is_Io()) return Some(dyn<error::Error>::from_ref(error.as_Io().source));
        if (error.is_Json()) return Some(dyn<error::Error>::from_ref(error.as_Json().source));
        if (error.is_Configuration()) {
            return Some(error.as_Configuration().source.as_ref());
        }
        if (error.is_CMakeOperation()) {
            return Some(dyn<error::Error>::from_ref(*error.as_CMakeOperation().source));
        }
        return None();
    }
};

} // namespace rstd
