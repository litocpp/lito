export module tenon.toolchain.clang_scan_deps_options;

import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace tenon::toolchain::clang_scan_deps_options
{

inline constexpr auto VERSION          = "--version"_str;
inline constexpr auto FORMAT_P1689     = "-format=p1689"_str;
inline constexpr auto DRIVER_ARGUMENTS = "--"_str;

} // namespace tenon::toolchain::clang_scan_deps_options
