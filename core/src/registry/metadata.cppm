module;
#include <rstd/enum.hpp>
#include <rstd/macro.hpp>

export module lito.core:registry.metadata;

import rstd;
import lito.crypto;
import rstd.json;
import :dependency.visibility;
import :parse.value;
import :registry.archive;
import :registry.canonical;
import :registry.config;
import :registry.crypto;
import :registry.digest;
import :registry.error;
import :registry.identity;
import :registry.version;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::registry
{

class RegistryTimestamp : public DefaultInClass<RegistryTimestamp, Clone> {
    String value_;

    explicit RegistryTimestamp(String value): value_(rstd::move(value)) {}

public:
    static auto parse(ref<str> value) -> RegistryValueResult<RegistryTimestamp>;

    auto as_str() const noexcept -> ref<str> { return value_.as_str(); }
    auto clone() const -> RegistryTimestamp { return RegistryTimestamp(value_.clone()); }
};

enum class RegistryDependencyKind
{
    Normal,
    Development,
    Runtime,
};

struct RegistryDependencyProjection {
    String                                 alias;
    RegistryPackageId                      package;
    VersionRequirement                     requirement;
    RegistryDependencyKind                 kind { RegistryDependencyKind::Normal };
    lito::dependency::DependencyVisibility visibility {
        lito::dependency::DependencyVisibility::Private
    };
    Vec<String> features;
    bool        default_features { true };

    auto clone() const -> RegistryDependencyProjection;
};

struct RegistryBlobProjection {
    BlobDigest            digest;
    RegistryBlobSize      size;
    RegistryArchiveFormat format;

    auto clone() const -> RegistryBlobProjection {
        return RegistryBlobProjection {
            .digest = digest.clone(),
            .size   = size.clone(),
            .format = format.clone(),
        };
    }
};

struct RegistryReleaseProjection {
    SemanticVersion                   version;
    ReleaseDigest                     release;
    SourceDigest                      source;
    ManifestDigest                    manifest;
    RegistryBlobProjection            blob;
    Vec<RegistryDependencyProjection> dependencies;
    bool                              yanked {};
    Option<String>                    deprecated;
    RegistryTimestamp                 published_at;

    auto clone() const -> RegistryReleaseProjection;
};

class VerifiedRegistryRelease : public DefaultInClass<VerifiedRegistryRelease, Clone> {
    RegistryPackageId         package_;
    RegistryReleaseProjection release_;
    SigningKeyId              verified_key_;
    String                    canonical_signed_;

    VerifiedRegistryRelease(RegistryPackageId         package,
                            RegistryReleaseProjection release,
                            SigningKeyId              verified_key,
                            String                    canonical_signed)
        : package_(rstd::move(package)),
          release_(rstd::move(release)),
          verified_key_(rstd::move(verified_key)),
          canonical_signed_(rstd::move(canonical_signed)) {}

    friend auto parse_verified_registry_release(slice<u8>,
                                                const RegistryPackageId&,
                                                const ReleaseDigest&,
                                                const Ed25519PublicKey&)
        -> RegistryValueResult<VerifiedRegistryRelease>;

public:
    auto package() const noexcept -> const RegistryPackageId& { return package_; }
    auto release() const noexcept -> const RegistryReleaseProjection& { return release_; }
    auto verified_key() const noexcept -> const SigningKeyId& { return verified_key_; }
    auto canonical_signed() const noexcept -> ref<str> { return canonical_signed_.as_str(); }
    auto clone() const -> VerifiedRegistryRelease {
        return VerifiedRegistryRelease(
            package_.clone(), release_.clone(), verified_key_.clone(), canonical_signed_.clone());
    }
};

struct RegistryTagProjection {
    String          name;
    SemanticVersion version;

    auto clone() const -> RegistryTagProjection {
        return RegistryTagProjection { .name = name.clone(), .version = version.clone() };
    }
};

class VerifiedPackageIndex : public DefaultInClass<VerifiedPackageIndex, Clone> {
    RegistryPackageId              package_;
    u64                            revision_ {};
    u64                            sequence_ {};
    Vec<RegistryReleaseProjection> releases_;
    Vec<RegistryTagProjection>     tags_;
    SigningKeyId                   verified_key_;
    String                         canonical_signed_;

    VerifiedPackageIndex(RegistryPackageId              package,
                         u64                            revision,
                         u64                            sequence,
                         Vec<RegistryReleaseProjection> releases,
                         Vec<RegistryTagProjection>     tags,
                         SigningKeyId                   verified_key,
                         String                         canonical_signed)
        : package_(rstd::move(package)),
          revision_(revision),
          sequence_(sequence),
          releases_(rstd::move(releases)),
          tags_(rstd::move(tags)),
          verified_key_(rstd::move(verified_key)),
          canonical_signed_(rstd::move(canonical_signed)) {}

    friend auto
    parse_verified_package_index(slice<u8>, const RegistryPackageId&, const Ed25519PublicKey&)
        -> RegistryValueResult<VerifiedPackageIndex>;

public:
    auto package() const noexcept -> const RegistryPackageId& { return package_; }
    auto revision() const noexcept -> u64 { return revision_; }
    auto sequence() const noexcept -> u64 { return sequence_; }
    auto releases() const noexcept -> slice<RegistryReleaseProjection> {
        return releases_.as_slice();
    }
    auto tags() const noexcept -> slice<RegistryTagProjection> { return tags_.as_slice(); }
    auto verified_key() const noexcept -> const SigningKeyId& { return verified_key_; }
    auto canonical_signed() const noexcept -> ref<str> { return canonical_signed_.as_str(); }
    auto clone() const -> VerifiedPackageIndex;
};

auto parse_verified_package_index(slice<u8>                input,
                                  const RegistryPackageId& expected,
                                  const Ed25519PublicKey&  trusted_key)
    -> RegistryValueResult<VerifiedPackageIndex>;
auto parse_verified_registry_release(slice<u8>                input,
                                     const RegistryPackageId& expected_package,
                                     const ReleaseDigest&     expected_release,
                                     const Ed25519PublicKey&  trusted_key)
    -> RegistryValueResult<VerifiedRegistryRelease>;
auto registry_immutable_release_matches(const RegistryReleaseProjection& left,
                                        const RegistryReleaseProjection& right) noexcept -> bool;

} // namespace lito::registry

