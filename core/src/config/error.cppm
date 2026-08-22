module;
#include <rstd/enum.hpp>

export module lito.core:config.error;

import rstd;
import rstd.serde;
import rstd.toml;
import lito.system;
import :parse.error;

using namespace rstd::prelude;

export namespace lito::config
{

class ConfigError {
    RSTD_ENUM(ConfigError,
              (Schema, (String message;)),
              (Value, (lito::parse::Error source;)),
              (Data, (rstd::serde::Error source;)),
              (EnvironmentFlags, (String variable; lito::system::SystemError source;)),
              (Input, (String context; rstd::toml::Error source;)),
              (MissingKey, (String key;)),
              (KeyConflict, (String key;)),
              (Io, (String operation; rstd::path::PathBuf path; rstd::io::error::Error source;)),
              (Parse, (rstd::path::PathBuf path; rstd::toml::Error source;)),
              (Serialize, (rstd::path::PathBuf path; rstd::toml::SerializeError source;)))
};

template<typename T>
using ConfigResult = Result<T, ConfigError>;

} // namespace lito::config

export namespace rstd
{

template<>
struct Impl<convert::From<lito::parse::Error>, lito::config::ConfigError> {
    static auto from(lito::parse::Error error) -> lito::config::ConfigError {
        return lito::config::ConfigError::Value(rstd::move(error));
    }
};

template<>
struct Impl<fmt::Display, lito::config::ConfigError> : ImplBase<lito::config::ConfigError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        const auto& error = this->self();
        if (error.is_Schema()) return formatter.write_str(error.as_Schema().message.as_str());
        if (error.is_Value()) return as<fmt::Display>(error.as_Value().source).fmt(formatter);
        if (error.is_Data()) {
            return formatter.write_raw("configuration data is invalid",
                                       sizeof("configuration data is invalid") - 1);
        }
        if (error.is_EnvironmentFlags()) {
            return formatter.write_fmt(fmt::Arguments::make("cannot read compiler flags from {}",
                                                            error.as_EnvironmentFlags().variable));
        }
        if (error.is_Input()) {
            return formatter.write_fmt(
                fmt::Arguments::make("cannot parse {}", error.as_Input().context));
        }
        if (error.is_MissingKey()) {
            return formatter.write_fmt(fmt::Arguments::make("configuration key '{}' does not exist",
                                                            error.as_MissingKey().key));
        }
        if (error.is_KeyConflict()) {
            return formatter.write_fmt(
                fmt::Arguments::make("configuration key '{}' conflicts with a non-table value",
                                     error.as_KeyConflict().key));
        }
        if (error.is_Io()) {
            const auto& value = error.as_Io();
            return formatter.write_fmt(
                fmt::Arguments::make("cannot {} '{}'", value.operation, value.path.as_path()));
        }
        if (error.is_Parse()) {
            return formatter.write_fmt(fmt::Arguments::make("cannot parse configuration '{}'",
                                                            error.as_Parse().path.as_path()));
        }
        return formatter.write_fmt(fmt::Arguments::make("cannot serialize configuration '{}'",
                                                        error.as_Serialize().path.as_path()));
    }
};

template<>
struct Impl<fmt::Debug, lito::config::ConfigError> : ImplBase<lito::config::ConfigError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, lito::config::ConfigError> : ImplBase<lito::config::ConfigError> {
    auto source() const noexcept -> Option<error::ErrorRef> {
        const auto& error = this->self();
        if (error.is_Value()) return Some(dyn<error::Error>::from_ref(error.as_Value().source));
        if (error.is_Data()) return Some(dyn<error::Error>::from_ref(error.as_Data().source));
        if (error.is_EnvironmentFlags()) {
            return Some(dyn<error::Error>::from_ref(error.as_EnvironmentFlags().source));
        }
        if (error.is_Input()) return Some(dyn<error::Error>::from_ref(error.as_Input().source));
        if (error.is_Io()) return Some(dyn<error::Error>::from_ref(error.as_Io().source));
        if (error.is_Parse()) return Some(dyn<error::Error>::from_ref(error.as_Parse().source));
        if (error.is_Serialize()) {
            return Some(dyn<error::Error>::from_ref(error.as_Serialize().source));
        }
        return None();
    }
};

} // namespace rstd
