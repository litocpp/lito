export module lito.core:registry.config;

import rstd;
import :parse.value;
import :registry.error;
import :registry.identity;
import :registry.version;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::registry
{

class RegistryIndexEndpointTemplate : public DefaultInClass<RegistryIndexEndpointTemplate, Clone> {
    String value_;

    explicit RegistryIndexEndpointTemplate(String value): value_(rstd::move(value)) {}

public:
    static auto parse(ref<str> value) -> RegistryValueResult<RegistryIndexEndpointTemplate>;

    auto as_str() const noexcept -> ref<str> { return value_.as_str(); }
    auto render(const RegistryPackageName& package) const -> String;
    auto clone() const -> RegistryIndexEndpointTemplate {
        return RegistryIndexEndpointTemplate(value_.clone());
    }

    friend auto operator==(const RegistryIndexEndpointTemplate& left,
                           const RegistryIndexEndpointTemplate& right) noexcept -> bool {
        return left.value_ == right.value_;
    }
};

class RegistryDownloadEndpointTemplate
    : public DefaultInClass<RegistryDownloadEndpointTemplate, Clone> {
    String value_;

    explicit RegistryDownloadEndpointTemplate(String value): value_(rstd::move(value)) {}

public:
    static auto parse(ref<str> value) -> RegistryValueResult<RegistryDownloadEndpointTemplate>;

    auto as_str() const noexcept -> ref<str> { return value_.as_str(); }
    auto render(const RegistryPackageName& package, const SemanticVersion& version) const -> String;
    auto clone() const -> RegistryDownloadEndpointTemplate {
        return RegistryDownloadEndpointTemplate(value_.clone());
    }

    friend auto operator==(const RegistryDownloadEndpointTemplate& left,
                           const RegistryDownloadEndpointTemplate& right) noexcept -> bool {
        return left.value_ == right.value_;
    }
};

class RegistryFixedEndpoint : public DefaultInClass<RegistryFixedEndpoint, Clone> {
    lito::parse::FetchUrl value_;

    explicit RegistryFixedEndpoint(lito::parse::FetchUrl value): value_(rstd::move(value)) {}

public:
    static auto parse(ref<str> value) -> RegistryValueResult<RegistryFixedEndpoint>;

    auto as_str() const noexcept -> ref<str> { return value_.as_str(); }
    auto scheme() const noexcept -> ref<str> { return value_.url()->scheme(); }
    auto clone() const -> RegistryFixedEndpoint { return RegistryFixedEndpoint(value_.clone()); }

    friend auto operator==(const RegistryFixedEndpoint& left,
                           const RegistryFixedEndpoint& right) noexcept -> bool {
        return left.value_ == right.value_;
    }
};

struct RegistryDataEndpoints {
    RegistryIndexEndpointTemplate    index;
    RegistryDownloadEndpointTemplate download;

    auto clone() const -> RegistryDataEndpoints {
        return RegistryDataEndpoints {
            .index    = index.clone(),
            .download = download.clone(),
        };
    }
};

} // namespace lito::registry

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

auto valid_registry_endpoint(ref<str> value, String candidate) -> bool {
    if (candidate.as_str().contains("{"_str) || candidate.as_str().contains("}"_str)) return false;
    auto parsed = lito::parse::HttpsUrl::parse(candidate.as_str());
    return parsed.is_ok() && parsed->url()->fragment().is_none() && ! value.contains("?"_str);
}

