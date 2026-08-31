module;
#include <rstd/macro.hpp>

export module lito.pack:inspection_protocol;

import rstd;
import rstd.json;
import lito.core;
import :registry.archive;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::registry
{

inline constexpr auto REGISTRY_INSPECTION_PROTOCOL       = "lito.registry.inspect.v3"_str;
inline constexpr auto REGISTRY_INSPECTION_REQUEST_SCHEMA = "lito.registry.inspect-request.v3"_str;
inline constexpr auto REGISTRY_INSPECTION_CANDIDATE_SCHEMA =
    "lito.registry.verified-publish-candidate.v3"_str;
inline constexpr auto REGISTRY_INSPECTION_FAILURE_SCHEMA =
    "lito.registry.package-check-failure.v3"_str;
inline constexpr auto REGISTRY_INSPECTOR_RECEIPT = "lito.registry.inspector-receipt.v3"_str;

struct RegistryInspectionProtocolError {
    String message;
};

template<typename T>
using RegistryInspectionProtocolResult = Result<T, RegistryInspectionProtocolError>;

struct RegistryInspectionRequest {
    RegistryPackageId      package;
    SemanticVersion        version;
    RegistryPackageArchive archive;
    RegistryArchiveLimits  limits;
    u64                    maximum_blob_size {};
};

struct VerifiedRegistryPackageDescriptor {
    RegistryPackageId                 package;
    SemanticVersion                   version;
    RegistryPackageArchive            archive;
    Vec<RegistryDependencyProjection> dependencies;
    usize                             file_count {};
    u64                               unpacked_size {};
};

auto registry_inspector_capabilities_json() -> String;
auto parse_registry_inspection_request(slice<u8> input)
    -> RegistryInspectionProtocolResult<RegistryInspectionRequest>;
auto parse_verified_publish_candidate(slice<u8> input)
    -> RegistryInspectionProtocolResult<VerifiedRegistryPackageDescriptor>;
auto serialize_verified_publish_candidate(const InspectedRegistryArchive& inspected) -> String;
auto serialize_registry_inspection_failure(const RegistryArtifactError& error) -> String;

} // namespace lito::registry

