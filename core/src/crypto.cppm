module;
#include <rstd/enum.hpp>
#include <sodium.h>

export module lito.crypto;

import rstd;

using namespace rstd::prelude;
using ::alloc::string::String;

export namespace lito::crypto
{

class Sha256DigestParseError {
    RSTD_ENUM(Sha256DigestParseError, (Length, (usize actual;)), (Character, (usize index;)))
};

class Sha256Digest : public DefaultInClass<Sha256Digest, Clone> {
    array<u8, crypto_hash_sha256_BYTES> bytes_;

public:
    static auto from_bytes(array<u8, crypto_hash_sha256_BYTES> bytes) noexcept -> Sha256Digest {
        auto result   = Sha256Digest {};
        result.bytes_ = rstd::move(bytes);
        return result;
    }

    static auto parse_hex(ref<str> value) -> Result<Sha256Digest, Sha256DigestParseError> {
        if (value.len() != usize(crypto_hash_sha256_BYTES * 2)) {
            return Err(Sha256DigestParseError::Length(value.len()));
        }
        auto bytes  = array<u8, crypto_hash_sha256_BYTES> {};
        auto nibble = [](u8 value) -> Option<u8> {
            const auto byte = value.to_primitive();
            if (byte >= '0' && byte <= '9') return Some(u8(byte - '0'));
            if (byte >= 'a' && byte <= 'f') return Some(u8(byte - 'a' + 10));
            if (byte >= 'A' && byte <= 'F') return Some(u8(byte - 'A' + 10));
            return None();
        };
        for (usize index {}; index < usize(crypto_hash_sha256_BYTES); ++index) {
            auto high = nibble(value[index * usize(2)]);
            if (high.is_none()) {
                return Err(Sha256DigestParseError::Character(index * usize(2)));
            }
            auto low = nibble(value[index * usize(2) + usize(1)]);
            if (low.is_none()) {
                return Err(Sha256DigestParseError::Character(index * usize(2) + usize(1)));
            }
            bytes[index] = u8((high->to_primitive() << 4u) | low->to_primitive());
        }
        return Ok(from_bytes(rstd::move(bytes)));
    }

    auto as_bytes() const noexcept -> slice<u8> { return bytes_.as_slice(); }

    auto to_hex() const -> String {
        static constexpr char digits[] = "0123456789abcdef";
        auto                  result   = String::make();
        result.reserve(usize(crypto_hash_sha256_BYTES * 2));
        for (const auto value : bytes_) {
            const auto byte = value.to_primitive();
            result.push_ascii(digits[byte >> 4u]);
            result.push_ascii(digits[byte & 0x0fu]);
        }
        return result;
    }

    auto clone() const -> Sha256Digest { return from_bytes(bytes_); }

    friend auto operator==(const Sha256Digest& left, const Sha256Digest& right) noexcept -> bool {
        return sodium_memcmp(left.bytes_.as_slice().as_raw_ptr(),
                             right.bytes_.as_slice().as_raw_ptr(),
                             crypto_hash_sha256_BYTES) == 0;
    }
};

class Sha256 {
    crypto_hash_sha256_state state_ {};

    Sha256() noexcept {
        if (sodium_init() < 0 || crypto_hash_sha256_init(&state_) != 0) {
            rstd::panic("cannot initialize libsodium SHA-256");
        }
    }

public:
    static auto make() noexcept -> Sha256 { return {}; }

    void update(slice<u8> input) noexcept {
        if (crypto_hash_sha256_update(&state_,
                                      reinterpret_cast<const unsigned char*>(input.as_raw_ptr()),
                                      input.len().to_primitive()) != 0) {
            rstd::panic("cannot update libsodium SHA-256 state");
        }
    }

    auto finalize() && noexcept -> array<u8, crypto_hash_sha256_BYTES> {
        auto result = array<u8, crypto_hash_sha256_BYTES> {};
        if (crypto_hash_sha256_final(
                &state_, reinterpret_cast<unsigned char*>(result.as_mut_slice().as_raw_ptr())) !=
            0) {
            rstd::panic("cannot finalize libsodium SHA-256 state");
        }
        return result;
    }

