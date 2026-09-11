export module lito.core:hash;

import rstd;

using namespace rstd::prelude;

export namespace lito::hash
{

class Fnv1a64 {
public:
    static constexpr uint64_t OFFSET = 14695981039346656037ull;
    static constexpr uint64_t PRIME  = 1099511628211ull;

    constexpr Fnv1a64() noexcept = default;

    constexpr auto write(slice<u8> bytes) noexcept -> void {
        for (auto byte : bytes) {
            value_ ^= byte.to_primitive();
            value_ *= PRIME;
        }
    }

    constexpr auto write_zero_terminated(ref<str> text) noexcept -> void {
        write(text.as_bytes());
        const byte zero {};
        write(slice<u8>::from_raw_parts(&zero, usize(1)));
    }

    constexpr auto finish() const noexcept -> u64 { return u64(value_); }

    auto hex64() const -> String {
        constexpr char digits[] = "0123456789abcdef";
        char           result[16];
        auto           value = value_;
        for (size_t index = 0; index < 16; ++index) {
            result[15 - index] = digits[value & 0xfu];
            value >>= 4u;
        }
        return String::make(
            ref<str>::from_raw_parts_unchecked(reinterpret_cast<const byte*>(result), usize(16)));
    }

private:
    uint64_t value_ = OFFSET;
};

constexpr auto fnv1a64(slice<u8> bytes) noexcept -> u64 {
    auto state = Fnv1a64 {};
    state.write(bytes);
    return state.finish();
}

} // namespace lito::hash
