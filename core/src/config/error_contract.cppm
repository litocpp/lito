module;
#include <rstd/enum.hpp>

export module lito.config.error_contract;

import rstd;
import rstd.toml;

export namespace lito
{

class ConfigError {
    RSTD_ENUM(ConfigError,
              (Schema, (rstd::string::String message;)),
              (Io,
               (rstd::string::String operation; rstd::path::PathBuf path;
                rstd::io::error::Error source;)),
              (Parse, (rstd::path::PathBuf path; rstd::toml::Error source;)))
};

template<typename T>
using ConfigResult = rstd::Result<T, ConfigError>;

}

export namespace rstd
{

template<>
struct Impl<fmt::Display, lito::ConfigError> : ImplBase<lito::ConfigError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        const auto& error = this->self();
        if (error.is_Schema()) return formatter.write_str(error.as_Schema().message.as_str());
        if (error.is_Io()) {
            const auto& value = error.as_Io();
            return formatter.write_fmt(
                fmt::Arguments::make("cannot {} '{}'", value.operation, value.path.as_path()));
        }
        return formatter.write_fmt(fmt::Arguments::make(
            "cannot parse configuration '{}'", error.as_Parse().path.as_path()));
    }
};

template<>
struct Impl<fmt::Debug, lito::ConfigError> : ImplBase<lito::ConfigError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, lito::ConfigError> : ImplBase<lito::ConfigError> {
    auto source() const noexcept -> Option<error::ErrorRef> {
        const auto& error = this->self();
        if (error.is_Io()) return Some(dyn<error::Error>::from_ref(error.as_Io().source));
        if (error.is_Parse()) return Some(dyn<error::Error>::from_ref(error.as_Parse().source));
        return None();
    }
};

}
