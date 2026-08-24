module lito.toolchain.common;

import rstd;
import lito.core;
import lito.tools;
import lito.system;
import lito.frontend.lexical;
import lito.cpp;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace rstd
{

auto Impl<convert::From<lito::system::SystemError>, lito::ToolchainError>::from(
    lito::system::SystemError error) -> lito::ToolchainError {
    return lito::ToolchainError::System(rstd::move(error));
}

auto Impl<convert::From<lito::tools::ToolError>, lito::ToolchainError>::from(
    lito::tools::ToolError error) -> lito::ToolchainError {
    return lito::ToolchainError::Tools(rstd::move(error));
}

auto Impl<convert::From<lito::frontend::lexical::Error>, lito::ToolchainError>::from(
    lito::frontend::lexical::Error error) -> lito::ToolchainError {
    return lito::ToolchainError::Frontend(rstd::move(error));
}

auto Impl<convert::From<lito::cpp::CppOptionError>, lito::ToolchainError>::from(
    lito::cpp::CppOptionError error) -> lito::ToolchainError {
    return lito::ToolchainError::Cpp(rstd::move(error));
}

auto Impl<convert::From<lito::system::PlatformError>, lito::ToolchainError>::from(
    lito::system::PlatformError error) -> lito::ToolchainError {
    return lito::ToolchainError::Platform(rstd::move(error));
}

auto Impl<fmt::Display, lito::StandardLibraryModuleError>::fmt(fmt::Formatter& formatter) const
    -> bool {
    const auto& error = this->self();
    if (error.is_Missing()) {
        const auto& value    = error.as_Missing();
        auto        searched = String::make();
        for (const auto& path : value.searched) {
            if (! searched.is_empty()) searched.push_str(", "_str);
            searched.push_str(path.as_path().to_string_lossy().as_str());
        }
        return formatter.write_fmt(fmt::Arguments::make(
            "selected {} for target '{}' at '{}' has no module manifest; searched {}",
            lito::cpp::standard_library_name(value.context.family),
            value.context.target,
            value.context.artifact.as_path(),
            searched));
    }
    if (error.is_Ambiguous()) {
        const auto& value = error.as_Ambiguous();
        return formatter.write_fmt(fmt::Arguments::make(
            "selected {} for target '{}' at '{}' has ambiguous module manifests '{}' and '{}'",
            lito::cpp::standard_library_name(value.context.family),
            value.context.target,
            value.context.artifact.as_path(),
            value.first.as_path(),
            value.second.as_path()));
    }
    if (error.is_Manifest()) {
        const auto& value = error.as_Manifest();
        if (value.entry.is_some()) {
            return formatter.write_fmt(
                fmt::Arguments::make("standard library module manifest '{}' entry '{}': {}",
                                     value.manifest.as_path(),
                                     value.entry->as_str(),
                                     value.message));
        }
        return formatter.write_fmt(fmt::Arguments::make(
            "standard library module manifest '{}': {}", value.manifest.as_path(), value.message));
    }
    if (error.is_Parse()) {
        const auto& value = error.as_Parse();
        if (value.entry.is_some()) {
            return formatter.write_fmt(
                fmt::Arguments::make("standard library module manifest '{}' entry '{}': {}",
                                     value.manifest.as_path(),
                                     value.entry->as_str(),
                                     value.source));
        }
        return formatter.write_fmt(fmt::Arguments::make(
            "standard library module manifest '{}': {}", value.manifest.as_path(), value.source));
    }
    const auto& value = error.as_Io();
    if (value.manifest.is_some() && value.entry.is_some()) {
        return formatter.write_fmt(
            fmt::Arguments::make("{} '{}' from standard library module manifest '{}' entry '{}'",
                                 value.operation,
                                 value.path.as_path(),
                                 value.manifest->as_path(),
                                 value.entry->as_str()));
    }
    if (value.manifest.is_some()) {
        return formatter.write_fmt(
            fmt::Arguments::make("{} '{}' from standard library module manifest '{}'",
                                 value.operation,
                                 value.path.as_path(),
                                 value.manifest->as_path()));
    }
    return formatter.write_fmt(
        fmt::Arguments::make("{} '{}'", value.operation, value.path.as_path()));
}

auto Impl<fmt::Debug, lito::StandardLibraryModuleError>::fmt(fmt::Formatter& formatter) const
    -> bool {
    return as<fmt::Display>(this->self()).fmt(formatter);
}

auto Impl<error::Error, lito::StandardLibraryModuleError>::source() const noexcept
    -> Option<error::ErrorRef> {
    const auto& error = this->self();
    if (error.is_Parse()) return Some(dyn<error::Error>::from_ref(error.as_Parse().source));
    if (error.is_Io()) return Some(dyn<error::Error>::from_ref(error.as_Io().source));
    return None();
}

auto Impl<fmt::Display, lito::ToolchainError>::fmt(fmt::Formatter& formatter) const -> bool {
    const auto& error = this->self();
    if (error.is_System()) {
        return formatter.write_raw("toolchain process operation failed",
                                   sizeof("toolchain process operation failed") - 1);
    }
    if (error.is_Tools()) {
        return formatter.write_raw("toolchain host tool operation failed",
                                   sizeof("toolchain host tool operation failed") - 1);
    }
    if (error.is_Frontend()) {
        return formatter.write_raw("toolchain frontend analysis failed",
                                   sizeof("toolchain frontend analysis failed") - 1);
    }
    if (error.is_Cpp()) {
        return formatter.write_raw("toolchain C++ argument schema failed",
                                   sizeof("toolchain C++ argument schema failed") - 1);
    }
    if (error.is_StandardLibraryModule()) {
        return formatter.write_raw("standard library module resolution failed",
                                   sizeof("standard library module resolution failed") - 1);
    }
    if (error.is_Platform()) {
        return formatter.write_raw("toolchain target platform is invalid",
                                   sizeof("toolchain target platform is invalid") - 1);
    }
    if (error.is_Io()) {
        const auto& value = error.as_Io();
        return formatter.write_fmt(
            fmt::Arguments::make("{} '{}'", value.operation, value.path.as_path()));
    }
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

auto Impl<fmt::Debug, lito::ToolchainError>::fmt(fmt::Formatter& formatter) const -> bool {
    return as<fmt::Display>(this->self()).fmt(formatter);
}

auto Impl<error::Error, lito::ToolchainError>::source() const noexcept -> Option<error::ErrorRef> {
    const auto& error = this->self();
    if (error.is_System()) return Some(dyn<error::Error>::from_ref(error.as_System().source));
    if (error.is_Tools()) return Some(dyn<error::Error>::from_ref(error.as_Tools().source));
    if (error.is_Frontend()) {
        return Some(dyn<error::Error>::from_ref(error.as_Frontend().source));
    }
    if (error.is_Cpp()) return Some(dyn<error::Error>::from_ref(error.as_Cpp().source));
    if (error.is_StandardLibraryModule()) {
        return Some(dyn<error::Error>::from_ref(error.as_StandardLibraryModule().source));
    }
    if (error.is_Platform()) {
        return Some(lito::system::platform_error_ref(error.as_Platform().source));
    }
    if (error.is_Io()) return Some(dyn<error::Error>::from_ref(error.as_Io().source));
    return None();
}

} // namespace rstd