    auto finalize_digest() && noexcept -> Sha256Digest {
        return Sha256Digest::from_bytes(rstd::move(*this).finalize());
    }
};

auto sha256(slice<u8> input) noexcept -> array<u8, crypto_hash_sha256_BYTES> {
    auto state = Sha256::make();
    state.update(input);
    return rstd::move(state).finalize();
}

auto sha256_hex(array<u8, crypto_hash_sha256_BYTES> digest) -> String {
    static constexpr char digits[] = "0123456789abcdef";
    auto                  result   = String::make();
    result.reserve(usize(crypto_hash_sha256_BYTES * 2));
    for (const auto value : digest) {
        const auto byte = value.get().to_primitive();
        result.push_ascii(digits[byte >> 4u]);
        result.push_ascii(digits[byte & 0x0fu]);
    }
    return result;
}

auto sha256_hex(slice<u8> input) -> String {
    return sha256_hex(sha256(input));
}

auto sha256_hex(ref<str> input) -> String {
    return sha256_hex(input.as_bytes());
}

auto sha256_digest(slice<u8> input) noexcept -> Sha256Digest {
    return Sha256Digest::from_bytes(sha256(input));
}

auto sha256_digest(ref<str> input) noexcept -> Sha256Digest {
    return sha256_digest(input.as_bytes());
}

} // namespace lito::crypto

export namespace rstd
{

template<>
struct Impl<str_::FromStr, lito::crypto::Sha256Digest> {
    using Err = lito::crypto::Sha256DigestParseError;

    static auto from_str(ref<str> value) -> Result<lito::crypto::Sha256Digest, Err> {
        return lito::crypto::Sha256Digest::parse_hex(value);
    }
};

template<>
struct Impl<convert::TryFrom<ref<str>>, lito::crypto::Sha256Digest> {
    using Error = lito::crypto::Sha256DigestParseError;

    static auto try_from(ref<str> value) -> Result<lito::crypto::Sha256Digest, Error> {
        return rstd::from_str<lito::crypto::Sha256Digest>(value);
    }
};

template<>
struct Impl<fmt::Display, lito::crypto::Sha256Digest> : ImplBase<lito::crypto::Sha256Digest> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        static constexpr char digits[] = "0123456789abcdef";
        for (const auto value : this->self().as_bytes()) {
            const auto byte = value.to_primitive();
            if (! formatter.write_raw(&digits[byte >> 4u], 1) ||
                ! formatter.write_raw(&digits[byte & 0x0fu], 1)) {
                return false;
            }
        }
        return true;
    }
};

template<>
struct Impl<fmt::Debug, lito::crypto::Sha256Digest> : ImplBase<lito::crypto::Sha256Digest> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<fmt::Display, lito::crypto::Sha256DigestParseError>
    : ImplBase<lito::crypto::Sha256DigestParseError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        const auto& error = this->self();
        if (error.is_Length()) {
            return formatter.write_fmt(fmt::Arguments::make(
                "SHA-256 digest must contain 64 hexadecimal characters; found {}",
                error.as_Length().actual));
        }
        return formatter.write_fmt(
            fmt::Arguments::make("SHA-256 digest contains a non-hexadecimal character at byte {}",
                                 error.as_Character().index));
    }
};

template<>
struct Impl<fmt::Debug, lito::crypto::Sha256DigestParseError>
    : ImplBase<lito::crypto::Sha256DigestParseError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, lito::crypto::Sha256DigestParseError>
    : DefaultInImpl<error::Error, lito::crypto::Sha256DigestParseError> {};

template<>
struct Impl<hash::Hash, lito::crypto::Sha256Digest> : ImplBase<lito::crypto::Sha256Digest> {
    template<typename H>
        requires Impled<H, hash::Hasher>
    void hash(H& state) const noexcept {
        rstd::as<hash::Hasher>(state).write(this->self().as_bytes());
    }
};

} // namespace rstd
