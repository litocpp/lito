module;
#include <initializer_list>
#include <rstd/macro.hpp>

export module lito.driver:registry.publish;

import rstd;
import rstd.json;
import licrypto;
import lito.core;
import lito.system;
import :config.registry;

using namespace rstd::prelude;
using namespace rstd::literals;
using PathBuf = rstd::path::PathBuf;

export namespace lito::registry
{

enum class RegistryPublishState
{
    Prepared,
    Uploaded,
    Checking,
    CheckRetry,
    CheckFailed,
    Rejected,
    Committed,
    Projecting,
    ProjectionRetry,
    Visible,
    Expired,
};

inline auto registry_publish_state_name(RegistryPublishState state) noexcept -> ref<str> {
    switch (state) {
    case RegistryPublishState::Prepared: return "prepared"_str;
    case RegistryPublishState::Uploaded: return "uploaded"_str;
    case RegistryPublishState::Checking: return "checking"_str;
    case RegistryPublishState::CheckRetry: return "check_retry"_str;
    case RegistryPublishState::CheckFailed: return "check_failed"_str;
    case RegistryPublishState::Rejected: return "rejected"_str;
    case RegistryPublishState::Committed: return "committed"_str;
    case RegistryPublishState::Projecting: return "projecting"_str;
    case RegistryPublishState::ProjectionRetry: return "projection_retry"_str;
    case RegistryPublishState::Visible: return "visible"_str;
    case RegistryPublishState::Expired: return "expired"_str;
    }
    rstd::unreachable();
}

enum class RegistryPublishErrorKind
{
    Network,
    Protocol,
    Authentication,
    Authorization,
    Conflict,
    Rejected,
    Infrastructure,
    Expired,
    Io,
};

struct RegistryPublishError {
    RegistryPublishErrorKind kind { RegistryPublishErrorKind::Protocol };
    RegistryPackageId        package;
    String                   message;
};

template<typename T>
using RegistryPublishResult = Result<T, RegistryPublishError>;

struct RegistryPublishHeader {
    String name;
    String value;
};

struct RegistryPublishHttpRequest {
    String                     method;
    String                     url;
    Vec<RegistryPublishHeader> headers;
    Option<String>             body;
    Option<PathBuf>            upload;
};

struct RegistryPublishHttpResponse {
    u16    status {};
    String body;
};

struct RegistryPublishHttpTransport {
    void* context {};
    RegistryPublishResult<RegistryPublishHttpResponse> (
        *execute)(void*, const RegistryPackageId&, const RegistryPublishHttpRequest&) noexcept {};
};

struct RegistryPublishSession {
    String                  id;
    RegistryPublishState    state { RegistryPublishState::Prepared };
    RegistryPackageId       package;
    SemanticVersion         version;
    RegistryPackageArchive  archive;
    Option<String>          rejection;
    Option<String>          failure;
    Option<PackageChecksum> checksum;
};

struct RegistryPublishRequest {
    RegistryFixedEndpoint                    api;
    const lito::config::RegistryBearerToken* token {};
    RegistryPackageId                        package;
    SemanticVersion                          version;
    RegistryPackageArchive                   artifact;
    PathBuf                                  archive;
};

class RegistryPublishClient {
    RegistryPublishHttpTransport transport_;

public:
    explicit RegistryPublishClient(RegistryPublishHttpTransport transport): transport_(transport) {}

    auto publish(const RegistryPublishRequest& request)
        -> RegistryPublishResult<RegistryPublishSession>;
};

class CurlRegistryPublishTransport {
    PathBuf                                         executable_;
    const lito::system::ResolvedProcessEnvironment* environment_ {};

    static auto
    execute_callback(void*, const RegistryPackageId&, const RegistryPublishHttpRequest&) noexcept
        -> RegistryPublishResult<RegistryPublishHttpResponse>;

public:
    CurlRegistryPublishTransport(PathBuf                                         executable,
                                 const lito::system::ResolvedProcessEnvironment& environment)
        : executable_(rstd::move(executable)), environment_(rstd::addressof(environment)) {}

    auto execute(const RegistryPackageId& package, const RegistryPublishHttpRequest& request)
        -> RegistryPublishResult<RegistryPublishHttpResponse>;
    auto transport() noexcept -> RegistryPublishHttpTransport {
        return RegistryPublishHttpTransport { .context = this, .execute = execute_callback };
    }
};

} // namespace lito::registry

