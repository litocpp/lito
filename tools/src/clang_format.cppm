module;
#include <rstd/macro.hpp>

export module lito.tools:clang_format;

import rstd;
import lito.system;
import :command;
import :error;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;
using namespace lito::system;
using namespace rstd::literals;

namespace lito::tools
{

template<typename T>
auto clang_format_failure(ref<str> message) -> ToolResult<T> {
    return Err(ToolError::Message(String::make(message)));
}

} // namespace lito::tools

export namespace lito::tools
{

class ClangFormat {
public:
    static auto create(ref<rstd::path::Path>             formatter_path,
                       const ResolvedProcessEnvironment& environment) -> ToolResult<ClangFormat> {
        auto path      = PathBuf::from(formatter_path);
        auto arguments = Vec<String>::make();
        rstd_try(command::push_path(arguments, path.as_path()));
        command::push_option(arguments, "--version"_str);
        auto version = rstd_try(
            command::tool_output(rstd::move(arguments), "clang-format --version"_str, environment));
        if (! version.as_str().contains("clang-format version"_str)) {
            return clang_format_failure<ClangFormat>(
                "configured formatter is not clang-format"_str);
        }
        return Ok(
            ClangFormat { rstd::move(path), rstd::move(version), rstd::addressof(environment) });
    }

    auto path() const -> ref<rstd::path::Path> { return path_.as_path(); }
    auto version() const -> ref<str> { return version_.as_str(); }

    auto is_formatted(ref<rstd::path::Path> source) const -> ToolResult<bool> {
        auto contents = rstd::fs::read_to_string(source);
        if (contents.is_err()) {
            return Err(ToolError::Io(String::make("cannot read format source"_str),
                                     PathBuf::from(source),
                                     rstd::move(contents).unwrap_err()));
        }
        auto arguments = Vec<String>::make();
        rstd_try(command::push_path(arguments, path_.as_path()));
        rstd_try(command::push_path(arguments, source));
        auto output = rstd_try(
            command::tool_output_raw(rstd::move(arguments), "clang-format"_str, *environment_));
        return Ok(contents->as_str() == output.as_str());
    }

    auto format(ref<rstd::path::Path> source) const -> ToolResult<empty> {
        auto arguments = Vec<String>::make();
        rstd_try(command::push_path(arguments, path_.as_path()));
        command::push_option(arguments, "-i"_str);
        rstd_try(command::push_path(arguments, source));
        rstd_try(command::tool_output(rstd::move(arguments), "clang-format"_str, *environment_));
        return Ok(empty {});
    }

private:
    ClangFormat(PathBuf path, String version, const ResolvedProcessEnvironment* environment)
        : path_(rstd::move(path)), version_(rstd::move(version)), environment_(environment) {}

    PathBuf                           path_;
    String                            version_;
    const ResolvedProcessEnvironment* environment_ {};
};

} // namespace lito::tools
