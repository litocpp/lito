export module lito.core:manifest.name;
import rstd;
using namespace rstd::literals;
using namespace rstd::prelude;

export namespace lito::manifest
{
auto package_name_is_valid(ref<str> value) -> bool {
    if (value.is_empty()) return false;
    for (auto byte : value) {
        const bool alpha =
            (byte >= u8('a') && byte <= u8('z')) || (byte >= u8('A') && byte <= u8('Z'));
        const bool digit = byte >= u8('0') && byte <= u8('9');
        if (! alpha && ! digit && byte != u8('-') && byte != u8('_')) return false;
    }
    return true;
}
auto package_attribute_scope_is_valid(ref<str> value) -> bool {
    if (! package_name_is_valid(value) || value.contains("__"_str)) return false;
    auto first = value[usize {}];
    if (! ((first >= u8('a') && first <= u8('z')) || (first >= u8('A') && first <= u8('Z'))))
        return false;
    if (value.contains("-"_str)) return false;
    constexpr ref<str> reserved[] = { "clang"_str, "gnu"_str, "msvc"_str,
                                      "gsl"_str,   "omp"_str, "std"_str };
    for (auto scope : reserved) {
        if (value == scope) return false;
    }
    return true;
}
} // namespace lito::manifest