namespace
{

using namespace lito::registry;
using Json    = rstd::json::Value;
using JsonMap = rstd::json::Map;

inline constexpr auto PUBLISH_RESPONSE_SCHEMA = "lito.registry.prepare-publish-session.v1"_str;
inline constexpr auto CURL_TRAILER            = "LITO_REGISTRY_PUBLISH_HTTP_V1:"_str;
inline constexpr auto MAXIMUM_RESPONSE_SIZE   = usize(1024 * 1024);

template<typename T>
auto publish_failure(RegistryPublishErrorKind kind,
                     const RegistryPackageId& package,
                     String                   message) -> RegistryPublishResult<T> {
    return Err(RegistryPublishError {
        .kind    = kind,
        .package = package.clone(),
        .message = rstd::move(message),
    });
}

template<typename T>
auto publish_failure(RegistryPublishErrorKind kind,
                     const RegistryPackageId& package,
                     ref<str>                 message) -> RegistryPublishResult<T> {
    return publish_failure<T>(kind, package, String::make(message));
}

auto request_body(const RegistryPublishRequest& request) -> String {
    auto archive = JsonMap::make();
    archive.insert(String::make("checksum"_str),
                   rstd::into<Json>(request.artifact.checksum.text().as_str()));
    archive.insert(String::make("size"_str),
                   rstd::into<Json>(request.artifact.size.text().as_str()));
    archive.insert(String::make("format"_str), rstd::into<Json>(request.artifact.format.as_str()));
    auto root = JsonMap::make();
    root.insert(String::make("registry"_str), rstd::into<Json>(request.package.registry.as_str()));
    root.insert(String::make("package"_str), rstd::into<Json>(request.package.name.as_str()));
    root.insert(String::make("version"_str), rstd::into<Json>(request.version.text()));
    root.insert(String::make("archive"_str), Json::Object(rstd::move(archive)));
    return rstd::json::to_string(Json::Object(rstd::move(root)));
}

auto api_url(const RegistryFixedEndpoint& api, ref<str> suffix) -> String {
    auto base = api.as_str();
    while (base.ends_with("/"_str)) base = base.get(usize {}, base.len() - usize(1)).unwrap();
    return rstd::format("{}{}", base, suffix);
}

auto session_id_valid(ref<str> value) noexcept -> bool {
    if (value.is_empty() || value.len() > usize(64)) return false;
    for (auto byte : value.as_bytes()) {
        auto raw = byte.to_primitive();
        if (! ((raw >= 'a' && raw <= 'z') || (raw >= 'A' && raw <= 'Z') ||
               (raw >= '0' && raw <= '9') || raw == '-')) {
            return false;
        }
    }
    return true;
}

auto header_name_valid(ref<str> value) noexcept -> bool {
    if (value.is_empty() || value.len() > usize(256)) return false;
    for (auto byte : value.as_bytes()) {
        auto raw = byte.to_primitive();
        if (raw <= 0x20 || raw >= 0x7f || raw == ':') return false;
    }
    return true;
}

auto header_value_valid(ref<str> value) noexcept -> bool {
    if (value.len() > usize(8192)) return false;
    for (auto byte : value.as_bytes()) {
        auto raw = byte.to_primitive();
        if (raw == '\r' || raw == '\n' || raw == 0) return false;
    }
    return true;
}

auto object(const Json& value, ref<str> context, const RegistryPackageId& package)
    -> RegistryPublishResult<ref<JsonMap>> {
    auto result = value.as_object();
    if (result.is_none()) {
        return publish_failure<ref<JsonMap>>(RegistryPublishErrorKind::Protocol,
                                             package,
                                             rstd::format("{} must be an object", context));
    }
    return Ok(*result);
}

auto known_field(ref<str> field, initializer_list<ref<str>> allowed) noexcept -> bool {
    for (auto candidate : allowed) {
        if (field == candidate) return true;
    }
    return false;
}

auto reject_unknown(const Json&                value,
                    ref<str>                   context,
                    initializer_list<ref<str>> allowed,
                    const RegistryPackageId&   package) -> RegistryPublishResult<empty> {
    auto members = rstd_try(object(value, context, package));
    for (auto key : members->keys()) {
        if (! known_field((*key).as_str(), allowed)) {
            return publish_failure<empty>(
                RegistryPublishErrorKind::Protocol,
                package,
                rstd::format("{} contains unknown field '{}'", context, (*key).as_str()));
        }
    }
    return Ok(empty {});
}

auto member(const Json& value, ref<str> key, ref<str> context, const RegistryPackageId& package)
    -> RegistryPublishResult<ref<Json>> {
    auto members = rstd_try(object(value, context, package));
    auto result  = members->get(key);
    if (result.is_none()) {
        return publish_failure<ref<Json>>(RegistryPublishErrorKind::Protocol,
                                          package,
                                          rstd::format("{} is missing '{}'", context, key));
    }
    return Ok(*result);
}

auto string_member(const Json&              value,
                   ref<str>                 key,
                   ref<str>                 context,
                   const RegistryPackageId& package) -> RegistryPublishResult<ref<str>> {
    auto value_member = rstd_try(member(value, key, context, package));
    auto text         = value_member->as_str();
    if (text.is_none()) {
        return publish_failure<ref<str>>(RegistryPublishErrorKind::Protocol,
                                         package,
                                         rstd::format("{}.{} must be a string", context, key));
    }
    return Ok(*text);
}

auto optional_string_member(const Json&              value,
                            ref<str>                 key,
                            ref<str>                 context,
                            const RegistryPackageId& package)
    -> RegistryPublishResult<Option<String>> {
    auto members = rstd_try(object(value, context, package));
    auto found   = members->get(key);
    if (found.is_none()) return Ok(Option<String> {});
    auto text = (**found).as_str();
    if (text.is_none()) {
        return publish_failure<Option<String>>(
            RegistryPublishErrorKind::Protocol,
            package,
            rstd::format("{}.{} must be a string", context, key));
    }
    return Ok(Some(String::make(*text)));
}

auto parse_publish_state(ref<str> value, const RegistryPackageId& package)
    -> RegistryPublishResult<RegistryPublishState> {
    if (value == "prepared"_str) return Ok(RegistryPublishState::Prepared);
    if (value == "uploaded"_str) return Ok(RegistryPublishState::Uploaded);
    if (value == "checking"_str) return Ok(RegistryPublishState::Checking);
    if (value == "check_retry"_str) return Ok(RegistryPublishState::CheckRetry);
    if (value == "check_failed"_str) return Ok(RegistryPublishState::CheckFailed);
    if (value == "rejected"_str) return Ok(RegistryPublishState::Rejected);
    if (value == "committed"_str) return Ok(RegistryPublishState::Committed);
    if (value == "projecting"_str) return Ok(RegistryPublishState::Projecting);
    if (value == "projection_retry"_str) return Ok(RegistryPublishState::ProjectionRetry);
    if (value == "visible"_str) return Ok(RegistryPublishState::Visible);
    if (value == "expired"_str) return Ok(RegistryPublishState::Expired);
    return publish_failure<RegistryPublishState>(
        RegistryPublishErrorKind::Protocol,
        package,
        rstd::format("publish session has unknown state '{}'", value));
}

struct ParsedPublishResponse {
    RegistryPublishSession     session;
    Option<String>             upload_url;
    Vec<RegistryPublishHeader> upload_headers;
};

auto parse_session_response(ref<str> input, const RegistryPublishRequest& expected, bool prepare)
    -> RegistryPublishResult<ParsedPublishResponse> {
    if (input.len() > MAXIMUM_RESPONSE_SIZE) {
        return publish_failure<ParsedPublishResponse>(
            RegistryPublishErrorKind::Protocol,
            expected.package,
            "Registry publish response exceeds 1 MiB"_str);
    }
    auto parsed =
        rstd::json::from_str(input, rstd::json::ParseOptions { .reject_duplicate_keys = true });
    if (parsed.is_err()) {
        return publish_failure<ParsedPublishResponse>(
            RegistryPublishErrorKind::Protocol,
            expected.package,
            rstd::format("Registry publish response is not strict JSON: {}",
                         rstd::move(parsed).unwrap_err()));
    }
    auto allowed = { "id"_str,      "state"_str,   "registry"_str,  "package"_str,
                     "version"_str, "archive"_str, "rejection"_str, "failure"_str,
                     "commit"_str,  "schema"_str,  "outcome"_str,   "upload"_str };
    rstd_try(reject_unknown(*parsed, "publish response"_str, allowed, expected.package));
    if (prepare) {
        if (rstd_try(
                string_member(*parsed, "schema"_str, "publish response"_str, expected.package)) !=
            PUBLISH_RESPONSE_SCHEMA) {
            return publish_failure<ParsedPublishResponse>(
                RegistryPublishErrorKind::Protocol,
                expected.package,
                "Registry publish response schema is unsupported"_str);
        }
        auto outcome = rstd_try(
            string_member(*parsed, "outcome"_str, "publish response"_str, expected.package));
        if (outcome != "created"_str && outcome != "existing"_str) {
            return publish_failure<ParsedPublishResponse>(
                RegistryPublishErrorKind::Protocol,
                expected.package,
                "Registry publish response has an invalid outcome"_str);
        }
    } else {
        auto root = rstd_try(object(*parsed, "publish response"_str, expected.package));
        if (root->get("schema"_str).is_some() || root->get("outcome"_str).is_some() ||
            root->get("upload"_str).is_some()) {
            return publish_failure<ParsedPublishResponse>(
                RegistryPublishErrorKind::Protocol,
                expected.package,
                "Registry publish status contains prepare-only fields"_str);
        }
    }

    auto id = rstd_try(string_member(*parsed, "id"_str, "publish response"_str, expected.package));
    if (! session_id_valid(id)) {
        return publish_failure<ParsedPublishResponse>(RegistryPublishErrorKind::Protocol,
                                                      expected.package,
                                                      "publish session ID is invalid"_str);
    }
    auto registry =
        rstd_try(string_member(*parsed, "registry"_str, "publish response"_str, expected.package));
    auto package =
        rstd_try(string_member(*parsed, "package"_str, "publish response"_str, expected.package));
    auto version =
        rstd_try(string_member(*parsed, "version"_str, "publish response"_str, expected.package));
    if (registry != expected.package.registry.as_str() ||
        package != expected.package.name.as_str() || version != expected.version.text()) {
        return publish_failure<ParsedPublishResponse>(
            RegistryPublishErrorKind::Protocol,
            expected.package,
            "Registry publish response context does not match the request"_str);
    }
    auto archive =
        rstd_try(member(*parsed, "archive"_str, "publish response"_str, expected.package));
    rstd_try(reject_unknown(*archive,
                            "publish response.archive"_str,
                            { "checksum"_str, "size"_str, "format"_str },
                            expected.package));
    if (rstd_try(string_member(
            *archive, "checksum"_str, "publish response.archive"_str, expected.package)) !=
            expected.artifact.checksum.text() ||
        rstd_try(string_member(
            *archive, "size"_str, "publish response.archive"_str, expected.package)) !=
            expected.artifact.size.text() ||
        rstd_try(string_member(
            *archive, "format"_str, "publish response.archive"_str, expected.package)) !=
            expected.artifact.format.as_str()) {
        return publish_failure<ParsedPublishResponse>(
            RegistryPublishErrorKind::Protocol,
            expected.package,
            "Registry publish response archive does not match the request"_str);
    }

    auto state     = rstd_try(parse_publish_state(
        rstd_try(string_member(*parsed, "state"_str, "publish response"_str, expected.package)),
        expected.package));
    auto rejection = rstd_try(
        optional_string_member(*parsed, "rejection"_str, "publish response"_str, expected.package));
    if ((state == RegistryPublishState::Rejected) != rejection.is_some()) {
        return publish_failure<ParsedPublishResponse>(
            RegistryPublishErrorKind::Protocol,
            expected.package,
            "Registry publish response rejection does not match its state"_str);
    }
    auto failure = rstd_try(
        optional_string_member(*parsed, "failure"_str, "publish response"_str, expected.package));
    if ((state == RegistryPublishState::CheckFailed) != failure.is_some()) {
        return publish_failure<ParsedPublishResponse>(
            RegistryPublishErrorKind::Protocol,
            expected.package,
            "Registry publish response failure does not match its state"_str);
    }
    auto checksum = Option<PackageChecksum> {};
    auto root     = rstd_try(object(*parsed, "publish response"_str, expected.package));
    auto commit   = root->get("commit"_str);
    if (commit.is_some()) {
        rstd_try(reject_unknown(**commit,
                                "publish response.commit"_str,
                                { "sequence"_str, "package_revision"_str, "checksum"_str },
                                expected.package));
        auto sequence = rstd_try(string_member(
            **commit, "sequence"_str, "publish response.commit"_str, expected.package));
        auto revision = rstd_try(string_member(
            **commit, "package_revision"_str, "publish response.commit"_str, expected.package));
        if (lito::parse::parse_canonical_u64_decimal(sequence).is_err() ||
            lito::parse::parse_canonical_u64_decimal(revision).is_err()) {
            return publish_failure<ParsedPublishResponse>(
                RegistryPublishErrorKind::Protocol,
                expected.package,
                "publish commit counters are invalid"_str);
        }
        auto parsed_checksum = PackageChecksum::parse(rstd_try(string_member(
            **commit, "checksum"_str, "publish response.commit"_str, expected.package)));
        if (parsed_checksum.is_err() || ! (*parsed_checksum == expected.artifact.checksum)) {
            return publish_failure<ParsedPublishResponse>(RegistryPublishErrorKind::Protocol,
                                                          expected.package,
                                                          "publish commit checksum is invalid"_str);
        }
        checksum = Some(rstd::move(parsed_checksum).unwrap());
    }
    const auto committed =
        state == RegistryPublishState::Committed || state == RegistryPublishState::Projecting ||
        state == RegistryPublishState::ProjectionRetry || state == RegistryPublishState::Visible;
    if (committed != checksum.is_some()) {
        return publish_failure<ParsedPublishResponse>(
            RegistryPublishErrorKind::Protocol,
            expected.package,
            "Registry publish response commit does not match its state"_str);
    }

    auto upload_url     = Option<String> {};
    auto upload_headers = Vec<RegistryPublishHeader>::make();
    auto upload         = root->get("upload"_str);
    if (upload.is_some()) {
        rstd_try(reject_unknown(**upload,
                                "publish response.upload"_str,
                                { "method"_str, "url"_str, "headers"_str, "expires_at"_str },
                                expected.package));
        if (rstd_try(string_member(
                **upload, "method"_str, "publish response.upload"_str, expected.package)) !=
            "PUT"_str) {
            return publish_failure<ParsedPublishResponse>(RegistryPublishErrorKind::Protocol,
                                                          expected.package,
                                                          "publish upload method must be PUT"_str);
        }
        auto url = rstd_try(
            string_member(**upload, "url"_str, "publish response.upload"_str, expected.package));
        if (lito::parse::HttpsUrl::parse(url).is_err()) {
            return publish_failure<ParsedPublishResponse>(RegistryPublishErrorKind::Protocol,
                                                          expected.package,
                                                          "publish upload URL must use HTTPS"_str);
        }
        auto expires = rstd_try(string_member(
            **upload, "expires_at"_str, "publish response.upload"_str, expected.package));
        if (expires.is_empty()) {
            return publish_failure<ParsedPublishResponse>(RegistryPublishErrorKind::Protocol,
                                                          expected.package,
                                                          "publish upload expiry is empty"_str);
        }
        auto headers = rstd_try(
            member(**upload, "headers"_str, "publish response.upload"_str, expected.package));
        auto header_map =
            rstd_try(object(*headers, "publish response.upload.headers"_str, expected.package));
        for (auto key : header_map->keys()) {
            auto value = header_map->get((*key).as_str()).unwrap()->as_str();
            if (value.is_none() || ! header_name_valid((*key).as_str()) ||
                ! header_value_valid(*value)) {
                return publish_failure<ParsedPublishResponse>(
                    RegistryPublishErrorKind::Protocol,
                    expected.package,
                    "publish upload contains an invalid HTTP header"_str);
            }
            upload_headers.push(RegistryPublishHeader {
                .name  = (*key).clone(),
                .value = String::make(*value),
            });
        }
        upload_url = Some(String::make(url));
    }
    if (state == RegistryPublishState::Prepared && upload_url.is_none()) {
        return publish_failure<ParsedPublishResponse>(RegistryPublishErrorKind::Protocol,
                                                      expected.package,
                                                      "prepared publish session has no upload"_str);
    }
    return Ok(ParsedPublishResponse {
        .session =
            RegistryPublishSession {
                .id        = String::make(id),
                .state     = state,
                .package   = expected.package.clone(),
                .version   = expected.version.clone(),
                .archive   = expected.artifact.clone(),
                .rejection = rstd::move(rejection),
                .failure   = rstd::move(failure),
                .checksum  = rstd::move(checksum),
            },
        .upload_url     = rstd::move(upload_url),
        .upload_headers = rstd::move(upload_headers),
    });
}

auto authorization_headers(const lito::config::RegistryBearerToken& token)
    -> Vec<RegistryPublishHeader> {
    auto headers = Vec<RegistryPublishHeader>::make();
    headers.push(RegistryPublishHeader {
        .name  = String::make("Authorization"_str),
        .value = token.authorization_header(),
    });
    headers.push(RegistryPublishHeader {
        .name  = String::make("Accept"_str),
        .value = String::make("application/json"_str),
    });
    return headers;
}

auto response_status_error(const RegistryPublishHttpResponse& response,
                           const RegistryPackageId&           package,
                           ref<str>                           operation)
    -> RegistryPublishResult<RegistryPublishHttpResponse> {
    auto kind = RegistryPublishErrorKind::Network;
    if (response.status == u16(401)) kind = RegistryPublishErrorKind::Authentication;
    if (response.status == u16(403)) kind = RegistryPublishErrorKind::Authorization;
    if (response.status == u16(409)) kind = RegistryPublishErrorKind::Conflict;
    if (response.status >= u16(400) && response.status < u16(500) && response.status != u16(401) &&
        response.status != u16(403) && response.status != u16(409)) {
        kind = RegistryPublishErrorKind::Protocol;
    }
    return publish_failure<RegistryPublishHttpResponse>(
        kind, package, rstd::format("{} returned HTTP {}", operation, response.status));
}

auto execute_request(RegistryPublishHttpTransport transport,
                     const RegistryPackageId&     package,
                     RegistryPublishHttpRequest   request,
                     initializer_list<u16>        accepted,
                     ref<str> operation) -> RegistryPublishResult<RegistryPublishHttpResponse> {
    if (transport.execute == nullptr) {
        return publish_failure<RegistryPublishHttpResponse>(
            RegistryPublishErrorKind::Network,
            package,
            "Registry publish has no HTTP transport"_str);
    }
    auto response = transport.execute(transport.context, package, request);
    if (response.is_err()) return Err(rstd::move(response).unwrap_err());
    for (auto status : accepted) {
        if (response->status == status) return response;
    }
    return response_status_error(*response, package, operation);
}

auto terminal_result(RegistryPublishSession session)
    -> RegistryPublishResult<RegistryPublishSession> {
    if (session.state == RegistryPublishState::Rejected) {
        return publish_failure<RegistryPublishSession>(
            RegistryPublishErrorKind::Rejected,
            session.package,
            rstd::format("Registry rejected publish session '{}': {}",
                         session.id.as_str(),
                         session.rejection->as_str()));
    }
    if (session.state == RegistryPublishState::CheckFailed) {
        return publish_failure<RegistryPublishSession>(
            RegistryPublishErrorKind::Infrastructure,
            session.package,
            rstd::format("Registry could not check publish session '{}': {}",
                         session.id.as_str(),
                         session.failure->as_str()));
    }
    if (session.state == RegistryPublishState::Expired) {
        return publish_failure<RegistryPublishSession>(
            RegistryPublishErrorKind::Expired,
            session.package,
            rstd::format("Registry publish session '{}' expired", session.id.as_str()));
    }
    return Ok(rstd::move(session));
}

auto curl_config_quote(ref<str> value, const RegistryPackageId& package, ref<str> context)
    -> RegistryPublishResult<String> {
    if (! header_value_valid(value)) {
        return publish_failure<String>(
            RegistryPublishErrorKind::Protocol,
            package,
            rstd::format("{} contains a forbidden control byte", context));
    }
    auto output = String::make();
    output.push_ascii('"');
    for (auto byte : value.as_bytes()) {
        auto raw = byte.to_primitive();
        if (raw == '\\' || raw == '"') output.push_ascii('\\');
        output.push_ascii(as_cast<u8>(raw));
    }
    output.push_ascii('"');
    return Ok(rstd::move(output));
}

auto append_curl_config(String&                  output,
                        ref<str>                 key,
                        ref<str>                 value,
                        const RegistryPackageId& package) -> RegistryPublishResult<empty> {
    auto quoted = rstd_try(curl_config_quote(value, package, key));
    output.push_str(key);
    output.push_str(" = "_str);
    output.push_str(quoted.as_str());
    output.push_ascii('\n');
    return Ok(empty {});
}

auto parse_curl_response(String output, const RegistryPackageId& package)
    -> RegistryPublishResult<RegistryPublishHttpResponse> {
    auto trailer = output.as_str().rfind(CURL_TRAILER);
    if (trailer.is_none()) {
        return publish_failure<RegistryPublishHttpResponse>(
            RegistryPublishErrorKind::Network,
            package,
            "curl output has no Registry publish HTTP status trailer"_str);
    }
    auto body = output.as_str().get(usize {}, *trailer).unwrap();
    if (body.len() > MAXIMUM_RESPONSE_SIZE) {
        return publish_failure<RegistryPublishHttpResponse>(
            RegistryPublishErrorKind::Protocol, package, "Registry response exceeds 1 MiB"_str);
    }
    auto status_text =
        output.as_str().get(*trailer + CURL_TRAILER.len(), output.len()).unwrap().trim_ascii();
    auto status = lito::parse::parse_canonical_u64_decimal(status_text);
    if (status.is_err() || *status > u64(999)) {
        return publish_failure<RegistryPublishHttpResponse>(
            RegistryPublishErrorKind::Network,
            package,
            "curl output has an invalid Registry publish HTTP status"_str);
    }
    return Ok(RegistryPublishHttpResponse {
        .status = as_cast<u16>(*status),
        .body   = String::make(body),
    });
}

} // namespace

