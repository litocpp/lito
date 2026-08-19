module;
#include <rstd/enum.hpp>

export module lito.cpp:standard_library.error;

import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::cpp
{

class StandardLibraryError {
    RSTD_ENUM(StandardLibraryError,
              (LanguageStandard,
               (rstd::path::PathBuf source; String module; String current; String required;)),
              (MissingProvider, (rstd::path::PathBuf manifest; String module;)),
              (ProviderMismatch,
               (rstd::path::PathBuf manifest; rstd::path::PathBuf source; String expected;
                Option<String>                                                   provided;
                bool                                                             interface;)),
              (UndeclaredDependency,
               (rstd::path::PathBuf manifest; rstd::path::PathBuf source; String module;
                String                                                           required;)))
};

} // namespace lito::cpp

export namespace rstd
{

template<>
struct Impl<fmt::Display, lito::cpp::StandardLibraryError>
    : ImplBase<lito::cpp::StandardLibraryError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        const auto& error = this->self();
        if (error.is_LanguageStandard()) {
            const auto& value = error.as_LanguageStandard();
            return formatter.write_fmt(fmt::Arguments::make(
                "source '{}' imports standard library module '{}' using '{}'; import std requires "
                "{} or later",
                value.source.as_path(),
                value.module,
                value.current,
                value.required));
        }
        if (error.is_MissingProvider()) {
            const auto& value = error.as_MissingProvider();
            return formatter.write_fmt(
                fmt::Arguments::make("standard library module manifest '{}' does not provide '{}'",
                                     value.manifest.as_path(),
                                     value.module));
        }
        if (error.is_ProviderMismatch()) {
            const auto& value  = error.as_ProviderMismatch();
            auto        actual = value.provided.is_some() ? value.provided->as_str() : "<none>"_str;
            return formatter.write_fmt(fmt::Arguments::make(
                "standard library module source '{}' from manifest '{}' must provide interface "
                "'{}'; provided '{}' as {}",
                value.source.as_path(),
                value.manifest.as_path(),
                value.expected,
                actual,
                value.interface ? "an interface" : "a non-interface"));
        }
        const auto& value = error.as_UndeclaredDependency();
        return formatter.write_fmt(fmt::Arguments::make(
            "standard library module '{}' source '{}' requires module '{}', which is not provided "
            "by manifest '{}'",
            value.module,
            value.source.as_path(),
            value.required,
            value.manifest.as_path()));
    }
};

template<>
struct Impl<fmt::Debug, lito::cpp::StandardLibraryError>
    : ImplBase<lito::cpp::StandardLibraryError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, lito::cpp::StandardLibraryError>
    : ImplBase<lito::cpp::StandardLibraryError> {
    auto source() const noexcept -> Option<error::ErrorRef> { return None(); }
};

} // namespace rstd