namespace
{

using namespace lito::registry;
using Json    = rstd::json::Value;
using JsonMap = rstd::json::Map;

template<typename T>
auto protocol_failure(String message) -> RegistryInspectionProtocolResult<T> {
    return Err(RegistryInspectionProtocolError { .message = rstd::move(message) });
}

template<typename T>
auto protocol_failure(ref<str> message) -> RegistryInspectionProtocolResult<T> {
    return protocol_failure<T>(String::make(message));
}

auto object(const Json& value, ref<str> context) -> RegistryInspectionProtocolResult<ref<JsonMap>> {
    auto result = value.as_object();
    if (result.is_none()) {
        return protocol_failure<ref<JsonMap>>(rstd::format("{} must be an object", context));
    }
    return Ok(*result);
}

auto known_field(ref<str> field, initializer_list<ref<str>> allowed) noexcept -> bool {
    for (auto candidate : allowed) {
        if (field == candidate) return true;
    }
    return false;
}

auto reject_unknown(const Json& value, ref<str> context, initializer_list<ref<str>> allowed)
    -> RegistryInspectionProtocolResult<empty> {
    auto members = rstd_try(object(value, context));
    auto keys    = members->keys();
    for (auto key : keys) {
        if (! known_field((*key).as_str(), allowed)) {
            return protocol_failure<empty>(
                rstd::format("{} contains unknown field '{}'", context, (*key).as_str()));
        }
    }
    return Ok(empty {});
}

auto member(const Json& value, ref<str> key, ref<str> context)
    -> RegistryInspectionProtocolResult<ref<Json>> {
    auto members = rstd_try(object(value, context));
    auto result  = members->get(key);
    if (result.is_none()) {
        return protocol_failure<ref<Json>>(rstd::format("{} is missing '{}'", context, key));
    }
    return Ok(*result);
}

auto string_member(const Json& value, ref<str> key, ref<str> context)
    -> RegistryInspectionProtocolResult<ref<str>> {
    auto result = rstd_try(member(value, key, context));
    auto text   = result->as_str();
    if (text.is_none()) {
        return protocol_failure<ref<str>>(rstd::format("{}.{} must be a string", context, key));
    }
    return Ok(*text);
}

auto bool_member(const Json& value, ref<str> key, ref<str> context)
    -> RegistryInspectionProtocolResult<bool> {
    auto result  = rstd_try(member(value, key, context));
    auto boolean = result->as_bool();
    if (boolean.is_none()) {
        return protocol_failure<bool>(rstd::format("{}.{} must be a boolean", context, key));
    }
    return Ok(*boolean);
}

template<typename T>
auto parse_registry_value(RegistryValueResult<T> result, ref<str> context)
    -> RegistryInspectionProtocolResult<T> {
    if (result.is_ok()) return Ok(rstd::move(result).unwrap());
    return protocol_failure<T>(rstd::format("{}: {}", context, rstd::move(result).unwrap_err()));
}

auto parse_limit(const Json& value, ref<str> key) -> RegistryInspectionProtocolResult<u64> {
    auto text   = rstd_try(string_member(value, key, "request.limits"_str));
    auto parsed = RegistryBlobSize::parse(text);
    if (parsed.is_err()) {
        return protocol_failure<u64>(
            rstd::format("request.limits.{} must be a canonical unsigned decimal string", key));
    }
    return Ok(parsed->value());
}

auto string_json(ref<str> value) -> Json {
    return Json::String(String::make(value));
}

auto dependency_kind_text(RegistryDependencyKind kind) noexcept -> ref<str> {
    switch (kind) {
    case RegistryDependencyKind::Normal: return "normal"_str;
    case RegistryDependencyKind::Development: return "development"_str;
    case RegistryDependencyKind::Runtime: return "runtime"_str;
    }
    rstd::unreachable();
}

auto dependency_visibility_text(lito::dependency::DependencyVisibility visibility) noexcept
    -> ref<str> {
    switch (visibility) {
    case lito::dependency::DependencyVisibility::Public: return "public"_str;
    case lito::dependency::DependencyVisibility::Private: return "private"_str;
    case lito::dependency::DependencyVisibility::LinkOnly: return "link"_str;
    }
    rstd::unreachable();
}

auto failure_code_text(RegistryArtifactFailureCode code) noexcept -> ref<str> {
    switch (code) {
    case RegistryArtifactFailureCode::PackageInvalid: return "package_invalid"_str;
    case RegistryArtifactFailureCode::ExternalInputsNotAllowed:
        return "external_inputs_not_allowed"_str;
    }
    rstd::unreachable();
}

auto dependency_kind(ref<str> value, ref<str> context)
    -> RegistryInspectionProtocolResult<RegistryDependencyKind> {
    if (value == "normal"_str) return Ok(RegistryDependencyKind::Normal);
    if (value == "development"_str) return Ok(RegistryDependencyKind::Development);
    if (value == "runtime"_str) return Ok(RegistryDependencyKind::Runtime);
    return protocol_failure<RegistryDependencyKind>(
        rstd::format("{}.kind is unsupported", context));
}

auto dependency_visibility(ref<str> value, ref<str> context)
    -> RegistryInspectionProtocolResult<lito::dependency::DependencyVisibility> {
    using lito::dependency::DependencyVisibility;
    if (value == "public"_str) return Ok(DependencyVisibility::Public);
    if (value == "private"_str) return Ok(DependencyVisibility::Private);
    if (value == "link"_str) return Ok(DependencyVisibility::LinkOnly);
    return protocol_failure<DependencyVisibility>(
        rstd::format("{}.visibility is unsupported", context));
}

auto canonical_size(const Json& value, ref<str> field, ref<str> context)
    -> RegistryInspectionProtocolResult<u64> {
    auto text   = rstd_try(string_member(value, field, context));
    auto parsed = RegistryBlobSize::parse(text);
    if (parsed.is_err()) {
        return protocol_failure<u64>(
            rstd::format("{}.{} must be a canonical unsigned decimal string", context, field));
    }
    return Ok(parsed->value());
}

auto parse_dependency(const Json& value, usize index)
    -> RegistryInspectionProtocolResult<RegistryDependencyProjection> {
    auto context = rstd::format("candidate.dependencies[{}]", index);
    rstd_try(reject_unknown(value,
                            context.as_str(),
                            { "alias"_str,
                              "registry"_str,
                              "package"_str,
                              "requirement"_str,
                              "kind"_str,
                              "visibility"_str,
                              "features"_str,
                              "default_features"_str }));
    auto registry = rstd_try(parse_registry_value(
        RegistryId::parse(rstd_try(string_member(value, "registry"_str, context.as_str()))),
        context.as_str()));
    auto package  = rstd_try(parse_registry_value(
        RegistryPackageName::parse(rstd_try(string_member(value, "package"_str, context.as_str()))),
        context.as_str()));
    auto requirement =
        rstd_try(parse_registry_value(VersionRequirement::parse(rstd_try(string_member(
                                          value, "requirement"_str, context.as_str()))),
                                      context.as_str()));
    auto features_value = rstd_try(member(value, "features"_str, context.as_str()));
    auto features_array = features_value->as_array();
    if (features_array.is_none()) {
        return protocol_failure<RegistryDependencyProjection>(
            rstd::format("{}.features must be an array", context.as_str()));
    }
    auto features = Vec<String>::with_capacity((*features_array)->len());
    for (usize feature_index {}; feature_index < (*features_array)->len(); ++feature_index) {
        auto feature = (**features_array)[feature_index].as_str();
        if (feature.is_none()) {
            return protocol_failure<RegistryDependencyProjection>(
                rstd::format("{}.features[{}] must be a string", context.as_str(), feature_index));
        }
        features.push(String::make(*feature));
    }
    return Ok(RegistryDependencyProjection {
        .alias = String::make(rstd_try(string_member(value, "alias"_str, context.as_str()))),
        .package =
            RegistryPackageId {
                .registry = rstd::move(registry),
                .name     = rstd::move(package),
            },
        .requirement      = rstd::move(requirement),
        .kind             = rstd_try(dependency_kind(
            rstd_try(string_member(value, "kind"_str, context.as_str())), context.as_str())),
        .visibility       = rstd_try(dependency_visibility(
            rstd_try(string_member(value, "visibility"_str, context.as_str())), context.as_str())),
        .features         = rstd::move(features),
        .default_features = rstd_try(bool_member(value, "default_features"_str, context.as_str())),
    });
}

auto dependency_json(const RegistryDependencyProjection& dependency) -> Json {
    auto features = rstd::json::Array::make();
    for (const auto& feature : dependency.features) features.push(string_json(feature.as_str()));
    auto value = JsonMap::make();
    value.insert(String::make("alias"_str), string_json(dependency.alias.as_str()));
    value.insert(String::make("registry"_str), string_json(dependency.package.registry.as_str()));
    value.insert(String::make("package"_str), string_json(dependency.package.name.as_str()));
    value.insert(String::make("requirement"_str), string_json(dependency.requirement.text()));
    value.insert(String::make("kind"_str), string_json(dependency_kind_text(dependency.kind)));
    value.insert(String::make("visibility"_str),
                 string_json(dependency_visibility_text(dependency.visibility)));
    value.insert(String::make("features"_str), Json::Array(rstd::move(features)));
    value.insert(String::make("default_features"_str), Json::Bool(dependency.default_features));
    return Json::Object(rstd::move(value));
}

} // namespace

