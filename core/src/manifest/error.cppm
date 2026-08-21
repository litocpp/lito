module;
#include <rstd/enum.hpp>

export module lito.core:manifest.error;

import rstd;
import rstd.toml;
import :manifest.profile;
import :parse.error;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;

export namespace lito::manifest
{

enum class ManifestKind;

class ManifestLocatorError {
    RSTD_ENUM(ManifestLocatorError,
              (Io, (String operation; PathBuf path; rstd::io::error::Error source;)),
              (NotDirectory, (PathBuf path;)),
              (NotRegularFile, (PathBuf path;)),
              (NotFound, (PathBuf directory;)))
};

class ManifestSchemaError {
    RSTD_ENUM(ManifestSchemaError,
              (Domain, (String message;)),
              (Parse, (lito::parse::Error source;)),
              (Io, (String node; String operation; PathBuf path; rstd::io::error::Error source;)),
              (Locate, (ManifestLocatorError source;)),
              (Profile, (BuildProfileError source;)))
};

class ManifestFileCause {
    RSTD_ENUM(ManifestFileCause,
              (Read, (rstd::io::error::Error source;)),
              (Utf8, (alloc::string::FromUtf8Error source;)),
              (Parse, (rstd::toml::Error source;)),
              (Schema, (ManifestSchemaError source;)))
};

struct ManifestFileError {
    PathBuf           path;
    ManifestFileCause cause;
};

class ManifestError {
    RSTD_ENUM(ManifestError,
              (Locate, (ManifestLocatorError source;)),
              (File, (ManifestFileError source;)),
              (Kind, (PathBuf requested_directory; ManifestKind expected; ManifestKind actual;)))
};

template<typename T>
using ManifestLocatorResult = Result<T, ManifestLocatorError>;

template<typename T>
using ManifestSchemaResult = Result<T, ManifestSchemaError>;

template<typename T>
using ManifestResult = Result<T, ManifestError>;

} // namespace lito::manifest

export namespace rstd
{

template<>
struct Impl<convert::From<lito::parse::Error>, lito::manifest::ManifestSchemaError> {
    static auto from(lito::parse::Error error) -> lito::manifest::ManifestSchemaError {
        return lito::manifest::ManifestSchemaError::Parse(rstd::move(error));
    }
};

template<>
struct Impl<convert::From<lito::manifest::ManifestLocatorError>,
            lito::manifest::ManifestSchemaError> {
    static auto from(lito::manifest::ManifestLocatorError error)
        -> lito::manifest::ManifestSchemaError {
        return lito::manifest::ManifestSchemaError::Locate(rstd::move(error));
    }
};

template<>
struct Impl<convert::From<lito::manifest::BuildProfileError>, lito::manifest::ManifestSchemaError> {
    static auto from(lito::manifest::BuildProfileError error)
        -> lito::manifest::ManifestSchemaError {
        return lito::manifest::ManifestSchemaError::Profile(rstd::move(error));
    }
};

template<>
struct Impl<convert::From<lito::manifest::ManifestLocatorError>, lito::manifest::ManifestError> {
    static auto from(lito::manifest::ManifestLocatorError error) -> lito::manifest::ManifestError {
        return lito::manifest::ManifestError::Locate(rstd::move(error));
    }
};

template<>
struct Impl<convert::From<lito::manifest::ManifestFileError>, lito::manifest::ManifestError> {
    static auto from(lito::manifest::ManifestFileError error) -> lito::manifest::ManifestError {
        return lito::manifest::ManifestError::File(rstd::move(error));
    }
};

template<>
struct Impl<fmt::Display, lito::manifest::ManifestLocatorError>
    : ImplBase<lito::manifest::ManifestLocatorError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        const auto& error = this->self();
        if (error.is_Io()) {
            const auto& value = error.as_Io();
            return formatter.write_fmt(
                fmt::Arguments::make("cannot {} '{}'", value.operation, value.path.as_path()));
        }
        if (error.is_NotDirectory()) {
            return formatter.write_fmt(fmt::Arguments::make(
                "manifest root '{}' is not a directory", error.as_NotDirectory().path.as_path()));
        }
        if (error.is_NotRegularFile()) {
            return formatter.write_fmt(fmt::Arguments::make(
                "manifest '{}' is not a regular file", error.as_NotRegularFile().path.as_path()));
        }
        return formatter.write_fmt(
            fmt::Arguments::make("cannot find lito.toml or legacy tenon.toml in '{}'",
                                 error.as_NotFound().directory.as_path()));
    }
};

template<>
struct Impl<fmt::Debug, lito::manifest::ManifestLocatorError>
    : ImplBase<lito::manifest::ManifestLocatorError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, lito::manifest::ManifestLocatorError>
    : ImplBase<lito::manifest::ManifestLocatorError> {
    auto source() const noexcept -> Option<error::ErrorRef> {
        const auto& value = this->self();
        if (! value.is_Io()) return None();
        return Some(dyn<error::Error>::from_ref(value.as_Io().source));
    }
};

