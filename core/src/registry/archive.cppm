export module lito.core:registry.archive;

import rstd;
import :parse.value;
import :registry.error;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::registry
{

class RegistryBlobSize : public DefaultInClass<RegistryBlobSize, Clone> {
    u64 value_ {};

public:
    RegistryBlobSize() = default;
    explicit RegistryBlobSize(u64 value): value_(value) {}

    static auto parse(ref<str> value) -> RegistryValueResult<RegistryBlobSize> {
        auto parsed = lito::parse::parse_canonical_u64_decimal(value);
        if (parsed.is_err()) {
            return registry_value_failure<RegistryBlobSize>(
                "blob size must be a canonical unsigned decimal string"_str);
        }
        return Ok(RegistryBlobSize(*parsed));
    }

    auto value() const noexcept -> u64 { return value_; }
    auto text() const -> String { return rstd::format("{}", value_); }
    auto clone() const -> RegistryBlobSize { return RegistryBlobSize(value_); }

    friend auto operator==(const RegistryBlobSize& left, const RegistryBlobSize& right) noexcept
        -> bool {
        return left.value_ == right.value_;
    }
};

class RegistryArchiveFormat : public DefaultInClass<RegistryArchiveFormat, Clone> {
public:
    static constexpr auto TAR_ZSTD_V1 = "lito.package.tar-zstd.v1"_str;

    static auto parse(ref<str> value) -> RegistryValueResult<RegistryArchiveFormat> {
        if (value != TAR_ZSTD_V1) {
            return registry_value_failure<RegistryArchiveFormat>(
                "archive format is not supported by this Lito"_str);
        }
        return Ok(RegistryArchiveFormat {});
    }

    auto as_str() const noexcept -> ref<str> { return TAR_ZSTD_V1; }
    auto clone() const -> RegistryArchiveFormat { return RegistryArchiveFormat {}; }

    friend auto operator==(const RegistryArchiveFormat&, const RegistryArchiveFormat&) noexcept
        -> bool {
        return true;
    }
};

} // namespace lito::registry
