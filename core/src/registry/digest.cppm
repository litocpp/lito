export module lito.core:registry.digest;

import rstd;
import lito.crypto;
import :registry.error;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::registry
{

template<typename Tag>
class TypedSha256Digest : public DefaultInClass<TypedSha256Digest<Tag>, Clone> {
    lito::crypto::Sha256Digest value_;

public:
    explicit TypedSha256Digest(lito::crypto::Sha256Digest value): value_(rstd::move(value)) {}

    static auto parse(ref<str> value) -> RegistryValueResult<TypedSha256Digest> {
        constexpr auto prefix = "sha256:"_str;
        if (! value.starts_with(prefix)) {
            return registry_value_failure<TypedSha256Digest>(
                "digest must start with 'sha256:'"_str);
        }
        auto hex = value.get(prefix.len(), value.len()).unwrap();
        for (usize index {}; index < hex.len(); ++index) {
            const auto byte = hex[index].to_primitive();
            if (! ((byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f'))) {
                return registry_value_failure<TypedSha256Digest>(
                    rstd::format("digest contains a non-canonical hexadecimal byte at {}", index));
            }
        }
        auto parsed = lito::crypto::Sha256Digest::parse_hex(hex);
        if (parsed.is_err()) {
            return registry_value_failure<TypedSha256Digest>(
                "digest must contain exactly 64 lowercase hexadecimal digits"_str);
        }
        return Ok(TypedSha256Digest(rstd::move(parsed).unwrap()));
    }

    auto digest() const noexcept -> const lito::crypto::Sha256Digest& { return value_; }
    auto text() const -> String { return rstd::format("sha256:{}", value_.to_hex()); }
    auto clone() const -> TypedSha256Digest { return TypedSha256Digest(value_.clone()); }

    friend auto operator==(const TypedSha256Digest& left, const TypedSha256Digest& right) noexcept
        -> bool {
        return left.value_ == right.value_;
    }
};

struct BlobDigestTag {};
struct SourceDigestTag {};
struct ManifestDigestTag {};
struct ReleaseDigestTag {};
struct SigningKeyIdTag {};

using BlobDigest     = TypedSha256Digest<BlobDigestTag>;
using SourceDigest   = TypedSha256Digest<SourceDigestTag>;
using ManifestDigest = TypedSha256Digest<ManifestDigestTag>;
using ReleaseDigest  = TypedSha256Digest<ReleaseDigestTag>;
using SigningKeyId   = TypedSha256Digest<SigningKeyIdTag>;

} // namespace lito::registry
