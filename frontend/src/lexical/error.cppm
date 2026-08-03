export module tenon.frontend.lexical:error;

import rstd;
import :token;

using namespace rstd::prelude;

export namespace tenon::frontend::lexical
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

} // namespace tenon::frontend::lexical