auto lito::registry::registry_inspector_capabilities_json() -> String {
    auto protocols = rstd::json::Array::make();
    protocols.push(string_json(REGISTRY_INSPECTION_PROTOCOL));
    auto formats = rstd::json::Array::make();
    formats.push(string_json(RegistryArchiveFormat::TAR_ZSTD_V1));
    auto root = JsonMap::make();
    root.insert(String::make("schema"_str),
                string_json("lito.registry.inspector-capabilities.v3"_str));
    root.insert(String::make("protocols"_str), Json::Array(rstd::move(protocols)));
    root.insert(String::make("archive_formats"_str), Json::Array(rstd::move(formats)));
    return rstd::json::to_string(Json::Object(rstd::move(root)));
}

auto lito::registry::parse_registry_inspection_request(slice<u8> input)
    -> RegistryInspectionProtocolResult<RegistryInspectionRequest> {
    auto parsed =
        rstd::json::from_slice(input, rstd::json::ParseOptions { .reject_duplicate_keys = true });
    if (parsed.is_err()) {
        return protocol_failure<RegistryInspectionRequest>(rstd::format(
            "inspection request is not strict JSON: {}", rstd::move(parsed).unwrap_err()));
    }
    auto& root = *parsed;
    rstd_try(reject_unknown(root,
                            "request"_str,
                            { "schema"_str,
                              "protocol"_str,
                              "registry"_str,
                              "package"_str,
                              "version"_str,
                              "archive"_str,
                              "limits"_str }));
    if (rstd_try(string_member(root, "schema"_str, "request"_str)) !=
        REGISTRY_INSPECTION_REQUEST_SCHEMA) {
        return protocol_failure<RegistryInspectionRequest>(
            "inspection request schema is unsupported"_str);
    }
    if (rstd_try(string_member(root, "protocol"_str, "request"_str)) !=
        REGISTRY_INSPECTION_PROTOCOL) {
        return protocol_failure<RegistryInspectionRequest>(
            "inspection protocol is unsupported"_str);
    }
    auto registry     = rstd_try(parse_registry_value(
        RegistryId::parse(rstd_try(string_member(root, "registry"_str, "request"_str))),
        "request.registry"_str));
    auto package_name = rstd_try(parse_registry_value(
        RegistryPackageName::parse(rstd_try(string_member(root, "package"_str, "request"_str))),
        "request.package"_str));
    auto version      = rstd_try(parse_registry_value(
        SemanticVersion::parse(rstd_try(string_member(root, "version"_str, "request"_str))),
        "request.version"_str));

    auto archive = rstd_try(member(root, "archive"_str, "request"_str));
    rstd_try(reject_unknown(
        *archive, "request.archive"_str, { "checksum"_str, "size"_str, "format"_str }));
    auto checksum =
        rstd_try(parse_registry_value(PackageChecksum::parse(rstd_try(string_member(
                                          *archive, "checksum"_str, "request.archive"_str))),
                                      "request.archive.checksum"_str));
    auto archive_size =
        rstd_try(parse_registry_value(RegistryBlobSize::parse(rstd_try(string_member(
                                          *archive, "size"_str, "request.archive"_str))),
                                      "request.archive.size"_str));
    auto archive_format =
        rstd_try(parse_registry_value(RegistryArchiveFormat::parse(rstd_try(string_member(
                                          *archive, "format"_str, "request.archive"_str))),
                                      "request.archive.format"_str));

    auto limits = rstd_try(member(root, "limits"_str, "request"_str));
    rstd_try(reject_unknown(*limits,
                            "request.limits"_str,
                            { "maximum_blob_size"_str,
                              "maximum_unpacked_size"_str,
                              "maximum_file_size"_str,
                              "maximum_entries"_str }));
    auto maximum_blob_size     = rstd_try(parse_limit(*limits, "maximum_blob_size"_str));
    auto maximum_unpacked_size = rstd_try(parse_limit(*limits, "maximum_unpacked_size"_str));
    auto maximum_file_size     = rstd_try(parse_limit(*limits, "maximum_file_size"_str));
    auto maximum_entries       = rstd_try(parse_limit(*limits, "maximum_entries"_str));
    if (maximum_entries > as_cast<u64>(usize::MAX)) {
        return protocol_failure<RegistryInspectionRequest>(
            "request.limits.maximum_entries exceeds this inspector's range"_str);
    }
    if (archive_size.value() > maximum_blob_size) {
        return protocol_failure<RegistryInspectionRequest>(
            "request blob exceeds the configured compressed size limit"_str);
    }
    return Ok(RegistryInspectionRequest {
        .package =
            RegistryPackageId {
                .registry = rstd::move(registry),
                .name     = rstd::move(package_name),
            },
        .version = rstd::move(version),
        .archive =
            RegistryPackageArchive {
                .checksum = rstd::move(checksum),
                .size     = rstd::move(archive_size),
                .format   = rstd::move(archive_format),
            },
        .limits =
            RegistryArchiveLimits {
                .maximum_unpacked_size = maximum_unpacked_size,
                .maximum_file_size     = maximum_file_size,
                .maximum_entries       = usize(maximum_entries.to_primitive()),
            },
        .maximum_blob_size = maximum_blob_size,
    });
}

