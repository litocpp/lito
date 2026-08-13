module;
#include <rstd/enum.hpp>

export module lito.build.script_error_contract;

import rstd;
import rstd.json;
import luato;
import lito.error;
import lito.build.layout_error_contract;

export namespace lito
{

class BuildScriptError {
    RSTD_ENUM(BuildScriptError,
              (Layout, (BuildLayoutError source;)),
              (Io,
               (String operation; PathBuf path; rstd::io::error::Error source;)),
              (Json, (PathBuf path; rstd::json::Error source;)),
              (Lua,
               (String operation; rstd::Option<PathBuf> path; luato::Error source;)),
              (Message, (String message;)))
};

template<typename T>
using BuildScriptResult = rstd::Result<T, BuildScriptError>;

} // namespace lito

export namespace rstd
{

template<>
struct Impl<fmt::Display, luato::Error> : ImplBase<luato::Error> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        const auto& error = this->self();
        if (error.traceback.is_empty()) return formatter.write_str(error.message.as_str());
        return formatter.write_str(
            rstd::format("{}\n{}", error.message.as_str(), error.traceback.as_str()).as_str());
    }
};

template<>
struct Impl<fmt::Debug, luato::Error> : ImplBase<luato::Error> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, luato::Error> : DefaultInImpl<error::Error, luato::Error> {};

template<>
struct Impl<convert::From<lito::BuildLayoutError>, lito::BuildScriptError> {
    static auto from(lito::BuildLayoutError error) -> lito::BuildScriptError {
        return lito::BuildScriptError::Layout(rstd::move(error));
    }
};

template<>
struct Impl<fmt::Display, lito::BuildScriptError> : ImplBase<lito::BuildScriptError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        const auto& error = this->self();
        if (error.is_Layout()) {
            return formatter.write_raw("build script layout operation failed",
                                       sizeof("build script layout operation failed") - 1);
        }
        if (error.is_Io()) {
            const auto& value = error.as_Io();
            return formatter.write_str(
                rstd::format("cannot {} '{}'", value.operation.as_str(), value.path.as_path())
                    .as_str());
        }
        if (error.is_Json()) {
            return formatter.write_str(
                rstd::format("cannot parse configure receipt '{}'", error.as_Json().path.as_path())
                    .as_str());
        }
        if (error.is_Lua()) {
            const auto& value = error.as_Lua();
            if (value.path.is_some()) {
                return formatter.write_str(
                    rstd::format("cannot {} '{}'", value.operation.as_str(), value.path->as_path())
                        .as_str());
            }
            return formatter.write_str(
                rstd::format("cannot {}", value.operation.as_str()).as_str());
        }
        return formatter.write_str(error.as_Message().message.as_str());
    }
};

template<>
struct Impl<fmt::Debug, lito::BuildScriptError> : ImplBase<lito::BuildScriptError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, lito::BuildScriptError> : ImplBase<lito::BuildScriptError> {
    auto source() const noexcept -> Option<error::ErrorRef> {
        const auto& error = this->self();
        if (error.is_Layout()) {
            return Some(dyn<error::Error>::from_ref(error.as_Layout().source));
        }
        if (error.is_Io()) return Some(dyn<error::Error>::from_ref(error.as_Io().source));
        if (error.is_Json()) return Some(dyn<error::Error>::from_ref(error.as_Json().source));
        if (error.is_Lua()) return Some(dyn<error::Error>::from_ref(error.as_Lua().source));
        return None();
    }
};

} // namespace rstd