namespace
{

using Json    = rstd::json::Value;
using JsonMap = rstd::json::Map;
using lito::registry::RegistryValueResult;

template<typename T>
auto metadata_failure(ref<str> context, ref<str> message) -> RegistryValueResult<T> {
    return lito::registry::registry_value_failure<T>(rstd::format("{} {}", context, message));
}

template<typename T>
auto metadata_failure(ref<str> context, String message) -> RegistryValueResult<T> {
    return lito::registry::registry_value_failure<T>(
        rstd::format("{} {}", context, message.as_str()));
}

auto json_object(const Json& value, ref<str> context) -> RegistryValueResult<ref<JsonMap>> {
    auto object = value.as_object();
    if (object.is_none()) return metadata_failure<ref<JsonMap>>(context, "must be an object"_str);
    return Ok(*object);
}

auto json_array(const Json& value, ref<str> context)
    -> RegistryValueResult<ref<rstd::json::Array>> {
    auto array = value.as_array();
    if (array.is_none())
        return metadata_failure<ref<rstd::json::Array>>(context, "must be an array"_str);
    return Ok(*array);
}

auto required_member(const Json& value, ref<str> key, ref<str> context)
    -> RegistryValueResult<ref<Json>> {
    auto object = rstd_try(json_object(value, context));
    auto member = object->get(key);
    if (member.is_none()) {
        return metadata_failure<ref<Json>>(context, rstd::format("is missing '{}'", key));
    }
    return Ok(*member);
}

auto required_string(const Json& value, ref<str> key, ref<str> context)
    -> RegistryValueResult<ref<str>> {
    auto member = rstd_try(required_member(value, key, context));
    auto text   = member->as_str();
    if (text.is_none()) {
        return metadata_failure<ref<str>>(context,
                                          rstd::format("field '{}' must be a string", key));
    }
    return Ok(*text);
}

auto required_bool(const Json& value, ref<str> key, ref<str> context) -> RegistryValueResult<bool> {
    auto member  = rstd_try(required_member(value, key, context));
    auto boolean = member->as_bool();
    if (boolean.is_none()) {
        return metadata_failure<bool>(context, rstd::format("field '{}' must be a boolean", key));
    }
    return Ok(*boolean);
}

auto known_field(ref<str> field, initializer_list<ref<str>> allowed) -> bool {
    for (auto candidate : allowed) {
        if (field == candidate) return true;
    }
    return false;
}

auto reject_unknown(const Json& value, ref<str> context, initializer_list<ref<str>> allowed)
    -> RegistryValueResult<empty> {
    auto object = rstd_try(json_object(value, context));
    auto keys   = object->keys();
    for (auto key : keys) {
        if (! known_field((*key).as_str(), allowed)) {
            return metadata_failure<empty>(
                context, rstd::format("contains unknown field '{}'", (*key).as_str()));
        }
    }
    return Ok(empty {});
}

auto valid_local_name(ref<str> value) -> bool {
    if (value.is_empty()) return false;
    for (auto byte : value.as_bytes()) {
        auto ascii = byte.to_primitive();
        if ((ascii >= 'a' && ascii <= 'z') || (ascii >= 'A' && ascii <= 'Z') ||
            (ascii >= '0' && ascii <= '9') || ascii == '-' || ascii == '_') {
            continue;
        }
        return false;
    }
    return true;
}

auto parse_kind(ref<str> value, ref<str> context)
    -> RegistryValueResult<lito::registry::RegistryDependencyKind> {
    using lito::registry::RegistryDependencyKind;
    if (value == "normal"_str) return Ok(RegistryDependencyKind::Normal);
    if (value == "development"_str) return Ok(RegistryDependencyKind::Development);
    if (value == "runtime"_str) return Ok(RegistryDependencyKind::Runtime);
    return metadata_failure<RegistryDependencyKind>(context,
                                                    "has an unsupported dependency kind"_str);
}

auto parse_visibility(ref<str> value, ref<str> context)
    -> RegistryValueResult<lito::dependency::DependencyVisibility> {
    using lito::dependency::DependencyVisibility;
    if (value == "public"_str) return Ok(DependencyVisibility::Public);
    if (value == "private"_str) return Ok(DependencyVisibility::Private);
    if (value == "link"_str) return Ok(DependencyVisibility::LinkOnly);
    return metadata_failure<DependencyVisibility>(context, "has an unsupported visibility"_str);
}

auto parse_features(const Json& value, ref<str> context) -> RegistryValueResult<Vec<String>> {
    auto array  = rstd_try(json_array(value, context));
    auto result = Vec<String>::with_capacity(array->len());
    auto seen   = rstd::collections::BTreeMap<String, empty>::make();
    for (usize index {}; index < array->len(); ++index) {
        auto item_context = rstd::format("{}[{}]", context, index);
        auto text         = (*array)[index].as_str();
        if (text.is_none() || ! valid_local_name(*text)) {
            return metadata_failure<Vec<String>>(item_context.as_str(),
                                                 "must be a package feature name"_str);
        }
        if (seen.contains_key(*text)) {
            return metadata_failure<Vec<String>>(context,
                                                 rstd::format("repeats feature '{}'", *text));
        }
        seen.insert(String::make(*text), empty {});
        result.push(String::make(*text));
    }
    return Ok(rstd::move(result));
}

auto parse_dependency(const Json& value, ref<str> context)
    -> RegistryValueResult<lito::registry::RegistryDependencyProjection> {
    rstd_try(reject_unknown(value,
                            context,
                            { "alias"_str,
                              "registry"_str,
                              "package"_str,
                              "requirement"_str,
                              "kind"_str,
                              "visibility"_str,
                              "features"_str,
                              "default_features"_str }));
    auto alias_text = rstd_try(required_string(value, "alias"_str, context));
    if (! valid_local_name(alias_text)) {
        return metadata_failure<lito::registry::RegistryDependencyProjection>(
            context, "field 'alias' is not a package dependency alias"_str);
    }
    auto registry = lito::registry::RegistryId::parse(
        rstd_try(required_string(value, "registry"_str, context)));
    auto package = lito::registry::RegistryPackageName::parse(
        rstd_try(required_string(value, "package"_str, context)));
    auto requirement = lito::registry::VersionRequirement::parse(
        rstd_try(required_string(value, "requirement"_str, context)));
    auto kind = parse_kind(rstd_try(required_string(value, "kind"_str, context)), context);
    auto visibility =
        parse_visibility(rstd_try(required_string(value, "visibility"_str, context)), context);
    auto features_member = rstd_try(required_member(value, "features"_str, context));
    auto features = parse_features(*features_member, rstd::format("{}.features", context).as_str());
    auto default_features = required_bool(value, "default_features"_str, context);
    if (registry.is_err())
        return metadata_failure<lito::registry::RegistryDependencyProjection>(
            context, rstd::format("has invalid registry: {}", rstd::move(registry).unwrap_err()));
    if (package.is_err())
        return metadata_failure<lito::registry::RegistryDependencyProjection>(
            context, rstd::format("has invalid package: {}", rstd::move(package).unwrap_err()));
    if (requirement.is_err())
        return metadata_failure<lito::registry::RegistryDependencyProjection>(
            context,
            rstd::format("has invalid requirement: {}", rstd::move(requirement).unwrap_err()));
    if (kind.is_err()) return Err(rstd::move(kind).unwrap_err());
    if (visibility.is_err()) return Err(rstd::move(visibility).unwrap_err());
    if (features.is_err()) return Err(rstd::move(features).unwrap_err());
    if (default_features.is_err()) return Err(rstd::move(default_features).unwrap_err());
    return Ok(lito::registry::RegistryDependencyProjection {
        .alias = String::make(alias_text),
        .package =
            lito::registry::RegistryPackageId {
                .registry = rstd::move(registry).unwrap(),
                .name     = rstd::move(package).unwrap(),
            },
        .requirement      = rstd::move(requirement).unwrap(),
        .kind             = *kind,
        .visibility       = *visibility,
        .features         = rstd::move(features).unwrap(),
        .default_features = *default_features,
    });
}

auto parse_blob(const Json& value, ref<str> context)
    -> RegistryValueResult<lito::registry::RegistryBlobProjection> {
    rstd_try(reject_unknown(value, context, { "digest"_str, "size"_str, "format"_str }));
    auto digest =
        lito::registry::BlobDigest::parse(rstd_try(required_string(value, "digest"_str, context)));
    auto size = lito::registry::RegistryBlobSize::parse(
        rstd_try(required_string(value, "size"_str, context)));
    auto format = lito::registry::RegistryArchiveFormat::parse(
        rstd_try(required_string(value, "format"_str, context)));
    if (digest.is_err())
        return metadata_failure<lito::registry::RegistryBlobProjection>(
            context, rstd::format("has invalid digest: {}", rstd::move(digest).unwrap_err()));
    if (size.is_err())
        return metadata_failure<lito::registry::RegistryBlobProjection>(
            context, rstd::format("has invalid size: {}", rstd::move(size).unwrap_err()));
    if (format.is_err())
        return metadata_failure<lito::registry::RegistryBlobProjection>(
            context, rstd::format("has invalid format: {}", rstd::move(format).unwrap_err()));
    return Ok(lito::registry::RegistryBlobProjection {
        .digest = rstd::move(digest).unwrap(),
        .size   = rstd::move(size).unwrap(),
        .format = rstd::move(format).unwrap(),
    });
}

auto immutable_release_payload(const Json&                              release,
                               const lito::registry::RegistryPackageId& package)
    -> RegistryValueResult<Json> {
    auto result = JsonMap::make();
    result.insert(String::make("schema"_str),
                  Json::String(String::make("lito.registry.release.v1"_str)));
    result.insert(String::make("registry"_str),
                  Json::String(String::make(package.registry.as_str())));
    result.insert(String::make("package"_str), Json::String(String::make(package.name.as_str())));
    constexpr ref<str> fields[] = { "version"_str, "source"_str,       "manifest"_str,
                                    "blob"_str,    "dependencies"_str, "published_at"_str };
    for (auto field : fields) {
        auto member = rstd_try(required_member(release, field, "Registry release"_str));
        result.insert(String::make(field), member->clone());
    }
    return Ok(Json::Object(rstd::move(result)));
}

auto parse_release(const Json&                              value,
                   const lito::registry::RegistryPackageId& package,
                   ref<str>                                 context)
    -> RegistryValueResult<lito::registry::RegistryReleaseProjection> {
    rstd_try(reject_unknown(value,
                            context,
                            { "version"_str,
                              "release"_str,
                              "source"_str,
                              "manifest"_str,
                              "blob"_str,
                              "dependencies"_str,
                              "yanked"_str,
                              "deprecated"_str,
                              "published_at"_str }));
    auto version = lito::registry::SemanticVersion::parse(
        rstd_try(required_string(value, "version"_str, context)));
    auto release = lito::registry::ReleaseDigest::parse(
        rstd_try(required_string(value, "release"_str, context)));
    auto source = lito::registry::SourceDigest::parse(
        rstd_try(required_string(value, "source"_str, context)));
    auto manifest = lito::registry::ManifestDigest::parse(
        rstd_try(required_string(value, "manifest"_str, context)));
    auto blob_member         = rstd_try(required_member(value, "blob"_str, context));
    auto blob                = parse_blob(*blob_member, rstd::format("{}.blob", context).as_str());
    auto dependencies_member = rstd_try(required_member(value, "dependencies"_str, context));
    auto dependency_values   = rstd_try(
        json_array(*dependencies_member, rstd::format("{}.dependencies", context).as_str()));
    auto dependencies =
        Vec<lito::registry::RegistryDependencyProjection>::with_capacity(dependency_values->len());
    auto aliases = rstd::collections::BTreeMap<String, empty>::make();
    for (usize index {}; index < dependency_values->len(); ++index) {
        auto item_context = rstd::format("{}.dependencies[{}]", context, index);
        auto dependency   = parse_dependency((*dependency_values)[index], item_context.as_str());
        if (dependency.is_err()) return Err(rstd::move(dependency).unwrap_err());
        if (aliases.contains_key(dependency->alias.as_str())) {
            return metadata_failure<lito::registry::RegistryReleaseProjection>(
                context, rstd::format("repeats dependency alias '{}'", dependency->alias.as_str()));
        }
        aliases.insert(dependency->alias.clone(), empty {});
        dependencies.push(rstd::move(dependency).unwrap());
    }
    auto yanked            = required_bool(value, "yanked"_str, context);
    auto deprecated_member = rstd_try(required_member(value, "deprecated"_str, context));
    auto deprecated        = Option<String> {};
    if (! deprecated_member->is_null()) {
        auto message = deprecated_member->as_str();
        if (message.is_none()) {
            return metadata_failure<lito::registry::RegistryReleaseProjection>(
                context, "field 'deprecated' must be a string or null"_str);
        }
        deprecated = Some(String::make(*message));
    }
    auto published_at = lito::registry::RegistryTimestamp::parse(
        rstd_try(required_string(value, "published_at"_str, context)));
    if (version.is_err())
        return metadata_failure<lito::registry::RegistryReleaseProjection>(
            context, rstd::format("has invalid version: {}", rstd::move(version).unwrap_err()));
    if (release.is_err())
        return metadata_failure<lito::registry::RegistryReleaseProjection>(
            context,
            rstd::format("has invalid release digest: {}", rstd::move(release).unwrap_err()));
    if (source.is_err())
        return metadata_failure<lito::registry::RegistryReleaseProjection>(
            context,
            rstd::format("has invalid source digest: {}", rstd::move(source).unwrap_err()));
    if (manifest.is_err())
        return metadata_failure<lito::registry::RegistryReleaseProjection>(
            context,
            rstd::format("has invalid manifest digest: {}", rstd::move(manifest).unwrap_err()));
    if (blob.is_err()) return Err(rstd::move(blob).unwrap_err());
    if (yanked.is_err()) return Err(rstd::move(yanked).unwrap_err());
    if (published_at.is_err())
        return metadata_failure<lito::registry::RegistryReleaseProjection>(
            context,
            rstd::format("has invalid published_at: {}", rstd::move(published_at).unwrap_err()));

    auto payload   = rstd_try(immutable_release_payload(value, package));
    auto canonical = rstd_try(lito::registry::canonical_signed_json(payload));
    auto computed =
        lito::registry::ReleaseDigest(lito::crypto::sha256_digest(canonical.as_str().as_bytes()));
    if (! (computed == *release)) {
        return metadata_failure<lito::registry::RegistryReleaseProjection>(
            context, "release digest does not match its immutable projection"_str);
    }
    return Ok(lito::registry::RegistryReleaseProjection {
        .version      = rstd::move(version).unwrap(),
        .release      = rstd::move(release).unwrap(),
        .source       = rstd::move(source).unwrap(),
        .manifest     = rstd::move(manifest).unwrap(),
        .blob         = rstd::move(blob).unwrap(),
        .dependencies = rstd::move(dependencies),
        .yanked       = *yanked,
        .deprecated   = rstd::move(deprecated),
        .published_at = rstd::move(published_at).unwrap(),
    });
}

auto valid_tag_name(ref<str> value) -> bool {
    if (value.is_empty() || value[usize()] == u8('v') ||
        (value[usize()] >= u8('0') && value[usize()] <= u8('9'))) {
        return false;
    }
    for (auto byte : value.as_bytes()) {
        if ((byte >= u8('a') && byte <= u8('z')) || (byte >= u8('0') && byte <= u8('9')) ||
            byte == u8('-') || byte == u8('_')) {
            continue;
        }
        return false;
    }
    return lito::registry::SemanticVersion::parse(value).is_err();
}

auto parse_signatures(const Json&                             envelope,
                      const Json&                             signed_value,
                      const lito::registry::Ed25519PublicKey& trusted_key)
    -> RegistryValueResult<lito::registry::SigningKeyId> {
    auto canonical  = rstd_try(lito::registry::canonical_signed_json(signed_value));
    auto trusted_id = rstd_try(lito::registry::signing_key_id(trusted_key));
    auto signatures_value =
        rstd_try(required_member(envelope, "signatures"_str, "Registry envelope"_str));
    auto signatures = rstd_try(json_array(*signatures_value, "Registry envelope.signatures"_str));
    if (signatures->is_empty()) {
        return metadata_failure<lito::registry::SigningKeyId>("Registry envelope.signatures"_str,
                                                              "must not be empty"_str);
    }
    auto matched = false;
    for (usize index {}; index < signatures->len(); ++index) {
        auto        context = rstd::format("Registry envelope.signatures[{}]", index);
        const auto& item    = (*signatures)[index];
        rstd_try(reject_unknown(
            item, context.as_str(), { "algorithm"_str, "key_id"_str, "signature"_str }));
        auto algorithm = rstd_try(required_string(item, "algorithm"_str, context.as_str()));
        auto key_id    = lito::registry::SigningKeyId::parse(
            rstd_try(required_string(item, "key_id"_str, context.as_str())));
        if (key_id.is_err()) {
            return metadata_failure<lito::registry::SigningKeyId>(
                context.as_str(),
                rstd::format("has invalid key_id: {}", rstd::move(key_id).unwrap_err()));
        }
        auto signature_text = rstd_try(required_string(item, "signature"_str, context.as_str()));
        if (algorithm != "ed25519"_str || ! (*key_id == trusted_id)) continue;
        auto signature = lito::registry::Ed25519Signature::parse(signature_text);
        if (signature.is_err()) {
            return metadata_failure<lito::registry::SigningKeyId>(
                context.as_str(),
                rstd::format("has invalid signature: {}", rstd::move(signature).unwrap_err()));
        }
        auto verified =
            lito::registry::verify_ed25519(trusted_key, *signature, canonical.as_str().as_bytes());
        if (verified.is_err()) return Err(rstd::move(verified).unwrap_err());
        matched = true;
    }
    if (! matched) {
        return metadata_failure<lito::registry::SigningKeyId>(
            "Registry envelope"_str, "has no valid signature from the trusted key"_str);
    }
    return Ok(rstd::move(trusted_id));
}

auto release_exists(slice<lito::registry::RegistryReleaseProjection> releases,
                    const lito::registry::SemanticVersion&           version) -> bool {
    for (const auto& release : releases) {
        if (release.version == version) return true;
    }
    return false;
}

auto decimal_field(const Json& value, ref<str> field, ref<str> context)
    -> RegistryValueResult<u64> {
    auto text   = rstd_try(required_string(value, field, context));
    auto parsed = lito::parse::parse_canonical_u64_decimal(text);
    if (parsed.is_err()) {
        return metadata_failure<u64>(
            context, rstd::format("field '{}' must be a canonical unsigned decimal string", field));
    }
    return Ok(*parsed);
}

} // namespace

