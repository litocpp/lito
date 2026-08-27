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

struct RegistryDataEndpoints {
    RegistryEndpointTemplate index;
    RegistryEndpointTemplate blob;

    auto clone() const -> RegistryDataEndpoints {
        return RegistryDataEndpoints {
            .index = index.clone(),
            .blob  = blob.clone(),
        };
    }
};

} // namespace lito::registry

auto registry_endpoint_placeholder(lito::registry::RegistryEndpointKind kind) -> ref<str> {
    using lito::registry::RegistryEndpointKind;
    switch (kind) {
    case RegistryEndpointKind::Index: return "{package}"_str;
    case RegistryEndpointKind::Blob: return "{checksum}"_str;
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
