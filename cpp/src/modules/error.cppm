module;
#include <rstd/enum.hpp>

export module lito.cpp:modules.error;

import rstd;

using namespace rstd::prelude;

export namespace lito::cpp
{

class ModuleError {
    RSTD_ENUM(ModuleError,
              (Convention, (String message;)),
              (Graph, (String message;)),
              (Io, (String operation; rstd::path::PathBuf path; rstd::io::error::Error source;)))
};

template<typename T>
using ModuleResult = Result<T, ModuleError>;

} // namespace lito::cpp

export namespace rstd
{

template<>
struct Impl<fmt::Display, lito::cpp::ModuleError> : ImplBase<lito::cpp::ModuleError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        const auto& error = this->self();
        if (error.is_Convention()) {
            return formatter.write_str(error.as_Convention().message.as_str());
        }
        if (error.is_Graph()) return formatter.write_str(error.as_Graph().message.as_str());
        const auto& value = error.as_Io();
        return formatter.write_fmt(fmt::Arguments::make(
            "cannot {} module path '{}'", value.operation, value.path.as_path()));
    }
};

template<>
struct Impl<fmt::Debug, lito::cpp::ModuleError> : ImplBase<lito::cpp::ModuleError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, lito::cpp::ModuleError> : ImplBase<lito::cpp::ModuleError> {
    auto source() const noexcept -> Option<error::ErrorRef> {
        const auto& error = this->self();
        if (! error.is_Io()) return None();
        return Some(dyn<error::Error>::from_ref(error.as_Io().source));
    }
};

} // namespace rstd