auto lito::registry::RegistryTimestamp::parse(ref<str> value)
    -> RegistryValueResult<RegistryTimestamp> {
    if (value.len() != usize(20) || value[usize(4)] != u8('-') || value[usize(7)] != u8('-') ||
        value[usize(10)] != u8('T') || value[usize(13)] != u8(':') || value[usize(16)] != u8(':') ||
        value[usize(19)] != u8('Z')) {
        return registry_value_failure<RegistryTimestamp>(
            "timestamp must use canonical UTC second form YYYY-MM-DDTHH:MM:SSZ"_str);
    }
    constexpr usize digits[] = { usize(0),  usize(1),  usize(2),  usize(3),  usize(5),
                                 usize(6),  usize(8),  usize(9),  usize(11), usize(12),
                                 usize(14), usize(15), usize(17), usize(18) };
    for (auto index : digits) {
        if (value[index] < u8('0') || value[index] > u8('9')) {
            return registry_value_failure<RegistryTimestamp>(
                "timestamp contains a non-decimal date component"_str);
        }
    }
    const auto number = [&](usize offset, usize count) -> u64 {
        auto result = u64 {};
        for (usize index {}; index < count; ++index) {
            result = result * u64(10) + as_cast<u64>(value[offset + index] - u8('0'));
        }
        return result;
    };
    auto year   = number(usize {}, usize(4));
    auto month  = number(usize(5), usize(2));
    auto day    = number(usize(8), usize(2));
    auto hour   = number(usize(11), usize(2));
    auto minute = number(usize(14), usize(2));
    auto second = number(usize(17), usize(2));
    if (year == u64 {} || month < u64(1) || month > u64(12) || hour > u64(23) || minute > u64(59) ||
        second > u64(59)) {
        return registry_value_failure<RegistryTimestamp>(
            "timestamp is outside the supported UTC calendar"_str);
    }
    constexpr u8 month_days[] = { u8(31), u8(28), u8(31), u8(30), u8(31), u8(30),
                                  u8(31), u8(31), u8(30), u8(31), u8(30), u8(31) };
    auto         maximum_day  = as_cast<u64>(month_days[(month - u64(1)).to_primitive()]);
    auto leap = (year % u64(4) == u64 {} && year % u64(100) != u64 {}) || year % u64(400) == u64 {};
    if (month == u64(2) && leap) maximum_day = u64(29);
    if (day < u64(1) || day > maximum_day) {
        return registry_value_failure<RegistryTimestamp>(
            "timestamp has an invalid calendar day"_str);
    }
    return Ok(RegistryTimestamp(String::make(value)));
}

