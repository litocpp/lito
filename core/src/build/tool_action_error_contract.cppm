module;
#include <rstd/enum.hpp>

export module lito.build.tool_action_error_contract;

import rstd;
import lito.error;
import lito.system.error_contract;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito
{

class BuildToolActionError {
    RSTD_ENUM(BuildToolActionError,
              (UnknownTool, (String alias;)),
              (InvalidRequest, (String message;)),
              (InvalidInput, (PathBuf path; String reason;)),
              (InvalidOutput, (PathBuf path; String reason;)),
              (Execution,
               (String alias; i32 exit_code; String standard_output; String standard_error;)),
              (Process, (String alias; SystemError source;)),
              (Publication,
               (String operation; PathBuf path; rstd::io::error::Error source;)),
              (Receipt,
               (String operation; PathBuf path; rstd::io::error::Error source;)))
};

template<typename T>
using BuildToolActionResult = rstd::Result<T, BuildToolActionError>;

} // namespace lito

export namespace rstd
{

template<>
struct Impl<fmt::Display, lito::BuildToolActionError>
    : ImplBase<lito::BuildToolActionError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        const auto& error = this->self();
        if (error.is_UnknownTool()) {
            return formatter.write_str(
                rstd::format("build script requested undeclared build-tool '{}'",
                             error.as_UnknownTool().alias.as_str())
                    .as_str());
        }
        if (error.is_InvalidRequest()) {
            return formatter.write_str(error.as_InvalidRequest().message.as_str());
        }
        if (error.is_InvalidInput()) {
            const auto& value = error.as_InvalidInput();
            return formatter.write_str(
                rstd::format("build-tool action input '{}': {}",
                             value.path.as_path(),
                             value.reason.as_str())
                    .as_str());
        }
        if (error.is_InvalidOutput()) {
            const auto& value = error.as_InvalidOutput();
            return formatter.write_str(
                rstd::format("build-tool action output '{}': {}",
                             value.path.as_path(),
                             value.reason.as_str())
                    .as_str());
        }
        if (error.is_Execution()) {
            const auto& value = error.as_Execution();
            return formatter.write_str(
                rstd::format("build-tool '{}' exited with {}\n{}",
                             value.alias.as_str(),
                             value.exit_code,
                             value.standard_error.as_str())
                    .as_str());
        }
        if (error.is_Process()) {
            return formatter.write_str(
                rstd::format("cannot execute build-tool '{}'",
                             error.as_Process().alias.as_str())
                    .as_str());
        }
        if (error.is_Publication()) {
            const auto& value = error.as_Publication();
            return formatter.write_str(
                rstd::format("cannot {} '{}'", value.operation.as_str(), value.path.as_path())
                    .as_str());
        }
        const auto& value = error.as_Receipt();
        return formatter.write_str(
            rstd::format("cannot {} '{}'", value.operation.as_str(), value.path.as_path())
                .as_str());
    }
};

template<>
struct Impl<fmt::Debug, lito::BuildToolActionError>
    : ImplBase<lito::BuildToolActionError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, lito::BuildToolActionError>
    : ImplBase<lito::BuildToolActionError> {
    auto source() const noexcept -> Option<error::ErrorRef> {
        const auto& error = this->self();
        if (error.is_Process()) {
            return Some(dyn<error::Error>::from_ref(error.as_Process().source));
        }
        if (error.is_Publication()) {
            return Some(dyn<error::Error>::from_ref(error.as_Publication().source));
        }
        if (error.is_Receipt()) {
            return Some(dyn<error::Error>::from_ref(error.as_Receipt().source));
        }
        return None();
    }
};

} // namespace rstd
