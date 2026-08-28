module;
#include <rstd/enum.hpp>

export module lito.core:registry.metadata;

import rstd;
import rstd.json;
import :dependency.visibility;
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

struct RegistryReleaseProjection {
    SemanticVersion                   version;
    PackageChecksum                   checksum;
    Vec<RegistryDependencyProjection> dependencies;
    bool                              yanked {};
    RegistryTimestamp                 published_at;

    auto clone() const -> RegistryReleaseProjection;
};

class RegistryPackageIndex : public DefaultInClass<RegistryPackageIndex, Clone> {
    RegistryPackageId              package_;
    Vec<RegistryReleaseProjection> releases_;

    RegistryPackageIndex(RegistryPackageId package, Vec<RegistryReleaseProjection> releases)
        : package_(rstd::move(package)), releases_(rstd::move(releases)) {}

    friend auto parse_package_index(slice<u8>, const RegistryPackageId&)
        -> RegistryValueResult<RegistryPackageIndex>;

public:
    static auto single(RegistryPackageId                 package,
                       SemanticVersion                   version,
                       PackageChecksum                   checksum,
                       Vec<RegistryDependencyProjection> dependencies)
        -> RegistryValueResult<RegistryPackageIndex>;
    auto package() const noexcept -> const RegistryPackageId& { return package_; }
    auto releases() const noexcept -> slice<RegistryReleaseProjection> {
        return releases_.as_slice();
    }
    auto clone() const -> RegistryPackageIndex;
};

auto parse_package_index(slice<u8> input, const RegistryPackageId& expected)
    -> RegistryValueResult<RegistryPackageIndex>;

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

auto parse_release(const Json& value, ref<str> context)
    -> RegistryValueResult<lito::registry::RegistryReleaseProjection> {
    rstd_try(reject_unknown(
        value,
        context,
        { "version"_str, "checksum"_str, "dependencies"_str, "yanked"_str, "published_at"_str }));
    auto version = lito::registry::SemanticVersion::parse(
        rstd_try(required_string(value, "version"_str, context)));
    auto checksum = lito::registry::PackageChecksum::parse(
        rstd_try(required_string(value, "checksum"_str, context)));
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
    auto yanked       = required_bool(value, "yanked"_str, context);
    auto published_at = lito::registry::RegistryTimestamp::parse(
        rstd_try(required_string(value, "published_at"_str, context)));
    if (version.is_err())
        return metadata_failure<lito::registry::RegistryReleaseProjection>(
            context, rstd::format("has invalid version: {}", rstd::move(version).unwrap_err()));
    if (checksum.is_err())
        return metadata_failure<lito::registry::RegistryReleaseProjection>(
            context, rstd::format("has invalid checksum: {}", rstd::move(checksum).unwrap_err()));
    if (yanked.is_err()) return Err(rstd::move(yanked).unwrap_err());
    if (published_at.is_err())
        return metadata_failure<lito::registry::RegistryReleaseProjection>(
            context,
            rstd::format("has invalid published_at: {}", rstd::move(published_at).unwrap_err()));
    return Ok(lito::registry::RegistryReleaseProjection {
        .version      = rstd::move(version).unwrap(),
        .checksum     = rstd::move(checksum).unwrap(),
        .dependencies = rstd::move(dependencies),
        .yanked       = *yanked,
        .published_at = rstd::move(published_at).unwrap(),
    });
}

auto release_exists(slice<lito::registry::RegistryReleaseProjection> releases,
                    const lito::registry::SemanticVersion&           version) -> bool {
    for (const auto& release : releases) {
        if (release.version == version) return true;
    }
    return false;
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
    return RegistryReleaseProjection {
        .version      = version.clone(),
        .checksum     = checksum.clone(),
        .dependencies = rstd::move(cloned_dependencies),
        .yanked       = yanked,
        .published_at = published_at.clone(),
    };
}

auto lito::registry::RegistryPackageIndex::clone() const -> RegistryPackageIndex {
    auto releases = Vec<RegistryReleaseProjection>::with_capacity(releases_.len());
    for (const auto& release : releases_) releases.push(release.clone());
    return RegistryPackageIndex(package_.clone(), rstd::move(releases));
}

auto lito::registry::RegistryPackageIndex::single(RegistryPackageId                 package,
                                                  SemanticVersion                   version,
                                                  PackageChecksum                   checksum,
                                                  Vec<RegistryDependencyProjection> dependencies)
    -> RegistryValueResult<RegistryPackageIndex> {
    auto timestamp = RegistryTimestamp::parse("2000-01-01T00:00:00Z"_str);
    if (timestamp.is_err()) return Err(rstd::move(timestamp).unwrap_err());
    auto releases = Vec<RegistryReleaseProjection>::make();
    releases.push(RegistryReleaseProjection {
        .version      = rstd::move(version),
        .checksum     = rstd::move(checksum),
        .dependencies = rstd::move(dependencies),
        .published_at = rstd::move(timestamp).unwrap(),
    });
    return Ok(RegistryPackageIndex(rstd::move(package), rstd::move(releases)));
}

auto lito::registry::parse_package_index(slice<u8> input, const RegistryPackageId& expected)
    -> RegistryValueResult<RegistryPackageIndex> {
    auto parsed =
        rstd::json::from_slice(input, rstd::json::ParseOptions { .reject_duplicate_keys = true });
    if (parsed.is_err()) {
        return registry_value_failure<RegistryPackageIndex>(rstd::format(
            "Registry package index is invalid JSON: {}", rstd::move(parsed).unwrap_err()));
    }
    auto document = rstd::move(parsed).unwrap();
    rstd_try(reject_unknown(document,
                            "Registry package index"_str,
                            { "schema"_str, "registry"_str, "package"_str, "releases"_str }));
    auto schema = rstd_try(required_string(document, "schema"_str, "Registry package index"_str));
    if (schema != "lito.registry.package-index.v1"_str) {
        return metadata_failure<RegistryPackageIndex>("Registry package index"_str,
                                                      "uses an unsupported schema"_str);
    }
    auto registry = RegistryId::parse(
        rstd_try(required_string(document, "registry"_str, "Registry package index"_str)));
    auto package = RegistryPackageName::parse(
        rstd_try(required_string(document, "package"_str, "Registry package index"_str)));
    if (registry.is_err() || package.is_err() || ! (*registry == expected.registry) ||
        ! (*package == expected.name)) {
        return metadata_failure<RegistryPackageIndex>(
            "Registry package index"_str, "does not match the requested registry and package"_str);
    }
    auto package_id = RegistryPackageId {
        .registry = rstd::move(registry).unwrap(),
        .name     = rstd::move(package).unwrap(),
    };
    auto releases_value =
        rstd_try(required_member(document, "releases"_str, "Registry package index"_str));
    auto release_values =
        rstd_try(json_array(*releases_value, "Registry package index.releases"_str));
    auto releases = Vec<RegistryReleaseProjection>::with_capacity(release_values->len());
    for (usize index {}; index < release_values->len(); ++index) {
        auto context = rstd::format("Registry package index.releases[{}]", index);
        auto release = parse_release((*release_values)[index], context.as_str());
        if (release.is_err()) return Err(rstd::move(release).unwrap_err());
        if (release_exists(releases.as_slice(), release->version)) {
            return metadata_failure<RegistryPackageIndex>(
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
    return Ok(RegistryPackageIndex(rstd::move(package_id), rstd::move(releases)));
}