auto lito::registry::RegistryDependencyProjection::clone() const -> RegistryDependencyProjection {
    return RegistryDependencyProjection {
        .alias            = alias.clone(),
        .package          = package.clone(),
        .requirement      = requirement.clone(),
        .kind             = kind,
        .visibility       = visibility,
        .features         = as<Clone>(features).clone(),
        .default_features = default_features,
    };
}

auto lito::registry::RegistryReleaseProjection::clone() const -> RegistryReleaseProjection {
    auto cloned_dependencies = Vec<RegistryDependencyProjection>::with_capacity(dependencies.len());
    for (const auto& dependency : dependencies) {
        cloned_dependencies.push(dependency.clone());
    }
    auto result = RegistryReleaseProjection {
        .version      = version.clone(),
        .release      = release.clone(),
        .source       = source.clone(),
        .manifest     = manifest.clone(),
        .blob         = blob.clone(),
        .dependencies = rstd::move(cloned_dependencies),
        .yanked       = yanked,
        .published_at = published_at.clone(),
    };
    if (deprecated.is_some()) result.deprecated = Some(deprecated->clone());
    return result;
}

auto lito::registry::VerifiedPackageIndex::clone() const -> VerifiedPackageIndex {
    auto releases = Vec<RegistryReleaseProjection>::with_capacity(releases_.len());
    for (const auto& release : releases_) releases.push(release.clone());
    auto tags = Vec<RegistryTagProjection>::with_capacity(tags_.len());
    for (const auto& tag : tags_) tags.push(tag.clone());
    return VerifiedPackageIndex(package_.clone(),
                                revision_,
                                sequence_,
                                rstd::move(releases),
                                rstd::move(tags),
                                verified_key_.clone(),
                                canonical_signed_.clone());
}

