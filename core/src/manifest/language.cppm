module;
#include <rstd/enum.hpp>

export module lito.core:manifest.language;

import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::manifest
{

enum class PackageLanguage
{
    C,
    Cpp,
};

enum class CStandard
{
    C99,
    C11,
    C17,
    C23,
};

enum class CppStandard
{
    Cpp20,
    Cpp23,
    Cpp26,
};

class PackageStandardRequirement : public DefaultInClass<PackageStandardRequirement, Clone> {
    RSTD_ENUM(PackageStandardRequirement, (C, (CStandard minimum;)), (Cpp, (CppStandard minimum;)))

public:
    auto clone() const -> PackageStandardRequirement {
        return is_C() ? C(as_C().minimum) : Cpp(as_Cpp().minimum);
    }
};

constexpr auto package_language_name(PackageLanguage language) noexcept -> ref<str> {
    return language == PackageLanguage::C ? "c"_str : "cpp"_str;
}

constexpr auto c_standard_name(CStandard standard) noexcept -> ref<str> {
    switch (standard) {
    case CStandard::C99: return "c99"_str;
    case CStandard::C11: return "c11"_str;
    case CStandard::C17: return "c17"_str;
    case CStandard::C23: return "c23"_str;
    }
    __builtin_unreachable();
}

constexpr auto cpp_standard_name(CppStandard standard) noexcept -> ref<str> {
    switch (standard) {
    case CppStandard::Cpp20: return "c++20"_str;
    case CppStandard::Cpp23: return "c++23"_str;
    case CppStandard::Cpp26: return "c++26"_str;
    }
    __builtin_unreachable();
}

constexpr auto c_standard_rank(CStandard standard) noexcept -> usize {
    switch (standard) {
    case CStandard::C99: return usize {};
    case CStandard::C11: return usize(1);
    case CStandard::C17: return usize(2);
    case CStandard::C23: return usize(3);
    }
    __builtin_unreachable();
}

constexpr auto cpp_standard_rank(CppStandard standard) noexcept -> usize {
    switch (standard) {
    case CppStandard::Cpp20: return usize {};
    case CppStandard::Cpp23: return usize(1);
    case CppStandard::Cpp26: return usize(2);
    }
    __builtin_unreachable();
}

auto parse_c_standard(ref<str> value) noexcept -> Option<CStandard> {
    if (value == "c99"_str) return Some(CStandard::C99);
    if (value == "c11"_str) return Some(CStandard::C11);
    if (value == "c17"_str) return Some(CStandard::C17);
    if (value == "c23"_str) return Some(CStandard::C23);
    return None();
}

auto parse_cpp_standard(ref<str> value) noexcept -> Option<CppStandard> {
    if (value == "c++20"_str) return Some(CppStandard::Cpp20);
    if (value == "c++23"_str) return Some(CppStandard::Cpp23);
    if (value == "c++26"_str) return Some(CppStandard::Cpp26);
    return None();
}

constexpr auto package_standard_language(const PackageStandardRequirement& requirement) noexcept
    -> PackageLanguage {
    return requirement.is_C() ? PackageLanguage::C : PackageLanguage::Cpp;
}

constexpr auto requirement_standard_name(const PackageStandardRequirement& requirement) noexcept
    -> ref<str> {
    return requirement.is_C() ? c_standard_name(requirement.as_C().minimum)
                              : cpp_standard_name(requirement.as_Cpp().minimum);
}

} // namespace lito::manifest
