export module lito.core:registry.config;

import rstd;
import :parse.value;
import :registry.error;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::registry
{

enum class RegistryEndpointKind
{
    Index,
    Blob,
    Release,
    Event,
};

class RegistryEndpointTemplate : public DefaultInClass<RegistryEndpointTemplate, Clone> {
    String               value_;
    RegistryEndpointKind kind_ { RegistryEndpointKind::Index };

    RegistryEndpointTemplate(String value, RegistryEndpointKind kind)
        : value_(rstd::move(value)), kind_(kind) {}

public:
    static auto parse(ref<str> value, RegistryEndpointKind kind)
        -> RegistryValueResult<RegistryEndpointTemplate>;

    auto as_str() const noexcept -> ref<str> { return value_.as_str(); }
    auto kind() const noexcept -> RegistryEndpointKind { return kind_; }
    auto render(ref<str> value) const -> String;
    auto clone() const -> RegistryEndpointTemplate {
        return RegistryEndpointTemplate(value_.clone(), kind_);
    }

    friend auto operator==(const RegistryEndpointTemplate& left,
                           const RegistryEndpointTemplate& right) noexcept -> bool {
        return left.kind_ == right.kind_ && left.value_ == right.value_;
    }
};

class RegistryFixedEndpoint : public DefaultInClass<RegistryFixedEndpoint, Clone> {
    lito::parse::HttpsUrl value_;

    explicit RegistryFixedEndpoint(lito::parse::HttpsUrl value): value_(rstd::move(value)) {}

public:
    static auto parse(ref<str> value) -> RegistryValueResult<RegistryFixedEndpoint>;

    auto as_str() const noexcept -> ref<str> { return value_.as_str(); }
    auto clone() const -> RegistryFixedEndpoint { return RegistryFixedEndpoint(value_.clone()); }

    friend auto operator==(const RegistryFixedEndpoint& left,
                           const RegistryFixedEndpoint& right) noexcept -> bool {
        return left.value_ == right.value_;
    }
};

class Ed25519PublicKey : public DefaultInClass<Ed25519PublicKey, Clone> {
    String value_;

    explicit Ed25519PublicKey(String value): value_(rstd::move(value)) {}

public:
    static auto parse(ref<str> value) -> RegistryValueResult<Ed25519PublicKey>;

    auto base64url() const noexcept -> ref<str> { return value_.as_str(); }
    auto clone() const -> Ed25519PublicKey { return Ed25519PublicKey(value_.clone()); }

    friend auto operator==(const Ed25519PublicKey& left, const Ed25519PublicKey& right) noexcept
        -> bool {
        return left.value_ == right.value_;
    }
};

struct RegistryDataEndpoints {
    RegistryFixedEndpoint    config;
    RegistryEndpointTemplate index;
    RegistryEndpointTemplate blob;
    RegistryEndpointTemplate release;
    RegistryEndpointTemplate event;
    RegistryFixedEndpoint    checkpoint;

    auto clone() const -> RegistryDataEndpoints {
        return RegistryDataEndpoints {
            .config     = config.clone(),
            .index      = index.clone(),
            .blob       = blob.clone(),
            .release    = release.clone(),
            .event      = event.clone(),
            .checkpoint = checkpoint.clone(),
        };
    }
};

} // namespace lito::registry

auto registry_endpoint_placeholder(lito::registry::RegistryEndpointKind kind) -> ref<str> {
    using lito::registry::RegistryEndpointKind;
    switch (kind) {
    case RegistryEndpointKind::Index: return "{package}"_str;
    case RegistryEndpointKind::Blob: return "{sha256}"_str;
    case RegistryEndpointKind::Release: return "{sha256}"_str;
    case RegistryEndpointKind::Event: return "{sequence}"_str;
    }
    return ""_str;
}