auto lito::registry::parse_verified_package_index(slice<u8>                input,
                                                  const RegistryPackageId& expected,
                                                  const Ed25519PublicKey&  trusted_key)
    -> RegistryValueResult<VerifiedPackageIndex> {
    auto parsed =
        rstd::json::from_slice(input, rstd::json::ParseOptions { .reject_duplicate_keys = true });
    if (parsed.is_err()) {
        return registry_value_failure<VerifiedPackageIndex>(rstd::format(
            "Registry package index is invalid JSON: {}", rstd::move(parsed).unwrap_err()));
    }
    auto envelope = rstd::move(parsed).unwrap();
    rstd_try(reject_unknown(envelope, "Registry envelope"_str, { "signed"_str, "signatures"_str }));
    auto signed_value = rstd_try(required_member(envelope, "signed"_str, "Registry envelope"_str));
    auto verified_key = rstd_try(parse_signatures(envelope, *signed_value, trusted_key));
    rstd_try(reject_unknown(*signed_value,
                            "Registry package index"_str,
                            { "schema"_str,
                              "registry"_str,
                              "package"_str,
                              "revision"_str,
                              "sequence"_str,
                              "releases"_str,
                              "tags"_str }));
    auto schema =
        rstd_try(required_string(*signed_value, "schema"_str, "Registry package index"_str));
    if (schema != "lito.registry.package-index.v1"_str) {
        return metadata_failure<VerifiedPackageIndex>("Registry package index"_str,
                                                      "uses an unsupported schema"_str);
    }
    auto registry = RegistryId::parse(
        rstd_try(required_string(*signed_value, "registry"_str, "Registry package index"_str)));
    auto package = RegistryPackageName::parse(
        rstd_try(required_string(*signed_value, "package"_str, "Registry package index"_str)));
    if (registry.is_err() || package.is_err() || ! (*registry == expected.registry) ||
        ! (*package == expected.name)) {
        return metadata_failure<VerifiedPackageIndex>(
            "Registry package index"_str, "does not match the requested registry and package"_str);
    }
    auto package_id = RegistryPackageId {
        .registry = rstd::move(registry).unwrap(),
        .name     = rstd::move(package).unwrap(),
    };
    auto revision =
        rstd_try(decimal_field(*signed_value, "revision"_str, "Registry package index"_str));
    auto sequence =
        rstd_try(decimal_field(*signed_value, "sequence"_str, "Registry package index"_str));
    auto releases_value =
        rstd_try(required_member(*signed_value, "releases"_str, "Registry package index"_str));
    auto release_values =
        rstd_try(json_array(*releases_value, "Registry package index.releases"_str));
    auto releases = Vec<RegistryReleaseProjection>::with_capacity(release_values->len());
    for (usize index {}; index < release_values->len(); ++index) {
        auto context = rstd::format("Registry package index.releases[{}]", index);
        auto release = parse_release((*release_values)[index], package_id, context.as_str());
        if (release.is_err()) return Err(rstd::move(release).unwrap_err());
        if (release_exists(releases.as_slice(), release->version)) {
            return metadata_failure<VerifiedPackageIndex>(
                "Registry package index"_str,
                rstd::format("repeats version '{}'", release->version.text().as_str()));
        }
        releases.push(rstd::move(release).unwrap());
    }
    rstd::slice_::sort_unstable_by(
        releases.as_mut_slice().as_mut_ref(),
        [](const RegistryReleaseProjection& left, const RegistryReleaseProjection& right) {
            return right.version < left.version;
        });

    auto tags_value =
        rstd_try(required_member(*signed_value, "tags"_str, "Registry package index"_str));
    auto tag_values = rstd_try(json_object(*tags_value, "Registry package index.tags"_str));
    auto tags       = Vec<RegistryTagProjection>::with_capacity(tag_values->len());
    auto tag_iter   = tag_values->iter();
    for (auto item : tag_iter) {
        auto name   = item.template get<0>();
        auto target = item.template get<1>()->as_str();
        if (! valid_tag_name(name->as_str()) || target.is_none()) {
            return metadata_failure<VerifiedPackageIndex>(
                "Registry package index.tags"_str,
                rstd::format("contains invalid tag '{}'", name->as_str()));
        }
        auto version = SemanticVersion::parse(*target);
        if (version.is_err() || ! release_exists(releases.as_slice(), *version)) {
            return metadata_failure<VerifiedPackageIndex>(
                "Registry package index.tags"_str,
                rstd::format("tag '{}' targets an unknown version", name->as_str()));
        }
        tags.push(RegistryTagProjection {
            .name    = name->clone(),
            .version = rstd::move(version).unwrap(),
        });
    }
    auto canonical = rstd_try(canonical_signed_json(*signed_value));
    return Ok(VerifiedPackageIndex(rstd::move(package_id),
                                   revision,
                                   sequence,
                                   rstd::move(releases),
                                   rstd::move(tags),
                                   rstd::move(verified_key),
                                   rstd::move(canonical)));
}

