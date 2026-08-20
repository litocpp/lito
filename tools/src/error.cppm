module;
#include <rstd/enum.hpp>

export module lito.tools:error;

import rstd;
import rstd.json;
import lito.system;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::tools
{

class ToolError {
    RSTD_ENUM(ToolError,
              (System, (lito::system::SystemError source;)),
              (Io, (String operation; rstd::path::PathBuf path; rstd::io::error::Error source;)),
              (Json, (String context; rstd::path::PathBuf path; rstd::json::Error source;)),
              (Context, (String context; Box<ToolError> source;)),
              (Execution,
               (String operation; i32 exit_code; String standard_output; String standard_error;)),
              (Message, (String message;)))
};

template<typename T>
using ToolResult = Result<T, ToolError>;

} // namespace lito::tools

export namespace rstd
{

template<>
struct Impl<convert::From<lito::system::SystemError>, lito::tools::ToolError> {
    static auto from(lito::system::SystemError error) -> lito::tools::ToolError {
        return lito::tools::ToolError::System(rstd::move(error));
    }
};

template<>
struct Impl<fmt::Display, lito::tools::ToolError> : ImplBase<lito::tools::ToolError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        const auto& error = this->self();
        if (error.is_System()) return formatter.write_str("host tool process operation failed"_str);
        if (error.is_Io()) {
            const auto& value = error.as_Io();
            return formatter.write_fmt(
                fmt::Arguments::make("{} '{}'", value.operation, value.path.as_path()));
        }
        if (error.is_Json()) {
            const auto& value = error.as_Json();
            return formatter.write_fmt(
                fmt::Arguments::make("cannot parse {} '{}'", value.context, value.path.as_path()));
        }
        if (error.is_Context()) return formatter.write_str(error.as_Context().context.as_str());
        if (error.is_Execution()) {
            const auto& value = error.as_Execution();
            return formatter.write_fmt(fmt::Arguments::make("{} failed with exit code {}:\n{}{}",
                                                            value.operation,
                                                            value.exit_code,
                                                            value.standard_output,
                                                            value.standard_error));
        }
        return formatter.write_str(error.as_Message().message.as_str());
    }
};

template<>
struct Impl<fmt::Debug, lito::tools::ToolError> : ImplBase<lito::tools::ToolError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, lito::tools::ToolError> : ImplBase<lito::tools::ToolError> {
    auto source() const noexcept -> Option<error::ErrorRef> {
        const auto& error = this->self();
        if (error.is_System()) return Some(dyn<error::Error>::from_ref(error.as_System().source));
        if (error.is_Io()) return Some(dyn<error::Error>::from_ref(error.as_Io().source));
        if (error.is_Json()) return Some(dyn<error::Error>::from_ref(error.as_Json().source));
        if (error.is_Context()) {
            return Some(dyn<error::Error>::from_ref(*error.as_Context().source));
        }
        return None();
    }
};

} // namespace rstd
