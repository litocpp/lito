export module tenon.toolchain.clang_options;

import rstd;
import tenon.model;

using namespace rstd::literals;

export namespace tenon::toolchain::clang_options
{

inline constexpr auto VERSION             = "--version"_str;
inline constexpr auto PRINT_TARGET_TRIPLE = "-print-target-triple"_str;
inline constexpr auto PRINT_RESOURCE_DIR  = "-print-resource-dir"_str;

inline constexpr auto RESOURCE_DIR  = "-resource-dir"_str;
inline constexpr auto STANDARD      = "-std="_str;
inline constexpr auto NO_RTTI       = "-fno-rtti"_str;
inline constexpr auto NO_EXCEPTIONS = "-fno-exceptions"_str;
inline constexpr auto DEFINE        = "-D"_str;
inline constexpr auto INCLUDE        = "-I"_str;

inline constexpr auto SCAN_FORMAT_P1689 = "-format=p1689"_str;
inline constexpr auto DRIVER_ARGUMENTS   = "--"_str;
inline constexpr auto DEPENDENCIES       = "-MD"_str;
inline constexpr auto DEPENDENCY_TARGET  = "-MT"_str;
inline constexpr auto DEPENDENCY_FILE    = "-MF"_str;

inline constexpr auto LANGUAGE      = "-x"_str;
inline constexpr auto CXX_MODULE    = "c++-module"_str;
inline constexpr auto MODULE_OUTPUT = "-fmodule-output="_str;
inline constexpr auto MODULE_FILE   = "-fmodule-file="_str;
inline constexpr auto COMPILE       = "-c"_str;
inline constexpr auto OUTPUT        = "-o"_str;
inline constexpr auto ARCHIVE_CREATE = "rcs"_str;

constexpr auto standard_library(StandardLibrary value) noexcept -> rstd::ref<rstd::str> {
    return value == StandardLibrary::Libstdcxx ? "-stdlib=libstdc++"_str
                                                : "-stdlib=libc++"_str;
}

constexpr auto bmi(BmiMode value) noexcept -> rstd::ref<rstd::str> {
    return value == BmiMode::Reduced ? "-fmodules-reduced-bmi"_str
                                     : "-fno-modules-reduced-bmi"_str;
}

} // namespace tenon::toolchain::clang_options