auto lito::registry::parse_verified_publish_candidate(slice<u8> input)
    -> RegistryInspectionProtocolResult<VerifiedRegistryPackageDescriptor> {
    auto parsed =
        rstd::json::from_slice(input, rstd::json::ParseOptions { .reject_duplicate_keys = true });
    if (parsed.is_err()) {
        return protocol_failure<VerifiedRegistryPackageDescriptor>(rstd::format(
            "verified candidate is not strict JSON: {}", rstd::move(parsed).unwrap_err()));
    }
    auto& root = *parsed;
    rstd_try(reject_unknown(root,
                            "candidate"_str,
                            { "schema"_str,
                              "protocol"_str,
                              "registry"_str,
                              "package"_str,
                              "version"_str,
                              "archive"_str,
                              "dependencies"_str,
                              "file_count"_str,
                              "unpacked_size"_str,
                              "receipt"_str }));
    if (rstd_try(string_member(root, "schema"_str, "candidate"_str)) !=
            REGISTRY_INSPECTION_CANDIDATE_SCHEMA ||
        rstd_try(string_member(root, "protocol"_str, "candidate"_str)) !=
            REGISTRY_INSPECTION_PROTOCOL ||
        rstd_try(string_member(root, "receipt"_str, "candidate"_str)) !=
            REGISTRY_INSPECTOR_RECEIPT) {
        return protocol_failure<VerifiedRegistryPackageDescriptor>(
            "verified candidate protocol identity is unsupported"_str);
    }
    auto registry = rstd_try(parse_registry_value(
        RegistryId::parse(rstd_try(string_member(root, "registry"_str, "candidate"_str))),
        "candidate.registry"_str));
    auto package  = rstd_try(parse_registry_value(
        RegistryPackageName::parse(rstd_try(string_member(root, "package"_str, "candidate"_str))),
        "candidate.package"_str));
    auto version  = rstd_try(parse_registry_value(
        SemanticVersion::parse(rstd_try(string_member(root, "version"_str, "candidate"_str))),
        "candidate.version"_str));
    auto archive  = rstd_try(member(root, "archive"_str, "candidate"_str));
    rstd_try(reject_unknown(
        *archive, "candidate.archive"_str, { "checksum"_str, "size"_str, "format"_str }));
    auto checksum =
        rstd_try(parse_registry_value(PackageChecksum::parse(rstd_try(string_member(
                                          *archive, "checksum"_str, "candidate.archive"_str))),
                                      "candidate.archive.checksum"_str));
    auto size = rstd_try(parse_registry_value(RegistryBlobSize::parse(rstd_try(string_member(
                                                  *archive, "size"_str, "candidate.archive"_str))),
                                              "candidate.archive.size"_str));
    auto format =
        rstd_try(parse_registry_value(RegistryArchiveFormat::parse(rstd_try(string_member(
                                          *archive, "format"_str, "candidate.archive"_str))),
                                      "candidate.archive.format"_str));
    auto dependencies_value = rstd_try(member(root, "dependencies"_str, "candidate"_str));
    auto dependencies_array = dependencies_value->as_array();
    if (dependencies_array.is_none()) {
        return protocol_failure<VerifiedRegistryPackageDescriptor>(
            "candidate.dependencies must be an array"_str);
    }
    auto dependencies =
        Vec<RegistryDependencyProjection>::with_capacity((*dependencies_array)->len());
    for (usize index {}; index < (*dependencies_array)->len(); ++index) {
        dependencies.push(rstd_try(parse_dependency((**dependencies_array)[index], index)));
    }
    auto file_count = rstd_try(canonical_size(root, "file_count"_str, "candidate"_str));
    if (file_count > as_cast<u64>(usize::MAX)) {
        return protocol_failure<VerifiedRegistryPackageDescriptor>(
            "candidate.file_count exceeds this platform's range"_str);
    }
    return Ok(VerifiedRegistryPackageDescriptor {
        .package =
            RegistryPackageId {
                .registry = rstd::move(registry),
                .name     = rstd::move(package),
            },
        .version = rstd::move(version),
        .archive =
            RegistryPackageArchive {
                .checksum = rstd::move(checksum),
                .size     = rstd::move(size),
                .format   = rstd::move(format),
            },
        .dependencies  = rstd::move(dependencies),
        .file_count    = usize(file_count.to_primitive()),
        .unpacked_size = rstd_try(canonical_size(root, "unpacked_size"_str, "candidate"_str)),
    });
}

