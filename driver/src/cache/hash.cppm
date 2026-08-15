module;

module lito.driver:cache.hash;

import rstd;

using namespace rstd::prelude;

namespace lito::cache
{

inline constexpr uint64_t FNV_OFFSET = 14695981039346656037ull;
inline constexpr uint64_t FNV_PRIME  = 1099511628211ull;

auto add_bytes(uint64_t& hash, slice<u8> bytes) -> void {
    for (auto value : bytes) {
        hash ^= value.to_primitive();
        hash *= FNV_PRIME;
    }
}

auto add_text(uint64_t& hash, ref<str> value) -> void {
    for (auto byte : value) {
        hash ^= byte.to_primitive();
        hash *= FNV_PRIME;
    }
    hash ^= 0;
    hash *= FNV_PRIME;
}

auto hex(uint64_t value) -> String {
    static constexpr char digits[] = "0123456789abcdef";
    char                  result[16];
    for (size_t index = 0; index < 16; ++index) {
        result[15 - index] = digits[value & 0xfu];
        value >>= 4u;
    }
    return String::make(
        ref<str>::from_raw_parts_unchecked(reinterpret_cast<const byte*>(result), usize(16)));
}

auto text_identity(ref<str> recipe, ref<str> value) -> String {
    auto hash = FNV_OFFSET;
    add_text(hash, recipe);
    add_text(hash, value);
    return hex(hash);
}

} // namespace lito::cache