auto replace_registry_endpoint_placeholder(ref<str> input,
                                           ref<str> placeholder,
                                           ref<str> replacement) -> String {
    auto output = String::make();
    auto rest   = input;
    while (true) {
        auto position = rest.find(placeholder);
        if (position.is_none()) {
            output.push_str(rest);
            return output;
        }
        output.push_str(rest.get(usize {}, *position).unwrap());
        output.push_str(replacement);
        rest = rest.get(*position + placeholder.len(), rest.len()).unwrap();
    }
}

auto lito::registry::RegistryEndpointTemplate::parse(ref<str> value, RegistryEndpointKind kind)
    -> RegistryValueResult<RegistryEndpointTemplate> {
    auto placeholder = registry_endpoint_placeholder(kind);
    auto position    = value.find(placeholder);
    if (position.is_none()) {
        return registry_value_failure<RegistryEndpointTemplate>(rstd::format(
            "registry endpoint must contain exactly one '{}' placeholder", placeholder));
    }
    auto suffix = value.get(*position + placeholder.len(), value.len()).unwrap();
    if (suffix.contains(placeholder)) {
        return registry_value_failure<RegistryEndpointTemplate>(rstd::format(
            "registry endpoint must contain exactly one '{}' placeholder", placeholder));
    }
    auto candidate = replace_registry_endpoint_placeholder(value, placeholder, "value"_str);
    if (candidate.as_str().contains("{"_str) || candidate.as_str().contains("}"_str)) {
        return registry_value_failure<RegistryEndpointTemplate>(
            "registry endpoint contains an unknown placeholder"_str);
    }
    auto parsed = lito::parse::HttpsUrl::parse(candidate.as_str());
    if (parsed.is_err() || parsed->url()->fragment().is_some() || value.contains("?"_str)) {
        return registry_value_failure<RegistryEndpointTemplate>(
            "registry endpoint must be an absolute HTTPS URL without a fragment"_str);
    }
    return Ok(RegistryEndpointTemplate(String::make(value), kind));
}

auto lito::registry::RegistryEndpointTemplate::render(ref<str> value) const -> String {
    return replace_registry_endpoint_placeholder(
        value_.as_str(), registry_endpoint_placeholder(kind_), value);
}

auto lito::registry::RegistryFixedEndpoint::parse(ref<str> value)
    -> RegistryValueResult<RegistryFixedEndpoint> {
    auto parsed = lito::parse::HttpsUrl::parse(value);
    if (parsed.is_err() || parsed->url()->fragment().is_some()) {
        return registry_value_failure<RegistryFixedEndpoint>(
            "registry endpoint must be an absolute HTTPS URL without a fragment"_str);
    }
    return Ok(RegistryFixedEndpoint(rstd::move(parsed).unwrap()));
}

auto lito::registry::Ed25519PublicKey::parse(ref<str> value)
    -> RegistryValueResult<Ed25519PublicKey> {
    if (value.len() != usize(43)) {
        return registry_value_failure<Ed25519PublicKey>(
            "Ed25519 public key must be 32 bytes encoded as base64url without padding"_str);
    }
    for (usize index {}; index < value.len(); ++index) {
        auto byte    = value[index].to_primitive();
        auto allowed = (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') ||
                       (byte >= '0' && byte <= '9') || byte == '-' || byte == '_';
        if (! allowed) {
            return registry_value_failure<Ed25519PublicKey>(
                rstd::format("Ed25519 public key contains a non-base64url byte at {}", index));
        }
    }
    auto tail           = value[value.len() - usize(1)].to_primitive();
    auto canonical_tail = tail == 'A' || tail == 'E' || tail == 'I' || tail == 'M' || tail == 'Q' ||
                          tail == 'U' || tail == 'Y' || tail == 'c' || tail == 'g' || tail == 'k' ||
                          tail == 'o' || tail == 's' || tail == 'w' || tail == '0' || tail == '4' ||
                          tail == '8';
    if (! canonical_tail) {
        return registry_value_failure<Ed25519PublicKey>(
            "Ed25519 public key is not a canonical base64url encoding"_str);
    }
    return Ok(Ed25519PublicKey(String::make(value)));
}
