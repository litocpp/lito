module;
#include <rstd/enum.hpp>

export module lito.core:source.error;

import rstd;
import lito.system;
import :acquisition;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;
using namespace rstd::literals;
using namespace lito::system;

export namespace lito::source
{

class SourceError {
    RSTD_ENUM(SourceError,
              (Acquisition, (String operation; lito::acquisition::AcquisitionError source;)),
              (System, (String operation; SystemError source;)),
              (Io, (String operation; PathBuf path; rstd::io::error::Error source;)),
              (Message, (String message;)))
};

template<typename T>
using SourceResult = Result<T, SourceError>;

} // namespace lito::source

using namespace lito::source;

template<typename T>
auto source_failure(String message) -> SourceResult<T> {
    return Err(SourceError::Message(rstd::move(message)));
}

template<typename T>
auto source_failure(ref<str> message) -> SourceResult<T> {
    return Err(SourceError::Message(String::make(message)));
}

template<typename T>
auto source_io_failure(ref<str>               operation,
                       ref<rstd::path::Path>  path,
                       rstd::io::error::Error source) -> SourceResult<T> {
    return Err(SourceError::Io(String::make(operation), PathBuf::from(path), rstd::move(source)));
}

export namespace rstd
{

template<>
struct Impl<convert::From<lito::acquisition::AcquisitionError>, lito::source::SourceError> {
    static auto from(lito::acquisition::AcquisitionError error) -> lito::source::SourceError {
        auto operation = rstd::format("{}", error);
        return lito::source::SourceError::Acquisition(rstd::move(operation), rstd::move(error));
    }
};

template<>
struct Impl<convert::From<lito::system::SystemError>, lito::source::SourceError> {
    static auto from(lito::system::SystemError error) -> lito::source::SourceError {
        return lito::source::SourceError::System(String::make("source operation"_str),
                                                 rstd::move(error));
    }
};

template<>
struct Impl<fmt::Display, lito::source::SourceError> : ImplBase<lito::source::SourceError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        const auto& error = this->self();
        if (error.is_Acquisition()) {
            return formatter.write_str(error.as_Acquisition().operation.as_str());
        }
        if (error.is_System()) return formatter.write_str(error.as_System().operation.as_str());
        if (error.is_Io()) {
            const auto& value = error.as_Io();
            return formatter.write_fmt(fmt::Arguments::make(
                "cannot {} source '{}'", value.operation, value.path.as_path()));
        }
        return formatter.write_str(error.as_Message().message.as_str());
    }
};

template<>
struct Impl<fmt::Debug, lito::source::SourceError> : ImplBase<lito::source::SourceError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, lito::source::SourceError> : ImplBase<lito::source::SourceError> {
    auto source() const noexcept -> Option<error::ErrorRef> {
        const auto& error = this->self();
        if (error.is_Acquisition()) {
            return Some(dyn<error::Error>::from_ref(error.as_Acquisition().source));
        }
        if (error.is_System()) {
            return Some(dyn<error::Error>::from_ref(error.as_System().source));
        }
        if (error.is_Io()) return Some(dyn<error::Error>::from_ref(error.as_Io().source));
        return None();
    }
};

} // namespace rstd
