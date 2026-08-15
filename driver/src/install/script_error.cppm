module;
#include <rstd/enum.hpp>

export module lito.driver:install.script_error;

import rstd;
import luato;
import lito.core;
import :build.script_error;

using namespace rstd::prelude;

export namespace lito
{

class InstallScriptError {
    RSTD_ENUM(InstallScriptError,
              (Io, (String operation; PathBuf path; rstd::io::error::Error source;)),
              (Lua, (PathBuf path; luato::Error source;)),
              (Binding, (PathBuf path; luato::Error source;)),
              (Template, (PathBuf path; TemplateError source;)),
              (Message, (String message;)))
};

template<typename T>
using InstallScriptResult = Result<T, InstallScriptError>;

} // namespace lito

export namespace rstd
{

template<>
struct Impl<fmt::Display, lito::InstallScriptError> : ImplBase<lito::InstallScriptError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        const auto& error = this->self();
        if (error.is_Io()) {
            return formatter.write_str(rstd::format("cannot {} '{}'",
                                                    error.as_Io().operation.as_str(),
                                                    error.as_Io().path.as_path())
                                           .as_str());
        }
        if (error.is_Lua()) {
            return formatter.write_str(
                rstd::format("install script '{}' failed", error.as_Lua().path.as_path()).as_str());
        }
        if (error.is_Binding()) {
            return formatter.write_str(rstd::format("install script '{}' has an invalid recipe",
                                                    error.as_Binding().path.as_path())
                                           .as_str());
        }
        if (error.is_Template()) {
            return formatter.write_str(rstd::format("cannot render install template '{}'",
                                                    error.as_Template().path.as_path())
                                           .as_str());
        }
        return formatter.write_str(error.as_Message().message.as_str());
    }
};

template<>
struct Impl<fmt::Debug, lito::InstallScriptError> : ImplBase<lito::InstallScriptError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, lito::InstallScriptError> : ImplBase<lito::InstallScriptError> {
    auto source() const noexcept -> Option<error::ErrorRef> {
        const auto& error = this->self();
        if (error.is_Io()) return Some(dyn<error::Error>::from_ref(error.as_Io().source));
        if (error.is_Lua()) return Some(dyn<error::Error>::from_ref(error.as_Lua().source));
        if (error.is_Binding()) {
            return Some(dyn<error::Error>::from_ref(error.as_Binding().source));
        }
        if (error.is_Template()) {
            return Some(dyn<error::Error>::from_ref(error.as_Template().source));
        }
        return None();
    }
};

} // namespace rstd