template<>
struct Impl<fmt::Display, lito::manifest::ManifestSchemaError>
    : ImplBase<lito::manifest::ManifestSchemaError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        const auto& error = this->self();
        if (error.is_Domain()) return formatter.write_str(error.as_Domain().message.as_str());
        if (error.is_Parse()) return as<fmt::Display>(error.as_Parse().source).fmt(formatter);
        if (error.is_Io()) {
            const auto& value = error.as_Io();
            return formatter.write_fmt(fmt::Arguments::make(
                "cannot {} {} '{}'", value.operation, value.node, value.path.as_path()));
        }
        if (error.is_Profile()) {
            return formatter.write_raw("manifest build profile is invalid",
                                       sizeof("manifest build profile is invalid") - 1);
        }
        return formatter.write_raw("manifest discovery failed",
                                   sizeof("manifest discovery failed") - 1);
    }
};

template<>
struct Impl<fmt::Debug, lito::manifest::ManifestSchemaError>
    : ImplBase<lito::manifest::ManifestSchemaError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, lito::manifest::ManifestSchemaError>
    : ImplBase<lito::manifest::ManifestSchemaError> {
    auto source() const noexcept -> Option<error::ErrorRef> {
        const auto& value = this->self();
        if (value.is_Parse()) return Some(dyn<error::Error>::from_ref(value.as_Parse().source));
        if (value.is_Io()) return Some(dyn<error::Error>::from_ref(value.as_Io().source));
        if (value.is_Locate()) return Some(dyn<error::Error>::from_ref(value.as_Locate().source));
        if (value.is_Profile()) {
            return Some(dyn<error::Error>::from_ref(value.as_Profile().source));
        }
        return None();
    }
};

template<>
struct Impl<fmt::Display, lito::manifest::ManifestFileCause>
    : ImplBase<lito::manifest::ManifestFileCause> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        const auto& value = this->self();
        if (value.is_Read()) {
            return formatter.write_raw("cannot read manifest", sizeof("cannot read manifest") - 1);
        }
        if (value.is_Parse()) {
            return formatter.write_raw("cannot parse manifest",
                                       sizeof("cannot parse manifest") - 1);
        }
        if (value.is_Utf8()) {
            return formatter.write_raw("manifest is not valid UTF-8",
                                       sizeof("manifest is not valid UTF-8") - 1);
        }
        return formatter.write_raw("manifest is invalid", sizeof("manifest is invalid") - 1);
    }
};

template<>
struct Impl<fmt::Debug, lito::manifest::ManifestFileCause>
    : ImplBase<lito::manifest::ManifestFileCause> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, lito::manifest::ManifestFileCause>
    : ImplBase<lito::manifest::ManifestFileCause> {
    auto source() const noexcept -> Option<error::ErrorRef> {
        const auto& value = this->self();
        if (value.is_Read()) return Some(dyn<error::Error>::from_ref(value.as_Read().source));
        if (value.is_Utf8()) return Some(dyn<error::Error>::from_ref(value.as_Utf8().source));
        if (value.is_Parse()) return Some(dyn<error::Error>::from_ref(value.as_Parse().source));
        return Some(dyn<error::Error>::from_ref(value.as_Schema().source));
    }
};

template<>
struct Impl<fmt::Display, lito::manifest::ManifestFileError>
    : ImplBase<lito::manifest::ManifestFileError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return formatter.write_fmt(
            fmt::Arguments::make("cannot load manifest '{}'", this->self().path.as_path()));
    }
};

template<>
struct Impl<fmt::Debug, lito::manifest::ManifestFileError>
    : ImplBase<lito::manifest::ManifestFileError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, lito::manifest::ManifestFileError>
    : ImplBase<lito::manifest::ManifestFileError> {
    auto source() const noexcept -> Option<error::ErrorRef> {
        return Some(dyn<error::Error>::from_ref(this->self().cause));
    }
};

template<>
struct Impl<fmt::Display, lito::manifest::ManifestError> : ImplBase<lito::manifest::ManifestError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        const auto& value = this->self();
        if (value.is_Locate()) {
            return formatter.write_raw("cannot locate manifest",
                                       sizeof("cannot locate manifest") - 1);
        }
        if (value.is_File()) {
            return formatter.write_raw("manifest file loading failed",
                                       sizeof("manifest file loading failed") - 1);
        }
        return formatter.write_fmt(fmt::Arguments::make(
            "directory '{}' contains a workspace manifest where a package is required",
            value.as_Kind().requested_directory.as_path()));
    }
};

template<>
struct Impl<fmt::Debug, lito::manifest::ManifestError> : ImplBase<lito::manifest::ManifestError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, lito::manifest::ManifestError> : ImplBase<lito::manifest::ManifestError> {
    auto source() const noexcept -> Option<error::ErrorRef> {
        const auto& value = this->self();
        if (value.is_Locate()) return Some(dyn<error::Error>::from_ref(value.as_Locate().source));
        if (value.is_File()) return Some(dyn<error::Error>::from_ref(value.as_File().source));
        return None();
    }
};

} // namespace rstd
