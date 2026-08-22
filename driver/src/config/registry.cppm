module;
#include <rstd/macro.hpp>

export module lito.driver:config.registry;

import rstd;
import rstd.toml;
import lito.core;
import lito.system;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;
using namespace rstd::literals;
using Toml  = rstd::toml::Value;
using Table = rstd::toml::Table;

export namespace lito::config
{

struct NamedRegistryConfig {
    String                                        name;
    lito::registry::RegistryId                    identity;
    lito::registry::RegistryDataEndpoints         endpoints;
    lito::registry::RegistryFixedEndpoint         api;
    lito::registry::Ed25519PublicKey              trusted_public_key;
    Option<lito::registry::RegistryDataEndpoints> mirror;

    auto clone() const -> NamedRegistryConfig {
        return NamedRegistryConfig {
            .name               = name.clone(),
            .identity           = identity.clone(),
            .endpoints          = endpoints.clone(),
            .api                = api.clone(),
            .trusted_public_key = trusted_public_key.clone(),
            .mirror = mirror.is_some() ? Some(mirror->clone())
                                       : Option<lito::registry::RegistryDataEndpoints> {},
        };
    }

    auto effective_endpoints() const noexcept -> ref<lito::registry::RegistryDataEndpoints> {
        if (mirror.is_some()) {
            return ref<lito::registry::RegistryDataEndpoints>::from_raw_parts(&*mirror);
        }
        return ref<lito::registry::RegistryDataEndpoints>::from_raw_parts(&endpoints);
    }
};

class LitoBootstrapConfig {
    Vec<NamedRegistryConfig> registries_;
    Option<String>           default_registry_;

public:
    LitoBootstrapConfig(Vec<NamedRegistryConfig> registries, Option<String> default_registry)
        : registries_(rstd::move(registries)), default_registry_(rstd::move(default_registry)) {}

    auto registries() const noexcept -> ref<Vec<NamedRegistryConfig>> {
        return ref<Vec<NamedRegistryConfig>>::from_raw_parts(&registries_);
    }
    auto default_registry_name() const noexcept -> Option<ref<str>> {
        if (default_registry_.is_none()) return None();
        return Some(default_registry_->as_str());
    }
    auto registry(ref<str> name) const noexcept -> Option<ref<NamedRegistryConfig>> {
        for (const auto& registry : registries_) {
            if (registry.name.as_str() == name) {
                return Some(ref<NamedRegistryConfig>::from_raw_parts(&registry));
            }
        }
        return None();
    }
    auto default_registry() const noexcept -> Option<ref<NamedRegistryConfig>> {
        if (default_registry_.is_none()) return None();
        return registry(default_registry_->as_str());
    }
};

struct RegistryBootstrapConfigRequest {
    ConfigLoadMode           mode { ConfigLoadMode::Enabled };
    Option<PathBuf>          path;
    Vec<NamedRegistryConfig> registry_overrides;
    Option<String>           default_registry;
};

class RegistryBearerToken {
    String value_;

public:
    explicit RegistryBearerToken(String value): value_(rstd::move(value)) {}

    auto authorization_header() const -> String { return rstd::format("Bearer {}", value_); }
};

struct NamedRegistryCredential {
    String              registry;
    RegistryBearerToken token;
};

class RegistryCredentials {
    Vec<NamedRegistryCredential> entries_;

public:
    explicit RegistryCredentials(Vec<NamedRegistryCredential> entries)
        : entries_(rstd::move(entries)) {}

    auto token(ref<str> registry) const noexcept -> Option<ref<RegistryBearerToken>> {
        for (const auto& entry : entries_) {
            if (entry.registry.as_str() == registry) {
                return Some(ref<RegistryBearerToken>::from_raw_parts(&entry.token));
            }
        }
        return None();
    }
};

auto registry_bootstrap_config_path() -> ConfigResult<PathBuf>;
auto registry_credentials_path() -> ConfigResult<PathBuf>;
auto load_registry_bootstrap_config(RegistryBootstrapConfigRequest request = {})
    -> ConfigResult<LitoBootstrapConfig>;
auto load_registry_credentials(Option<PathBuf> path = {}) -> ConfigResult<RegistryCredentials>;

} // namespace lito::config

