export module lito.frontend.lexical:error;

import rstd;
import :token;

using namespace rstd::prelude;

export namespace lito::frontend::lexical
{

struct Error {
    String                      message;
    Option<SourceLocation>      location;
    Option<rstd::path::PathBuf> path;

    static auto make(ref<str> message) -> Error {
        return Error { .message = String::make(message) };
    }

    static auto make(String message) -> Error { return Error { .message = rstd::move(message) }; }

    static auto at(String message, SourceLocation location) -> Error {
        return Error { .message = rstd::move(message), .location = Some(location) };
    }
};

template<typename T>
using Result = rstd::Result<T, Error>;

} // namespace lito::frontend::lexical

export namespace rstd
{

template<>
struct Impl<fmt::Display, lito::frontend::lexical::Error>
    : ImplBase<lito::frontend::lexical::Error> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        const auto& error = this->self();
        if (error.path.is_some() && error.location.is_some()) {
            return formatter.write_fmt(fmt::Arguments::make("{}:{}:{}: {}",
                                                            error.path->as_path(),
                                                            error.location->line,
                                                            error.location->column,
                                                            error.message));
        }
        if (error.path.is_some()) {
            return formatter.write_fmt(
                fmt::Arguments::make("{}: {}", error.path->as_path(), error.message));
        }
        return formatter.write_str(error.message.as_str());
    }
};

template<>
struct Impl<fmt::Debug, lito::frontend::lexical::Error> : ImplBase<lito::frontend::lexical::Error> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, lito::frontend::lexical::Error>
    : DefaultInImpl<error::Error, lito::frontend::lexical::Error> {};

} // namespace rstd
