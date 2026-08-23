module;
#include <cstddef>
#include <rstd/macro.hpp>
#include <sodium/crypto_sign_ed25519.h>
#include <sodium/utils.h>

export module lito.core:registry.crypto;

import rstd;
import :registry.config;
import :registry.digest;
import :registry.error;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::registry
{

class Ed25519Signature : public DefaultInClass<Ed25519Signature, Clone> {
    String value_;

    explicit Ed25519Signature(String value): value_(rstd::move(value)) {}

public:
    static auto parse(ref<str> value) -> RegistryValueResult<Ed25519Signature>;

    auto base64url() const noexcept -> ref<str> { return value_.as_str(); }
    auto clone() const -> Ed25519Signature { return Ed25519Signature(value_.clone()); }
};

auto signing_key_id(const Ed25519PublicKey& public_key) -> RegistryValueResult<SigningKeyId>;
auto verify_ed25519(const Ed25519PublicKey& public_key,
                    const Ed25519Signature& signature,
                    slice<u8>               message) -> RegistryValueResult<empty>;

} // namespace lito::registry

namespace
{

template<rstd::size_t Size>
auto decode_base64url(ref<str> value, ref<str> description)
    -> lito::registry::RegistryValueResult<array<u8, Size>> {
    auto result = array<u8, Size> {};
    auto length = std::size_t {};
    auto status =
        sodium_base642bin(reinterpret_cast<unsigned char*>(result.as_mut_slice().as_raw_ptr()),
                          Size,
                          reinterpret_cast<const char*>(value.data()),
                          value.len().to_primitive(),
                          nullptr,
                          &length,
                          nullptr,
                          sodium_base64_VARIANT_URLSAFE_NO_PADDING);
    if (status != 0 || length != Size) {
        return lito::registry::registry_value_failure<array<u8, Size>>(
            rstd::format("{} is not a canonical base64url value", description));
    }
    return Ok(rstd::move(result));
}

auto valid_base64url_character(u8 value) -> bool {
    auto byte = value.to_primitive();
    return (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') ||
           (byte >= '0' && byte <= '9') || byte == '-' || byte == '_';
}

} // namespace

auto lito::registry::Ed25519Signature::parse(ref<str> value)
    -> RegistryValueResult<Ed25519Signature> {
    if (value.len() != usize(86)) {
        return registry_value_failure<Ed25519Signature>(
            "Ed25519 signature must be 64 bytes encoded as base64url without padding"_str);
    }
    for (usize index {}; index < value.len(); ++index) {
        if (! valid_base64url_character(value[index])) {
            return registry_value_failure<Ed25519Signature>(
                rstd::format("Ed25519 signature contains a non-base64url byte at {}", index));
        }
    }
    auto tail = value[value.len() - usize(1)].to_primitive();
    if (tail != 'A' && tail != 'Q' && tail != 'g' && tail != 'w') {
        return registry_value_failure<Ed25519Signature>(
            "Ed25519 signature is not a canonical base64url encoding"_str);
    }
    auto decoded = decode_base64url<64>(value, "Ed25519 signature"_str);
    if (decoded.is_err()) return Err(rstd::move(decoded).unwrap_err());
    return Ok(Ed25519Signature(String::make(value)));
}

auto lito::registry::signing_key_id(const Ed25519PublicKey& public_key)
    -> RegistryValueResult<SigningKeyId> {
    auto decoded = rstd_try(decode_base64url<32>(public_key.base64url(), "Ed25519 public key"_str));
    return Ok(SigningKeyId(lito::crypto::sha256_digest(decoded.as_slice())));
}

auto lito::registry::verify_ed25519(const Ed25519PublicKey& public_key,
                                    const Ed25519Signature& signature,
                                    slice<u8>               message) -> RegistryValueResult<empty> {
    auto key = rstd_try(decode_base64url<32>(public_key.base64url(), "Ed25519 public key"_str));
    auto decoded_signature =
        rstd_try(decode_base64url<64>(signature.base64url(), "Ed25519 signature"_str));
    auto status = crypto_sign_ed25519_verify_detached(
        reinterpret_cast<const unsigned char*>(decoded_signature.as_slice().as_raw_ptr()),
        reinterpret_cast<const unsigned char*>(message.as_raw_ptr()),
        message.len().to_primitive(),
        reinterpret_cast<const unsigned char*>(key.as_slice().as_raw_ptr()));
    if (status != 0) {
        return registry_value_failure<empty>("Registry metadata signature is invalid"_str);
    }
    return Ok(empty {});
}
