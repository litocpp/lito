export module tenon.toolchain.clang_format_options;

import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace tenon::toolchain::clang_format_options
{

inline constexpr auto VERSION  = "--version"_str;
inline constexpr auto IN_PLACE = "-i"_str;

} // namespace tenon::toolchain::clang_format_options