auto lito::registry::parse_verified_registry_release(slice<u8>                input,
                                                     const RegistryPackageId& expected_package,
                                                     const ReleaseDigest&     expected_release,
                                                     const Ed25519PublicKey&  trusted_key)
    -> RegistryValueResult<VerifiedRegistryRelease> {
    auto parsed =
        rstd::json::from_slice(input, rstd::json::ParseOptions { .reject_duplicate_keys = true });
    if (parsed.is_err()) {
        return registry_value_failure<VerifiedRegistryRelease>(
            rstd::format("Registry release is invalid JSON: {}", rstd::move(parsed).unwrap_err()));
    }
    auto envelope = rstd::move(parsed).unwrap();
    rstd_try(reject_unknown(envelope, "Registry envelope"_str, { "signed"_str, "signatures"_str }));
    auto signed_value = rstd_try(required_member(envelope, "signed"_str, "Registry envelope"_str));
    auto verified_key = rstd_try(parse_signatures(envelope, *signed_value, trusted_key));
    rstd_try(reject_unknown(*signed_value,
                            "Registry release"_str,
                            { "schema"_str,
                              "registry"_str,
                              "package"_str,
                              "version"_str,
                              "source"_str,
                              "manifest"_str,
                              "blob"_str,
                              "dependencies"_str,
                              "published_at"_str }));
    auto schema = rstd_try(required_string(*signed_value, "schema"_str, "Registry release"_str));
    if (schema != "lito.registry.release.v1"_str) {
        return metadata_failure<VerifiedRegistryRelease>("Registry release"_str,
                                                         "uses an unsupported schema"_str);
    }
    auto registry = RegistryId::parse(
        rstd_try(required_string(*signed_value, "registry"_str, "Registry release"_str)));
    auto package = RegistryPackageName::parse(
        rstd_try(required_string(*signed_value, "package"_str, "Registry release"_str)));
    if (registry.is_err() || package.is_err() || ! (*registry == expected_package.registry) ||
        ! (*package == expected_package.name)) {
        return metadata_failure<VerifiedRegistryRelease>(
            "Registry release"_str, "does not match the requested registry and package"_str);
    }
    auto canonical = rstd_try(canonical_signed_json(*signed_value));
    auto computed  = ReleaseDigest(lito::crypto::sha256_digest(canonical.as_str().as_bytes()));
    if (! (computed == expected_release)) {
        return metadata_failure<VerifiedRegistryRelease>(
            "Registry release"_str, "does not match the requested release digest"_str);
    }

    auto               release_value = JsonMap::make();
    constexpr ref<str> fields[]      = { "version"_str, "source"_str,       "manifest"_str,
                                         "blob"_str,    "dependencies"_str, "published_at"_str };
    for (auto field : fields) {
        auto member = rstd_try(required_member(*signed_value, field, "Registry release"_str));
        release_value.insert(String::make(field), member->clone());
    }
    release_value.insert(String::make("release"_str),
                         Json::String(String::make(expected_release.text().as_str())));
    release_value.insert(String::make("yanked"_str), Json::Bool(false));
    release_value.insert(String::make("deprecated"_str), Json {});
    auto release = rstd_try(parse_release(
        Json::Object(rstd::move(release_value)), expected_package, "Registry release"_str));
    return Ok(VerifiedRegistryRelease(expected_package.clone(),
                                      rstd::move(release),
                                      rstd::move(verified_key),
                                      rstd::move(canonical)));
}

auto lito::registry::registry_immutable_release_matches(
    const RegistryReleaseProjection& left,
    const RegistryReleaseProjection& right) noexcept -> bool {
    if (! (left.version == right.version) || ! (left.release == right.release) ||
        ! (left.source == right.source) || ! (left.manifest == right.manifest) ||
        ! (left.blob.digest == right.blob.digest) ||
        left.blob.size.value() != right.blob.size.value() ||
        left.blob.format.as_str() != right.blob.format.as_str() ||
        left.published_at.as_str() != right.published_at.as_str() ||
        left.dependencies.len() != right.dependencies.len()) {
        return false;
    }
    for (usize index {}; index < left.dependencies.len(); ++index) {
        const auto& first  = left.dependencies[index];
        const auto& second = right.dependencies[index];
        if (first.alias != second.alias || ! (first.package == second.package) ||
            first.requirement.text() != second.requirement.text() || first.kind != second.kind ||
            first.visibility != second.visibility ||
            first.default_features != second.default_features ||
            first.features.len() != second.features.len()) {
            return false;
        }
        for (usize feature {}; feature < first.features.len(); ++feature) {
            if (first.features[feature] != second.features[feature]) return false;
        }
    }
    return true;
}