namespace
{

template<typename T>
auto registry_config_failure(String message) -> lito::config::ConfigResult<T> {
    return Err(lito::config::ConfigError::Schema(rstd::move(message)));
}

template<typename T>
auto registry_config_failure(ref<str> message) -> lito::config::ConfigResult<T> {
    return Err(lito::config::ConfigError::Schema(String::make(message)));
}

template<typename T>
auto registry_config_io_failure(ref<str>               operation,
                                ref<rstd::path::Path>  path,
                                rstd::io::error::Error source) -> lito::config::ConfigResult<T> {
    return Err(lito::config::ConfigError::Io(
        String::make(operation), PathBuf::from(path), rstd::move(source)));
}

auto valid_registry_config_name(ref<str> value) -> bool {
    if (value.is_empty() || value.len() > usize(64)) return false;
    for (auto byte : value.as_bytes()) {
        auto character = byte.to_primitive();
        if ((character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') ||
            character == '-' || character == '_') {
            continue;
        }
        return false;
    }
    return true;
}

auto reject_unknown(const Table& value, ref<str> context, initializer_list<ref<str>> allowed)
    -> lito::config::ConfigResult<empty> {
    auto keys = value.keys();
    for (auto key : keys) {
        auto known = false;
        for (auto candidate : allowed) {
            if ((*key).as_str() == candidate) {
                known = true;
                break;
            }
        }
        if (! known) {
            return registry_config_failure<empty>(
                rstd::format("{} contains unknown field '{}'", context, (*key).as_str()));
        }
    }
    return Ok(empty {});
}

auto table(const Toml& value, ref<str> context) -> lito::config::ConfigResult<ref<Table>> {
    auto result = value.as_table();
    if (result.is_none()) {
        return registry_config_failure<ref<Table>>(rstd::format("{} must be a table", context));
    }
    return Ok(*result);
}

auto required_string(const Toml& value, ref<str> key, ref<str> context)
    -> lito::config::ConfigResult<ref<str>> {
    auto member = value.get(key);
    if (member.is_none()) {
        return registry_config_failure<ref<str>>(rstd::format("{}.{} is required", context, key));
    }
    auto text = (**member).as_str();
    if (text.is_none() || text->is_empty()) {
        return registry_config_failure<ref<str>>(
            rstd::format("{}.{} must be a non-empty string", context, key));
    }
    return Ok(*text);
}

template<typename T, typename Parser>
auto parse_registry_value(ref<str> value, ref<str> context, Parser parser)
    -> lito::config::ConfigResult<T> {
    auto parsed = parser(value);
    if (parsed.is_err()) {
        return registry_config_failure<T>(
            rstd::format("{}: {}", context, rstd::move(parsed).unwrap_err()));
    }
    return Ok(rstd::move(parsed).unwrap());
}

auto parse_fixed_endpoint(const Toml& value, ref<str> key, ref<str> context)
    -> lito::config::ConfigResult<lito::registry::RegistryFixedEndpoint> {
    auto field = rstd::format("{}.{}", context, key);
    return parse_registry_value<lito::registry::RegistryFixedEndpoint>(
        rstd_try(required_string(value, key, context)), field.as_str(), [](ref<str> text) {
            return lito::registry::RegistryFixedEndpoint::parse(text);
        });
}

auto parse_endpoint_template(const Toml&                          value,
                             ref<str>                             key,
                             ref<str>                             context,
                             lito::registry::RegistryEndpointKind kind)
    -> lito::config::ConfigResult<lito::registry::RegistryEndpointTemplate> {
    auto field = rstd::format("{}.{}", context, key);
    return parse_registry_value<lito::registry::RegistryEndpointTemplate>(
        rstd_try(required_string(value, key, context)), field.as_str(), [kind](ref<str> text) {
            return lito::registry::RegistryEndpointTemplate::parse(text, kind);
        });
}

auto parse_data_endpoints(const Toml& value, ref<str> context)
    -> lito::config::ConfigResult<lito::registry::RegistryDataEndpoints> {
    return Ok(lito::registry::RegistryDataEndpoints {
        .config     = rstd_try(parse_fixed_endpoint(value, "config"_str, context)),
        .index      = rstd_try(parse_endpoint_template(
            value, "index"_str, context, lito::registry::RegistryEndpointKind::Index)),
        .blob       = rstd_try(parse_endpoint_template(
            value, "blob"_str, context, lito::registry::RegistryEndpointKind::Blob)),
        .release    = rstd_try(parse_endpoint_template(
            value, "release"_str, context, lito::registry::RegistryEndpointKind::Release)),
        .event      = rstd_try(parse_endpoint_template(
            value, "event"_str, context, lito::registry::RegistryEndpointKind::Event)),
        .checkpoint = rstd_try(parse_fixed_endpoint(value, "checkpoint"_str, context)),
    });
}

auto parse_named_registry(ref<str> name, const Toml& value)
    -> lito::config::ConfigResult<lito::config::NamedRegistryConfig> {
    if (! valid_registry_config_name(name)) {
        return registry_config_failure<lito::config::NamedRegistryConfig>(
            rstd::format("registry config name '{}' must use 1 to 64 lowercase ASCII letters, "
                         "digits, '-' or '_'",
                         name));
    }
    auto context     = rstd::format("registries.{}", name);
    auto value_table = rstd_try(table(value, context.as_str()));
    rstd_try(reject_unknown(*value_table,
                            context.as_str(),
                            { "identity"_str,
                              "config"_str,
                              "index"_str,
                              "blob"_str,
                              "release"_str,
                              "event"_str,
                              "checkpoint"_str,
                              "api"_str,
                              "trusted-public-key"_str,
                              "mirror"_str }));
    auto identity_field = rstd::format("{}.identity", context);
    auto identity       = parse_registry_value<lito::registry::RegistryId>(
        rstd_try(required_string(value, "identity"_str, context.as_str())),
        identity_field.as_str(),
        [](ref<str> text) {
            return lito::registry::RegistryId::parse(text);
        });
    auto key_field = rstd::format("{}.trusted-public-key", context);
    auto key       = parse_registry_value<lito::registry::Ed25519PublicKey>(
        rstd_try(required_string(value, "trusted-public-key"_str, context.as_str())),
        key_field.as_str(),
        [](ref<str> text) {
            return lito::registry::Ed25519PublicKey::parse(text);
        });
    auto mirror       = Option<lito::registry::RegistryDataEndpoints> {};
    auto mirror_value = value.get("mirror"_str);
    if (mirror_value.is_some()) {
        auto mirror_context = rstd::format("{}.mirror", context);
        auto mirror_table   = rstd_try(table(**mirror_value, mirror_context.as_str()));
        rstd_try(reject_unknown(*mirror_table,
                                mirror_context.as_str(),
                                { "config"_str,
                                  "index"_str,
                                  "blob"_str,
                                  "release"_str,
                                  "event"_str,
                                  "checkpoint"_str }));
        mirror = Some(rstd_try(parse_data_endpoints(**mirror_value, mirror_context.as_str())));
    }
    return Ok(lito::config::NamedRegistryConfig {
        .name               = String::make(name),
        .identity           = rstd_try(rstd::move(identity)),
        .endpoints          = rstd_try(parse_data_endpoints(value, context.as_str())),
        .api                = rstd_try(parse_fixed_endpoint(value, "api"_str, context.as_str())),
        .trusted_public_key = rstd_try(rstd::move(key)),
        .mirror             = rstd::move(mirror),
    });
}

auto parse_bootstrap_document(const Toml& document)
    -> lito::config::ConfigResult<lito::config::LitoBootstrapConfig> {
    auto root = rstd_try(table(document, "registry bootstrap config"_str));
    rstd_try(reject_unknown(
        *root, "registry bootstrap config"_str, { "default"_str, "registries"_str }));
    auto registries     = Vec<lito::config::NamedRegistryConfig>::make();
    auto registry_value = document.get("registries"_str);
    if (registry_value.is_some()) {
        auto registry_table = rstd_try(table(**registry_value, "registries"_str));
        auto keys           = registry_table->keys();
        for (auto key : keys) {
            auto value = registry_table->get((*key).as_str()).unwrap();
            registries.push(rstd_try(parse_named_registry((*key).as_str(), *value)));
        }
    }
    auto default_registry = Option<String> {};
    auto default_value    = document.get("default"_str);
    if (default_value.is_some()) {
        auto text = (**default_value).as_str();
        if (text.is_none() || ! valid_registry_config_name(*text)) {
            return registry_config_failure<lito::config::LitoBootstrapConfig>(
                "registry bootstrap config default must be a valid registry config name"_str);
        }
        default_registry = Some(String::make(*text));
    }
    return Ok(
        lito::config::LitoBootstrapConfig(rstd::move(registries), rstd::move(default_registry)));
}

auto read_registry_toml(ref<rstd::path::Path> path, ref<str> description, bool private_file)
    -> lito::config::ConfigResult<Option<Toml>> {
    auto metadata = rstd::fs::symlink_metadata(path);
    if (metadata.is_err()) {
        auto error = rstd::move(metadata).unwrap_err();
        if (error.kind() == rstd::io::error::ErrorKind { rstd::io::error::ErrorKind::NotFound }) {
            return Ok(Option<Toml> {});
        }
        return registry_config_io_failure<Option<Toml>>(
            rstd::format("inspect {}", description).as_str(), path, rstd::move(error));
    }
    if (! metadata->is_file()) {
        return registry_config_failure<Option<Toml>>(
            rstd::format("{} '{}' must be an ordinary file", description, path));
    }
#if ! defined(_WIN32)
    if (private_file && (metadata->permissions().mode() & u32(0077)) != u32 {}) {
        return registry_config_failure<Option<Toml>>(rstd::format(
            "{} '{}' must not be accessible by group or other users", description, path));
    }
#else
    (void)private_file;
#endif
    auto contents = rstd::fs::read_to_string(path);
    if (contents.is_err()) {
        return registry_config_io_failure<Option<Toml>>(
            rstd::format("read {}", description).as_str(), path, rstd::move(contents).unwrap_err());
    }
    auto parsed = rstd::toml::from_str(contents->as_str());
    if (parsed.is_err()) {
        return Err(
            lito::config::ConfigError::Parse(PathBuf::from(path), rstd::move(parsed).unwrap_err()));
    }
    if (! parsed->is_table()) {
        return registry_config_failure<Option<Toml>>(
            rstd::format("{} root must be a table", description));
    }
    return Ok(Some(rstd::move(parsed).unwrap()));
}

auto replace_registry(Vec<lito::config::NamedRegistryConfig>& registries,
                      lito::config::NamedRegistryConfig       replacement) -> void {
    for (auto& registry : registries) {
        if (registry.name == replacement.name) {
            registry = rstd::move(replacement);
            return;
        }
    }
    registries.push(rstd::move(replacement));
}

auto validate_bootstrap_default(const lito::config::LitoBootstrapConfig& config)
    -> lito::config::ConfigResult<empty> {
    auto name = config.default_registry_name();
    if (name.is_some() && config.registry(*name).is_none()) {
        return registry_config_failure<empty>(
            rstd::format("default registry '{}' is not configured", *name));
    }
    return Ok(empty {});
}

auto compiled_bootstrap_config() -> lito::config::ConfigResult<lito::config::LitoBootstrapConfig> {
    auto registries       = Vec<lito::config::NamedRegistryConfig>::make();
    auto default_registry = Option<String> {};
#if defined(LITO_OFFICIAL_REGISTRY_PUBLIC_KEY)
    constexpr auto official = R"toml(default = "official"
[registries.official]
identity = "https://registry.litocpp.org/"
config = "https://registry.litocpp.org/v1/config.json"
index = "https://registry.litocpp.org/v1/index/{package}.json"
blob = "https://registry.litocpp.org/v1/blobs/sha256/{sha256}.tar.zst"
release = "https://registry.litocpp.org/v1/releases/sha256/{sha256}.json"
event = "https://registry.litocpp.org/v1/events/{sequence}.json"
checkpoint = "https://registry.litocpp.org/v1/checkpoint.json"
api = "https://registry.litocpp.org/"
trusted-public-key = ")toml" LITO_OFFICIAL_REGISTRY_PUBLIC_KEY "\"\n";
    auto           parsed   = rstd::toml::from_str(ref<str>::from_c_str(official));
    if (parsed.is_err()) {
        return registry_config_failure<lito::config::LitoBootstrapConfig>(
            "compiled official registry config is invalid"_str);
    }
    return parse_bootstrap_document(*parsed);
#endif
    return Ok(
        lito::config::LitoBootstrapConfig(rstd::move(registries), rstd::move(default_registry)));
}

} // namespace

auto lito::config::registry_bootstrap_config_path() -> ConfigResult<PathBuf> {
    auto root = lito::system::LitoConfigRoot::resolve();
    if (root.is_err()) {
        return Err(ConfigError::Schema(rstd::format("cannot resolve registry bootstrap config: {}",
                                                    rstd::move(root).unwrap_err())));
    }
    return Ok(root->registries());
}

auto lito::config::registry_credentials_path() -> ConfigResult<PathBuf> {
    auto root = lito::system::LitoConfigRoot::resolve();
    if (root.is_err()) {
        return Err(ConfigError::Schema(rstd::format("cannot resolve registry credentials: {}",
                                                    rstd::move(root).unwrap_err())));
    }
    return Ok(root->registry_credentials());
}

auto lito::config::load_registry_bootstrap_config(RegistryBootstrapConfigRequest request)
    -> ConfigResult<LitoBootstrapConfig> {
    auto compiled   = rstd_try(compiled_bootstrap_config());
    auto registries = Vec<NamedRegistryConfig>::make();
    for (const auto& registry : *compiled.registries()) registries.push(registry.clone());
    auto default_registry = Option<String> {};
    auto compiled_default = compiled.default_registry_name();
    if (compiled_default.is_some()) default_registry = Some(String::make(*compiled_default));

    if (request.mode == ConfigLoadMode::Enabled) {
        auto path = request.path.is_some() ? rstd::move(request.path).unwrap()
                                           : rstd_try(registry_bootstrap_config_path());
        auto document =
            rstd_try(read_registry_toml(path.as_path(), "registry bootstrap config"_str, false));
        if (document.is_some()) {
            auto user = rstd_try(parse_bootstrap_document(*document));
            for (const auto& registry : *user.registries())
                replace_registry(registries, registry.clone());
            auto user_default = user.default_registry_name();
            if (user_default.is_some()) default_registry = Some(String::make(*user_default));
        }
    }
    for (auto& registry : request.registry_overrides) {
        replace_registry(registries, rstd::move(registry));
    }
    if (request.default_registry.is_some()) {
        if (! valid_registry_config_name(request.default_registry->as_str())) {
            return registry_config_failure<LitoBootstrapConfig>(
                "invocation default registry is not a valid registry config name"_str);
        }
        default_registry = rstd::move(request.default_registry);
    }
    auto result = LitoBootstrapConfig(rstd::move(registries), rstd::move(default_registry));
    rstd_try(validate_bootstrap_default(result));
    return Ok(rstd::move(result));
}

auto lito::config::load_registry_credentials(Option<PathBuf> requested_path)
    -> ConfigResult<RegistryCredentials> {
    auto path     = requested_path.is_some() ? rstd::move(requested_path).unwrap()
                                             : rstd_try(registry_credentials_path());
    auto document = rstd_try(read_registry_toml(path.as_path(), "registry credentials"_str, true));
    auto entries  = Vec<NamedRegistryCredential>::make();
    if (document.is_none()) return Ok(RegistryCredentials(rstd::move(entries)));
    auto root = rstd_try(table(*document, "registry credentials"_str));
    rstd_try(reject_unknown(*root, "registry credentials"_str, { "registries"_str }));
    auto registries = document->get("registries"_str);
    if (registries.is_none()) return Ok(RegistryCredentials(rstd::move(entries)));
    auto registry_table = rstd_try(table(**registries, "registry credentials.registries"_str));
    auto keys           = registry_table->keys();
    for (auto key : keys) {
        auto name = (*key).as_str();
        if (! valid_registry_config_name(name)) {
            return registry_config_failure<RegistryCredentials>(
                rstd::format("registry credential name '{}' is invalid", name));
        }
        auto value       = registry_table->get(name).unwrap();
        auto context     = rstd::format("registry credentials.registries.{}", name);
        auto value_table = rstd_try(table(*value, context.as_str()));
        rstd_try(reject_unknown(*value_table, context.as_str(), { "token"_str }));
        auto token = rstd_try(required_string(*value, "token"_str, context.as_str()));
        for (auto byte : token.as_bytes()) {
            if (byte.to_primitive() <= 0x20 || byte.to_primitive() == 0x7f) {
                return registry_config_failure<RegistryCredentials>(
                    rstd::format("{}.token must not contain whitespace or control bytes", context));
            }
        }
        entries.push(NamedRegistryCredential {
            .registry = String::make(name),
            .token    = RegistryBearerToken(String::make(token)),
        });
    }
    return Ok(RegistryCredentials(rstd::move(entries)));
}
