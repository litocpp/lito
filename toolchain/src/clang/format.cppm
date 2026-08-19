export module lito.toolchain.clang:format;

import rstd;
import lito.core;
import lito.toolchain.common;
import lito.system;
import :format_options;

using namespace rstd::prelude;
using namespace lito::system;
using namespace rstd::literals;

namespace lito::toolchain
{

template<typename T>
auto clang_format_failure(ref<str> message) -> ToolchainResult<T> {
    return Err(ToolchainError::Message(String::make(message)));
}

} // namespace lito::toolchain

export namespace lito::toolchain
{

class ClangFormat {
public:
    static auto create(ref<rstd::path::Path>             formatter_path,
                       const ResolvedProcessEnvironment& environment)
        -> ToolchainResult<ClangFormat> {
        auto path = PathBuf::from(formatter_path);

        auto arguments = Vec<String>::make();
        auto pushed    = command::push_path(arguments, path.as_path());
        if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
        command::push_option(arguments, clang_format_options::VERSION);
        auto version =
            command::tool_output(rstd::move(arguments), "clang-format --version"_str, environment);
        if (version.is_err()) return Err(rstd::move(version).unwrap_err());
        if (! version->as_str().contains("clang-format version"_str)) {
            return clang_format_failure<ClangFormat>(
                "configured formatter is not clang-format"_str);
        }

        return Ok(ClangFormat {
            rstd::move(path),
            rstd::move(version).unwrap(),
            rstd::addressof(environment),
        });
    }

    auto path() const -> ref<rstd::path::Path> { return path_.as_path(); }
    auto version() const -> ref<str> { return version_.as_str(); }

    auto is_formatted(ref<rstd::path::Path> source) const -> ToolchainResult<bool> {
        auto contents = rstd::fs::read_to_string(source);
        if (contents.is_err()) {
            return Err(ToolchainError::Io(String::make("cannot read format source"_str),
                                          PathBuf::from(source),
                                          rstd::move(contents).unwrap_err()));
        }

        auto arguments = Vec<String>::make();
        auto pushed    = command::push_path(arguments, path_.as_path());
        if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
        pushed = command::push_path(arguments, source);
        if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
        auto output =
            command::tool_output_raw(rstd::move(arguments), "clang-format"_str, *environment_);
        if (output.is_err()) return Err(rstd::move(output).unwrap_err());
        return Ok(contents->as_str() == output->as_str());
    }

    auto format(ref<rstd::path::Path> source) const -> ToolchainResult<empty> {
        auto arguments = Vec<String>::make();
        auto pushed    = command::push_path(arguments, path_.as_path());
        if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
        command::push_option(arguments, clang_format_options::IN_PLACE);
        pushed = command::push_path(arguments, source);
        if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
        auto output =
            command::tool_output(rstd::move(arguments), "clang-format"_str, *environment_);
        if (output.is_err()) return Err(rstd::move(output).unwrap_err());
        return Ok(empty {});
    }

private:
    ClangFormat(PathBuf path, String version, const ResolvedProcessEnvironment* environment)
        : path_(rstd::move(path)), version_(rstd::move(version)), environment_(environment) {}

    PathBuf                           path_;
    String                            version_;
    const ResolvedProcessEnvironment* environment_ {};
};

} // namespace lito::toolchain