auto lito::registry::serialize_verified_publish_candidate(const InspectedRegistryArchive& inspected)
    -> String {
    const auto& candidate    = inspected.candidate;
    auto        dependencies = rstd::json::Array::make();
    for (const auto& dependency : candidate.dependencies) {
        dependencies.push(dependency_json(dependency));
    }
    auto archive = JsonMap::make();
    archive.insert(String::make("checksum"_str),
                   string_json(inspected.archive.checksum.text().as_str()));
    archive.insert(String::make("size"_str), string_json(inspected.archive.size.text().as_str()));
    archive.insert(String::make("format"_str), string_json(inspected.archive.format.as_str()));

    auto root = JsonMap::make();
    root.insert(String::make("schema"_str), string_json(REGISTRY_INSPECTION_CANDIDATE_SCHEMA));
    root.insert(String::make("protocol"_str), string_json(REGISTRY_INSPECTION_PROTOCOL));
    root.insert(String::make("registry"_str), string_json(candidate.package.registry.as_str()));
    root.insert(String::make("package"_str), string_json(candidate.package.name.as_str()));
    root.insert(String::make("version"_str), string_json(candidate.version.text().as_str()));
    root.insert(String::make("archive"_str), Json::Object(rstd::move(archive)));
    root.insert(String::make("dependencies"_str), Json::Array(rstd::move(dependencies)));
    root.insert(String::make("file_count"_str),
                string_json(rstd::format("{}", candidate.file_count).as_str()));
    root.insert(String::make("unpacked_size"_str),
                string_json(rstd::format("{}", candidate.unpacked_size).as_str()));
    root.insert(String::make("receipt"_str), string_json(REGISTRY_INSPECTOR_RECEIPT));
    return rstd::json::to_string(Json::Object(rstd::move(root)));
}

auto lito::registry::serialize_registry_inspection_failure(const RegistryArtifactError& error)
    -> String {
    auto root = JsonMap::make();
    root.insert(String::make("schema"_str), string_json(REGISTRY_INSPECTION_FAILURE_SCHEMA));
    root.insert(String::make("protocol"_str), string_json(REGISTRY_INSPECTION_PROTOCOL));
    root.insert(String::make("code"_str), string_json(failure_code_text(error.code)));
    root.insert(String::make("message"_str), string_json(error.message.as_str()));
    root.insert(String::make("receipt"_str), string_json(REGISTRY_INSPECTOR_RECEIPT));
    return rstd::json::to_string(Json::Object(rstd::move(root)));
}
