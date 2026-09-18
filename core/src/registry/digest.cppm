export module lito.core:registry.digest;

import rstd;
import licrypto;
import :registry.error;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::registry
{

class PackageChecksum : public DefaultInClass<PackageChecksum, Clone> {
    licrypto::Sha256Digest value_;

public:
    explicit PackageChecksum(licrypto::Sha256Digest value): value_(rstd::move(value)) {}

    static auto parse(ref<str> value) -> RegistryValueResult<PackageChecksum> {
        if (value.len() != usize(64)) {
            return Err(RegistryValueError::Message(
                "checksum must contain exactly 64 lowercase hexadecimal digits"_Str));
        }
        for (usize index {}; index < value.len(); ++index) {
            const auto byte = value[index].to_primitive();
            if (! ((byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f'))) {
                return Err(RegistryValueError::Message(rstd::format(
                    "checksum contains a non-canonical hexadecimal byte at {}", index)));
            }
        }
        auto parsed = licrypto::Sha256Digest::parse_hex(value);
        if (parsed.is_err()) {
            return Err(RegistryValueError::Message(
                "checksum must contain exactly 64 lowercase hexadecimal digits"_Str));
        }
        return Ok(PackageChecksum(rstd::move(parsed).unwrap()));
    }

    auto digest() const noexcept -> const licrypto::Sha256Digest& { return value_; }
    auto text() const -> String { return value_.to_hex(); }
    auto clone() const -> PackageChecksum { return PackageChecksum(value_.clone()); }

    friend auto operator==(const PackageChecksum& left, const PackageChecksum& right) noexcept
        -> bool {
        return left.value_ == right.value_;
    }
};

} // namespace lito::registry