auto lito::registry::RegistryPublishClient::publish(const RegistryPublishRequest& request)
    -> RegistryPublishResult<RegistryPublishSession> {
    if (request.token == nullptr) {
        return publish_failure<RegistryPublishSession>(
            RegistryPublishErrorKind::Authentication,
            request.package,
            "Registry publish requires a bearer token"_str);
    }
    auto body        = request_body(request);
    auto idempotency = rstd::format("lito-{}", licrypto::sha256_hex(body.as_str()));
    auto headers     = authorization_headers(*request.token);
    headers.push(RegistryPublishHeader {
        .name  = String::make("Content-Type"_str),
        .value = String::make("application/json"_str),
    });
    headers.push(RegistryPublishHeader {
        .name  = String::make("Idempotency-Key"_str),
        .value = rstd::move(idempotency),
    });
    auto prepared_response =
        rstd_try(execute_request(transport_,
                                 request.package,
                                 RegistryPublishHttpRequest {
                                     .method  = String::make("POST"_str),
                                     .url     = api_url(request.api, "/v1/publish/sessions"_str),
                                     .headers = rstd::move(headers),
                                     .body    = Some(rstd::move(body)),
                                 },
                                 { u16(200), u16(201) },
                                 "prepare publish session"_str));
    auto prepared =
        rstd_try(parse_session_response(prepared_response.body.as_str(), request, true));

    if (prepared.session.state == RegistryPublishState::Prepared) {
        auto upload_headers = rstd::move(prepared.upload_headers);
        auto uploaded =
            rstd_try(execute_request(transport_,
                                     request.package,
                                     RegistryPublishHttpRequest {
                                         .method  = String::make("PUT"_str),
                                         .url     = rstd::move(prepared.upload_url).unwrap(),
                                         .headers = rstd::move(upload_headers),
                                         .upload  = Some(request.archive.clone()),
                                     },
                                     { u16(200), u16(201), u16(204) },
                                     "upload publish archive"_str));
        (void)uploaded;
    }

    auto session = rstd::move(prepared.session);
    if (session.state == RegistryPublishState::Rejected ||
        session.state == RegistryPublishState::CheckFailed ||
        session.state == RegistryPublishState::Expired) {
        return terminal_result(rstd::move(session));
    }
    if (session.state == RegistryPublishState::Prepared ||
        session.state == RegistryPublishState::Uploaded) {
        auto submit_response = rstd_try(execute_request(
            transport_,
            request.package,
            RegistryPublishHttpRequest {
                .method = String::make("POST"_str),
                .url = api_url(request.api,
                               rstd::format("/v1/publish/sessions/{}/submit", session.id).as_str()),
                .headers = authorization_headers(*request.token),
            },
            { u16(200) },
            "submit publish session"_str));
        session              = rstd::move(
            rstd_try(parse_session_response(submit_response.body.as_str(), request, false))
                .session);
    }
    return terminal_result(rstd::move(session));
}

