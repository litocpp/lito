export module lito.toolchain:clang_format;

import rstd;
import lito.model;
import :clang_format_options;
import :command;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace lito::toolchain
{

template<typename T>
auto clang_format_failure(ref<str> message) -> Result<T> {
    return Err(Error::make(ErrorKind::Toolchain, message));
}

} // namespace lito::toolchain

export namespace lito::toolchain
{

class ClangFormat {
public:
    static auto create(ref<rstd::path::Path> formatter_path) -> Result<ClangFormat> {
        auto canonical = command::resolve_tool(formatter_path, "clang-format"_str);
        if (canonical.is_err()) return Err(rstd::move(canonical).unwrap_err());
        auto path = rstd::move(canonical).unwrap();

        auto arguments = Vec<String>::make();
        auto pushed    = command::push_path(arguments, path.as_path());
        if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
        command::push_option(arguments, clang_format_options::VERSION);
        auto version = command::tool_output(rstd::move(arguments), "clang-format --version"_str);
        if (version.is_err()) return Err(rstd::move(version).unwrap_err());
        if (! version->as_str().contains("clang-format version"_str)) {
            return clang_format_failure<ClangFormat>(
                "configured formatter is not clang-format"_str);
        }

        return Ok(ClangFormat {
            rstd::move(path),
            rstd::move(version).unwrap(),
        });
    }

    auto path() const -> ref<rstd::path::Path> { return path_.as_path(); }
    auto version() const -> ref<str> { return version_.as_str(); }

    auto format(ref<rstd::path::Path> source) const -> Result<empty> {
        auto arguments = Vec<String>::make();
        auto pushed    = command::push_path(arguments, path_.as_path());
        if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
        command::push_option(arguments, clang_format_options::IN_PLACE);
        pushed = command::push_path(arguments, source);
        if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
        auto output = command::tool_output(rstd::move(arguments), "clang-format"_str);
        if (output.is_err()) return Err(rstd::move(output).unwrap_err());
        return Ok(empty {});
    }

private:
    ClangFormat(PathBuf path, String version)
        : path_(rstd::move(path)), version_(rstd::move(version)) {}

    PathBuf path_;
    String  version_;
};

} // namespace lito::toolchain
