export module lito.toolchain.clang:format_options;

import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::toolchain::clang_format_options
{

inline constexpr auto VERSION  = "--version"_str;
inline constexpr auto IN_PLACE = "-i"_str;

} // namespace lito::toolchain::clang_format_options