auto lito::registry::CurlRegistryPublishTransport::execute_callback(
    void*                             context,
    const RegistryPackageId&          package,
    const RegistryPublishHttpRequest& request) noexcept
    -> RegistryPublishResult<RegistryPublishHttpResponse> {
    return static_cast<CurlRegistryPublishTransport*>(context)->execute(package, request);
}

auto lito::registry::CurlRegistryPublishTransport::execute(
    const RegistryPackageId&          package,
    const RegistryPublishHttpRequest& request)
    -> RegistryPublishResult<RegistryPublishHttpResponse> {
    if (environment_ == nullptr) {
        return publish_failure<RegistryPublishHttpResponse>(
            RegistryPublishErrorKind::Network,
            package,
            "curl Registry publish transport has no process environment"_str);
    }
    auto executable = executable_.as_path().to_str();
    if (executable.is_none()) {
        return publish_failure<RegistryPublishHttpResponse>(
            RegistryPublishErrorKind::Io, package, "curl path is not valid UTF-8"_str);
    }
    auto temporary = rstd::fs::TempDir::make("lito-registry-publish"_str);
    if (temporary.is_err()) {
        return publish_failure<RegistryPublishHttpResponse>(
            RegistryPublishErrorKind::Io,
            package,
            rstd::format("cannot create Registry publish temporary directory: {}",
                         rstd::move(temporary).unwrap_err()));
    }
    auto temp      = rstd::move(temporary).unwrap();
    auto body_path = PathBuf::from(temp.path()).join(PathBuf::from("request.json"_str).as_path());
    if (request.body.is_some()) {
        auto file = rstd::fs::OpenOptions::make().write(true).create_new(true).mode(u32(0600)).open(
            body_path.as_path());
        if (file.is_err()) {
            return publish_failure<RegistryPublishHttpResponse>(
                RegistryPublishErrorKind::Io,
                package,
                rstd::format("cannot create Registry publish request body: {}",
                             rstd::move(file).unwrap_err()));
        }
        auto written = file->write_all(request.body->as_str().as_bytes());
        if (written.is_err()) {
            return publish_failure<RegistryPublishHttpResponse>(
                RegistryPublishErrorKind::Io,
                package,
                rstd::format("cannot write Registry publish request body: {}",
                             rstd::move(written).unwrap_err()));
        }
    }

    auto endpoint = RegistryFixedEndpoint::parse(request.url.as_str());
    if (endpoint.is_err()) {
        return publish_failure<RegistryPublishHttpResponse>(
            RegistryPublishErrorKind::Protocol,
            package,
            "Registry publish request URL is not an allowed endpoint"_str);
    }
    auto config = String::make("silent\nshow-error\ngloboff\n"_str);
    config.push_str(rstd::format("proto = \"={}\"\n", endpoint->scheme()).as_str());
    config.push_str("connect-timeout = 30\nmax-time = 300\nmax-filesize = 1048576\n"_str);
    rstd_try(append_curl_config(config, "request"_str, request.method.as_str(), package));
    rstd_try(append_curl_config(config, "url"_str, request.url.as_str(), package));
    for (const auto& header : request.headers) {
        if (! header_name_valid(header.name.as_str()) ||
            ! header_value_valid(header.value.as_str())) {
            return publish_failure<RegistryPublishHttpResponse>(
                RegistryPublishErrorKind::Protocol,
                package,
                "Registry publish request contains an invalid HTTP header"_str);
        }
        auto value = rstd::format("{}: {}", header.name, header.value);
        rstd_try(append_curl_config(config, "header"_str, value.as_str(), package));
    }
    if (request.body.is_some()) {
        auto path = body_path.as_path().to_str();
        if (path.is_none()) {
            return publish_failure<RegistryPublishHttpResponse>(
                RegistryPublishErrorKind::Io, package, "request body path is not UTF-8"_str);
        }
        auto value = rstd::format("@{}", *path);
        rstd_try(append_curl_config(config, "data-binary"_str, value.as_str(), package));
    }
    if (request.upload.is_some()) {
        auto path = request.upload->as_path().to_str();
        if (path.is_none()) {
            return publish_failure<RegistryPublishHttpResponse>(
                RegistryPublishErrorKind::Io, package, "upload archive path is not UTF-8"_str);
        }
        rstd_try(append_curl_config(config, "upload-file"_str, *path, package));
    }
    rstd_try(append_curl_config(
        config, "write-out"_str, "LITO_REGISTRY_PUBLISH_HTTP_V1:%{response_code}"_str, package));

    auto arguments = Vec<String>::make();
    arguments.push(String::make(*executable));
    arguments.push(String::make("--config"_str));
    arguments.push(String::make("-"_str));
    auto executed = lito::system::run_command_with_input(arguments, config.as_str(), *environment_);
    if (executed.is_err()) {
        return publish_failure<RegistryPublishHttpResponse>(
            RegistryPublishErrorKind::Network,
            package,
            "cannot execute Registry publish HTTP request"_str);
    }
    auto output = rstd::move(executed).unwrap();
    if (output.exit_code != i32 {}) {
        return publish_failure<RegistryPublishHttpResponse>(
            RegistryPublishErrorKind::Network,
            package,
            rstd::format("Registry publish HTTP request failed with curl exit code {}",
                         output.exit_code));
    }
    return parse_curl_response(rstd::move(output.standard_output), package);
}
