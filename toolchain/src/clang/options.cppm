export module lito.toolchain.clang:options;

import rstd;
import lito.core;
import lito.cpp;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::toolchain::clang_options
{

inline constexpr auto VERSION             = "--version"_str;
inline constexpr auto HELP                = "--help"_str;
inline constexpr auto PRINT_COMPILER_PATH = "-print-prog-name=clang++"_str;
inline constexpr auto PRINT_TARGET_TRIPLE = "-print-target-triple"_str;
inline constexpr auto PRINT_RESOURCE_DIR  = "-print-resource-dir"_str;

inline constexpr auto RESOURCE_DIR  = "-resource-dir"_str;
inline constexpr auto STANDARD      = "-std="_str;
inline constexpr auto NO_RTTI       = "-fno-rtti"_str;
inline constexpr auto RTTI          = "-frtti"_str;
inline constexpr auto NO_EXCEPTIONS = "-fno-exceptions"_str;
inline constexpr auto EXCEPTIONS    = "-fexceptions"_str;
inline constexpr auto DEFINE        = "-D"_str;
inline constexpr auto INCLUDE       = "-I"_str;

inline constexpr auto DEPENDENCIES      = "-MD"_str;
inline constexpr auto DEPENDENCY_TARGET = "-MT"_str;
inline constexpr auto DEPENDENCY_FILE   = "-MF"_str;
inline constexpr auto PREPROCESS        = "-E"_str;
inline constexpr auto DUMP_MACROS       = "-dM"_str;
inline constexpr auto VERBOSE           = "-v"_str;
inline constexpr auto NO_LINE_MARKERS   = "-P"_str;
inline constexpr auto STANDARD_INPUT    = "-"_str;

inline constexpr auto LANGUAGE            = "-x"_str;
inline constexpr auto CXX_SOURCE          = "c++"_str;
inline constexpr auto CXX_MODULE          = "c++-module"_str;
inline constexpr auto MODULE_OUTPUT       = "-fmodule-output="_str;
inline constexpr auto MODULE_FILE         = "-fmodule-file="_str;
inline constexpr auto EMBED_ALL_FILES     = "-fmodules-embed-all-files"_str;
inline constexpr auto COMPILE             = "-c"_str;
inline constexpr auto OUTPUT              = "-o"_str;
inline constexpr auto ARCHIVE_CREATE      = "rcs"_str;
inline constexpr auto WHOLE_ARCHIVE       = "-Wl,--whole-archive"_str;
inline constexpr auto NO_WHOLE_ARCHIVE    = "-Wl,--no-whole-archive"_str;
inline constexpr auto LINKER_ARGUMENT     = "-Xlinker"_str;
inline constexpr auto FORCE_LOAD          = "-force_load"_str;
inline constexpr auto NO_STANDARD_LIBRARY = "-nostdlib++"_str;

constexpr auto standard_library(lito::config::StandardLibrary value) noexcept -> ref<str> {
    return value == lito::config::StandardLibrary::Libstdcxx ? "-stdlib=libstdc++"_str
                                                             : "-stdlib=libc++"_str;
}

constexpr auto standard_library_linker_option(lito::config::StandardLibrary value,
                                              bool link) noexcept -> ref<str> {
    return link ? standard_library(value) : NO_STANDARD_LIBRARY;
}

constexpr auto bmi(cpp::BmiMode value) noexcept -> ref<str> {
    return value == cpp::BmiMode::Reduced ? "-fmodules-reduced-bmi"_str
                                          : "-fno-modules-reduced-bmi"_str;
}

} // namespace lito::toolchain::clang_options