auto lito::registry::RegistryIndexEndpointTemplate::parse(ref<str> value)
    -> RegistryValueResult<RegistryIndexEndpointTemplate> {
    constexpr auto placeholder = "{package}"_str;
    auto           position    = value.find(placeholder);
    if (position.is_none()) {
        return registry_value_failure<RegistryIndexEndpointTemplate>(rstd::format(
            "registry endpoint must contain exactly one '{}' placeholder", placeholder));
    }
    auto suffix = value.get(*position + placeholder.len(), value.len()).unwrap();
    if (suffix.contains(placeholder)) {
        return registry_value_failure<RegistryIndexEndpointTemplate>(rstd::format(
            "registry endpoint must contain exactly one '{}' placeholder", placeholder));
    }
    auto candidate = replace_registry_endpoint_placeholder(value, placeholder, "value"_str);
    if (! valid_registry_endpoint(value, rstd::move(candidate))) {
        return registry_value_failure<RegistryIndexEndpointTemplate>(
            "registry endpoint must be an absolute HTTPS URL without a fragment"_str);
    }
    return Ok(RegistryIndexEndpointTemplate(String::make(value)));
}

auto lito::registry::RegistryIndexEndpointTemplate::render(const RegistryPackageName& package) const
    -> String {
    return replace_registry_endpoint_placeholder(
        value_.as_str(), "{package}"_str, package.as_str());
}

auto lito::registry::RegistryDownloadEndpointTemplate::parse(ref<str> value)
    -> RegistryValueResult<RegistryDownloadEndpointTemplate> {
    if (! value.contains("{package}"_str) || ! value.contains("{version}"_str)) {
        return registry_value_failure<RegistryDownloadEndpointTemplate>(
            "registry download endpoint must contain '{package}' and '{version}' placeholders"_str);
    }
    auto candidate = replace_registry_endpoint_placeholder(value, "{package}"_str, "package"_str);
    candidate =
        replace_registry_endpoint_placeholder(candidate.as_str(), "{version}"_str, "1.0.0"_str);
    if (! valid_registry_endpoint(value, rstd::move(candidate))) {
        return registry_value_failure<RegistryDownloadEndpointTemplate>(
            "registry download endpoint must be an absolute HTTPS URL without a query, fragment, or unknown placeholder"_str);
    }
    return Ok(RegistryDownloadEndpointTemplate(String::make(value)));
}

auto lito::registry::RegistryDownloadEndpointTemplate::render(const RegistryPackageName& package,
                                                              const SemanticVersion& version) const
    -> String {
    auto rendered =
        replace_registry_endpoint_placeholder(value_.as_str(), "{package}"_str, package.as_str());
    return replace_registry_endpoint_placeholder(
        rendered.as_str(), "{version}"_str, version.text().as_str());
}

auto loopback_api_authority(ref<str> authority) -> bool {
    constexpr ref<str> hosts[] = { "localhost"_str, "127.0.0.1"_str, "[::1]"_str };
    for (auto host : hosts) {
        if (authority == host) return true;
        if (! authority.starts_with(host) || authority.len() <= host.len() ||
            authority[host.len()] != u8(':')) {
            continue;
        }
        auto port = authority.get(host.len() + usize(1), authority.len()).unwrap();
        if (port.is_empty() || port.len() > usize(5)) return false;
        auto value = u32 {};
        for (auto byte : port.as_bytes()) {
            const auto raw = byte.to_primitive();
            if (raw < '0' || raw > '9') return false;
            value = value * u32(10) + u32(raw - '0');
        }
        return value > u32 {} && value <= u32(65535);
    }
    return false;
}

auto lito::registry::RegistryFixedEndpoint::parse(ref<str> value)
    -> RegistryValueResult<RegistryFixedEndpoint> {
    auto parsed = lito::parse::FetchUrl::parse(value);
    if (parsed.is_err() || parsed->url()->fragment().is_some() ||
        (parsed->url()->scheme() != "https"_str &&
         (parsed->url()->scheme() != "http"_str ||
          ! loopback_api_authority(parsed->url()->authority())))) {
        return registry_value_failure<RegistryFixedEndpoint>(
            "registry API endpoint must be HTTPS, except HTTP on a loopback address"_str);
    }
    return Ok(RegistryFixedEndpoint(rstd::move(parsed).unwrap()));
}
