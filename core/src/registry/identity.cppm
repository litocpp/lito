export module lito.core:registry.identity;

import rstd;
import :parse.value;
import :registry.error;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::registry
{

class RegistryId : public DefaultInClass<RegistryId, Clone> {
    lito::parse::HttpsUrl value_;

    explicit RegistryId(lito::parse::HttpsUrl value): value_(rstd::move(value)) {}

public:
    RegistryId(const RegistryId&)                = delete;
    RegistryId& operator=(const RegistryId&)     = delete;
    RegistryId(RegistryId&&) noexcept            = default;
    RegistryId& operator=(RegistryId&&) noexcept = default;

    static auto parse(ref<str> value) -> RegistryValueResult<RegistryId>;

    auto as_str() const noexcept -> ref<str> { return value_.as_str(); }
    auto clone() const -> RegistryId { return RegistryId(value_.clone()); }

    friend auto operator==(const RegistryId& left, const RegistryId& right) noexcept -> bool {
        return left.value_ == right.value_;
    }
    friend auto operator<=>(const RegistryId& left, const RegistryId& right) noexcept {
        return left.value_ <=> right.value_;
    }
};

class RegistryPackageName : public DefaultInClass<RegistryPackageName, Clone> {
    String value_;

    explicit RegistryPackageName(String value): value_(rstd::move(value)) {}

public:
    RegistryPackageName(const RegistryPackageName&)                = delete;
    RegistryPackageName& operator=(const RegistryPackageName&)     = delete;
    RegistryPackageName(RegistryPackageName&&) noexcept            = default;
    RegistryPackageName& operator=(RegistryPackageName&&) noexcept = default;

    static auto parse(ref<str> value) -> RegistryValueResult<RegistryPackageName>;

    auto as_str() const noexcept -> ref<str> { return value_.as_str(); }
    auto collision_key() const -> String;
    auto clone() const -> RegistryPackageName { return RegistryPackageName(value_.clone()); }

    friend auto operator==(const RegistryPackageName& left,
                           const RegistryPackageName& right) noexcept -> bool {
        return left.value_ == right.value_;
    }
    friend auto operator<=>(const RegistryPackageName& left,
                            const RegistryPackageName& right) noexcept {
        return left.value_ <=> right.value_;
    }
};

struct RegistryPackageId {
    RegistryId          registry;
    RegistryPackageName name;

    auto clone() const -> RegistryPackageId {
        return RegistryPackageId { .registry = registry.clone(), .name = name.clone() };
    }

    auto operator==(const RegistryPackageId& other) const noexcept -> bool {
        return registry == other.registry && name == other.name;
    }
};

auto registry_package_id_text(const RegistryPackageId& id) -> String;

} // namespace lito::registry

auto lito::registry::RegistryId::parse(ref<str> value) -> RegistryValueResult<RegistryId> {
    auto parsed = lito::parse::HttpsUrl::parse(value);
    if (parsed.is_err()) {
        return registry_value_failure<RegistryId>(
            "registry identity must be a canonical HTTPS origin"_str);
    }
    auto url = rstd::move(parsed).unwrap();
    if (url.url()->path() != "/"_str || url.url()->fragment().is_some() ||
        value.contains("?"_str) || ! value.ends_with("/"_str)) {
        return registry_value_failure<RegistryId>(
            "registry identity must contain only an HTTPS origin and trailing '/'"_str);
    }
    auto authority = url.url()->authority();
    if (authority.contains("@"_str) || authority.ends_with(":443"_str)) {
        return registry_value_failure<RegistryId>(
            "registry identity must not contain credentials or the default port"_str);
    }
    for (auto byte : authority.as_bytes()) {
        const auto ascii = byte.to_primitive();
        if (ascii >= 'A' && ascii <= 'Z') {
            return registry_value_failure<RegistryId>(
                "registry identity authority must use lowercase ASCII"_str);
        }
    }
    return Ok(RegistryId(rstd::move(url)));
}

auto registry_name_is_reserved(ref<str> value) -> bool {
    constexpr ref<str> names[] = {
        "con"_str,  "prn"_str,  "aux"_str,  "nul"_str,  "com1"_str, "com2"_str,
        "com3"_str, "com4"_str, "com5"_str, "com6"_str, "com7"_str, "com8"_str,
        "com9"_str, "lpt1"_str, "lpt2"_str, "lpt3"_str, "lpt4"_str, "lpt5"_str,
        "lpt6"_str, "lpt7"_str, "lpt8"_str, "lpt9"_str,
    };
    for (auto name : names) {
        if (value == name) return true;
    }
    return false;
}

auto lito::registry::RegistryPackageName::parse(ref<str> value)
    -> RegistryValueResult<RegistryPackageName> {
    if (value.is_empty() || value.len() > usize(64)) {
        return registry_value_failure<RegistryPackageName>(
            "registry package name must contain 1 to 64 ASCII bytes"_str);
    }
    for (usize index {}; index < value.len(); ++index) {
        const auto ascii   = value[index].to_primitive();
        const auto allowed = (ascii >= 'a' && ascii <= 'z') || (ascii >= '0' && ascii <= '9') ||
                             ascii == '-' || ascii == '_';
        if (! allowed) {
            return registry_value_failure<RegistryPackageName>(
                rstd::format("registry package name contains an invalid byte at {}", index));
        }
        if ((index == usize {} || index + usize(1) == value.len()) &&
            ! ((ascii >= 'a' && ascii <= 'z') || (ascii >= '0' && ascii <= '9'))) {
            return registry_value_failure<RegistryPackageName>(
                "registry package name must start and end with an ASCII letter or digit"_str);
        }
    }
    if (registry_name_is_reserved(value)) {
        return registry_value_failure<RegistryPackageName>(
            "registry package name is reserved by portable filesystems"_str);
    }
    return Ok(RegistryPackageName(String::make(value)));
}

auto lito::registry::RegistryPackageName::collision_key() const -> String {
    auto result = String::make();
    result.reserve(value_.len());
    for (auto byte : value_.as_str().as_bytes()) {
        result.push_ascii(byte == u8('_') ? '-' : char(byte.to_primitive()));
    }
    return result;
}

auto lito::registry::registry_package_id_text(const RegistryPackageId& id) -> String {
    return rstd::format("{}{}", id.registry.as_str(), id.name.as_str());
}
