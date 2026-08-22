module;
#include <rstd/macro.hpp>

export module lito.driver:registry.index;

import rstd;
import lito.crypto;
import rstd.json;
import lito.core;
import lito.system;
import :config.registry;

using namespace rstd::prelude;
using namespace rstd::literals;
using PathBuf = rstd::path::PathBuf;

export namespace lito::registry
{

enum class RegistryNetworkPolicy
{
    Online,
    Offline,
};

struct RegistryHttpRequest {
    RegistryPackageId package;
    String            url;
    Option<String>    if_none_match;
};

struct RegistryHttpResponse {
    u16            status {};
    String         body;
    Option<String> etag;
};

using RegistryHttpResult = Result<RegistryHttpResponse, RegistryIndexError>;

struct RegistryHttpTransport {
    void* context {};
    RegistryHttpResult (*get)(void*, const RegistryHttpRequest&) noexcept {};
};

class RegistryIndexClient {
    PathBuf                  cache_root_;
    RegistryId               registry_;
    RegistryEndpointTemplate endpoint_;
    Ed25519PublicKey         trusted_key_;
    RegistryNetworkPolicy    network_ { RegistryNetworkPolicy::Online };
    RegistryHttpTransport    transport_;

    static auto load_provider(void* context, const RegistryPackageId& package) noexcept
        -> RegistryIndexLoadResult;

public:
    RegistryIndexClient(PathBuf                                  cache_root,
                        const lito::config::NamedRegistryConfig& config,
                        RegistryNetworkPolicy                    network,
                        RegistryHttpTransport                    transport)
        : cache_root_(rstd::move(cache_root)),
          registry_(config.identity.clone()),
          endpoint_(config.effective_endpoints()->index.clone()),
          trusted_key_(config.trusted_public_key.clone()),
          network_(network),
          transport_(transport) {}

    auto load(const RegistryPackageId& package) -> RegistryIndexLoadResult;
    auto provider() noexcept -> RegistryIndexProvider {
        return RegistryIndexProvider {
            .context = this,
            .load    = load_provider,
        };
    }
};

} // namespace lito::registry

namespace
{

using Json    = rstd::json::Value;
using JsonMap = rstd::json::Map;
using namespace lito::registry;

inline constexpr auto INDEX_CACHE_SCHEMA = "lito.registry.index-cache.v1"_str;
inline constexpr auto MAX_INDEX_BYTES    = usize(16 * 1024 * 1024);

template<typename T>
auto index_failure(RegistryIndexErrorKind kind, const RegistryPackageId& package, String message)
    -> Result<T, RegistryIndexError> {
    return Err(RegistryIndexError {
        .kind    = kind,
        .package = package.clone(),
        .message = rstd::move(message),
    });
}

template<typename T>
auto index_failure(RegistryIndexErrorKind kind, const RegistryPackageId& package, ref<str> message)
    -> Result<T, RegistryIndexError> {
    return index_failure<T>(kind, package, String::make(message));
}

auto cache_record_path(ref<rstd::path::Path> root, const RegistryPackageId& package) -> PathBuf {
    auto registry_key = lito::crypto::sha256_hex(package.registry.as_str());
    return PathBuf::from(root)
        .join(PathBuf::from("indices"_str).as_path())
        .join(PathBuf::from(registry_key).as_path())
        .join(PathBuf::from(package.name.as_str()).as_path())
        .join(PathBuf::from("record.json"_str).as_path());
}

auto cache_lock_path(ref<rstd::path::Path> record) -> PathBuf {
    return PathBuf::from(record.parent().unwrap()).join(PathBuf::from("lock"_str).as_path());
}

auto valid_etag(ref<str> value) -> bool {
    if (value.is_empty() || value.len() > usize(512)) return false;
    for (auto byte : value.as_bytes()) {
        auto raw = byte.to_primitive();
        if (raw < 0x21 || raw > 0x7e) return false;
    }
    return true;
}

auto required_string(const Json& value, ref<str> field) -> Option<ref<str>> {
    auto member = value.get(field);
    if (member.is_none()) return None();
    return (**member).as_str();
}

auto cache_fields_are_known(const JsonMap& object) -> bool {
    auto keys = object.keys();
    for (auto key = keys.next(); key.is_some(); key = keys.next()) {
        auto value = (**key).as_str();
        if (value == "schema"_str || value == "registry"_str || value == "package"_str ||
            value == "revision"_str || value == "sequence"_str || value == "key-id"_str ||
            value == "etag"_str || value == "fetched-at-unix-seconds"_str || value == "body"_str) {
            continue;
        }
        return false;
    }
    return true;
}

struct CachedPackageIndex {
    VerifiedPackageIndex index;
    String               body;
    Option<String>       etag;
};

auto corrupt_cache(const RegistryPackageId& package, ref<rstd::path::Path> record, ref<str> reason)
    -> Result<Option<CachedPackageIndex>, RegistryIndexError> {
    return index_failure<Option<CachedPackageIndex>>(
        RegistryIndexErrorKind::CorruptCache,
        package,
        rstd::format("Registry index cache record '{}': {}", record, reason));
}

auto read_cached_index(ref<rstd::path::Path>    record,
                       const RegistryPackageId& package,
                       const Ed25519PublicKey&  trusted_key)
    -> Result<Option<CachedPackageIndex>, RegistryIndexError> {
    auto contents = rstd::fs::read_to_string(record);
    if (contents.is_err()) {
        auto error = rstd::move(contents).unwrap_err();
        if (error.kind() == rstd::io::error::ErrorKind { rstd::io::error::ErrorKind::NotFound }) {
            return Ok(Option<CachedPackageIndex> {});
        }
        return index_failure<Option<CachedPackageIndex>>(
            RegistryIndexErrorKind::CorruptCache,
            package,
            rstd::format("cannot read Registry index cache record '{}': {}", record, error));
    }
    if (contents->len() > MAX_INDEX_BYTES * usize(2)) {
        return corrupt_cache(package, record, "record exceeds the supported size"_str);
    }
    auto parsed = rstd::json::from_str(contents->as_str(),
                                       rstd::json::ParseOptions { .reject_duplicate_keys = true });
    if (parsed.is_err()) return corrupt_cache(package, record, "record is not valid JSON"_str);
    auto object = parsed->as_object();
    if (object.is_none() || ! cache_fields_are_known(**object)) {
        return corrupt_cache(package, record, "record schema is invalid"_str);
    }
    auto schema   = required_string(*parsed, "schema"_str);
    auto registry = required_string(*parsed, "registry"_str);
    auto name     = required_string(*parsed, "package"_str);
    auto revision = required_string(*parsed, "revision"_str);
    auto sequence = required_string(*parsed, "sequence"_str);
    auto key_id   = required_string(*parsed, "key-id"_str);
    auto fetched  = required_string(*parsed, "fetched-at-unix-seconds"_str);
    auto body     = required_string(*parsed, "body"_str);
    if (schema.is_none() || registry.is_none() || name.is_none() || revision.is_none() ||
        sequence.is_none() || key_id.is_none() || fetched.is_none() || body.is_none() ||
        *schema != INDEX_CACHE_SCHEMA || *registry != package.registry.as_str() ||
        *name != package.name.as_str() || body->len() > MAX_INDEX_BYTES ||
        lito::parse::parse_canonical_u64_decimal(*revision).is_err() ||
        lito::parse::parse_canonical_u64_decimal(*sequence).is_err() ||
        lito::parse::parse_canonical_u64_decimal(*fetched).is_err()) {
        return corrupt_cache(package, record, "record fields are invalid"_str);
    }
    auto expected_key = signing_key_id(trusted_key);
    if (expected_key.is_err() || expected_key->text().as_str() != *key_id) {
        return corrupt_cache(package, record, "record is bound to another signing key"_str);
    }
    auto etag       = Option<String> {};
    auto etag_value = parsed->get("etag"_str);
    if (etag_value.is_none()) return corrupt_cache(package, record, "record has no ETag field"_str);
    if (! (**etag_value).is_null()) {
        auto text = (**etag_value).as_str();
        if (text.is_none() || ! valid_etag(*text)) {
            return corrupt_cache(package, record, "record has an invalid ETag"_str);
        }
        etag = Some(String::make(*text));
    }
    auto verified = parse_verified_package_index(body->as_bytes(), package, trusted_key);
    if (verified.is_err()) {
        return corrupt_cache(package, record, "cached envelope no longer verifies"_str);
    }
    auto parsed_revision = lito::parse::parse_canonical_u64_decimal(*revision).unwrap();
    auto parsed_sequence = lito::parse::parse_canonical_u64_decimal(*sequence).unwrap();
    if (verified->revision() != parsed_revision || verified->sequence() != parsed_sequence ||
        verified->verified_key().text().as_str() != *key_id) {
        return corrupt_cache(package, record, "receipt does not match the verified envelope"_str);
    }
    return Ok(Some(CachedPackageIndex {
        .index = rstd::move(verified).unwrap(),
        .body  = String::make(*body),
        .etag  = rstd::move(etag),
    }));
}

auto json_string(ref<str> value) -> Json {
    return Json::String(String::make(value));
}

auto write_cached_index(ref<rstd::path::Path>       record,
                        const RegistryPackageId&    package,
                        const VerifiedPackageIndex& index,
                        ref<str>                    body,
                        const Option<String>&       etag) -> Result<empty, RegistryIndexError> {
    auto parent = record.parent();
    if (parent.is_none()) {
        return index_failure<empty>(RegistryIndexErrorKind::CorruptCache,
                                    package,
                                    "Registry index cache path has no parent"_str);
    }
    auto created = rstd::fs::create_dir_all(*parent);
    if (created.is_err()) {
        return index_failure<empty>(
            RegistryIndexErrorKind::CorruptCache,
            package,
            rstd::format("cannot create Registry index cache directory '{}': {}",
                         *parent,
                         rstd::move(created).unwrap_err()));
    }
    auto document = JsonMap::make();
    document.insert(String::make("schema"_str), json_string(INDEX_CACHE_SCHEMA));
    document.insert(String::make("registry"_str), json_string(package.registry.as_str()));
    document.insert(String::make("package"_str), json_string(package.name.as_str()));
    document.insert(String::make("revision"_str),
                    json_string(rstd::format("{}", index.revision()).as_str()));
    document.insert(String::make("sequence"_str),
                    json_string(rstd::format("{}", index.sequence()).as_str()));
    document.insert(String::make("key-id"_str), json_string(index.verified_key().text().as_str()));
    document.insert(String::make("etag"_str),
                    etag.is_some() ? json_string(etag->as_str()) : Json::Null());
    auto now = rstd::time::SystemTime::now().as_unix_time();
    document.insert(String::make("fetched-at-unix-seconds"_str),
                    json_string(rstd::format("{}", now.seconds).as_str()));
    document.insert(String::make("body"_str), json_string(body));
    auto text = rstd::json::to_string(Json::Object(rstd::move(document)));
    text.push_ascii(u8('\n'));
    auto written = rstd::fs::write_atomic(record, text.as_str().as_bytes());
    if (written.is_err()) {
        return index_failure<empty>(
            RegistryIndexErrorKind::CorruptCache,
            package,
            rstd::format("cannot atomically write Registry index cache record '{}': {}",
                         record,
                         rstd::move(written).unwrap_err()));
    }
    return Ok(empty {});
}

auto acquire_cache_lock(ref<rstd::path::Path> record, const RegistryPackageId& package)
    -> Result<rstd::fs::FileLock, RegistryIndexError> {
    auto parent = record.parent();
    if (parent.is_none()) {
        return index_failure<rstd::fs::FileLock>(RegistryIndexErrorKind::CorruptCache,
                                                 package,
                                                 "Registry index cache path has no parent"_str);
    }
    auto created = rstd::fs::create_dir_all(*parent);
    if (created.is_err()) {
        return index_failure<rstd::fs::FileLock>(
            RegistryIndexErrorKind::CorruptCache,
            package,
            rstd::format("cannot create Registry index cache directory '{}': {}",
                         *parent,
                         rstd::move(created).unwrap_err()));
    }
    auto lock = cache_lock_path(record);
    auto opened =
        rstd::fs::OpenOptions::make().read(true).write(true).create(true).open(lock.as_path());
    if (opened.is_err()) {
        return index_failure<rstd::fs::FileLock>(
            RegistryIndexErrorKind::CorruptCache,
            package,
            rstd::format("cannot open Registry index cache lock '{}': {}",
                         lock.as_path(),
                         rstd::move(opened).unwrap_err()));
    }
    auto locked =
        rstd::fs::FileLock::acquire(rstd::move(opened).unwrap(), rstd::fs::FileLockMode::Exclusive);
    if (locked.is_err()) {
        return index_failure<rstd::fs::FileLock>(
            RegistryIndexErrorKind::CorruptCache,
            package,
            rstd::format("cannot acquire Registry index cache lock '{}': {}",
                         lock.as_path(),
                         rstd::move(locked).unwrap_err()));
    }
    return Ok(rstd::move(locked).unwrap());
}

auto reject_rollback(const VerifiedPackageIndex& incoming, const VerifiedPackageIndex& current)
    -> Result<empty, RegistryIndexError> {
    const auto& package = incoming.package();
    if (incoming.revision() < current.revision() || incoming.sequence() < current.sequence() ||
        (incoming.revision() > current.revision() && incoming.sequence() <= current.sequence())) {
        return index_failure<empty>(
            RegistryIndexErrorKind::Rollback,
            package,
            rstd::format("Registry index revision/sequence {}/{} rolls back cached {}/{}",
                         incoming.revision(),
                         incoming.sequence(),
                         current.revision(),
                         current.sequence()));
    }
    if (incoming.revision() == current.revision() &&
        incoming.canonical_signed() != current.canonical_signed()) {
        return index_failure<empty>(
            RegistryIndexErrorKind::Integrity,
            package,
            rstd::format("Registry index revision {} changed its signed payload",
                         incoming.revision()));
    }
    return Ok(empty {});
}

auto commit_cached_index(ref<rstd::path::Path>       record,
                         const RegistryPackageId&    package,
                         const Ed25519PublicKey&     trusted_key,
                         const VerifiedPackageIndex& incoming,
                         ref<str>                    body,
                         const Option<String>&       etag) -> Result<empty, RegistryIndexError> {
    auto lock    = rstd_try(acquire_cache_lock(record, package));
    auto current = read_cached_index(record, package, trusted_key);
    if (current.is_ok() && current->is_some()) {
        rstd_try(reject_rollback(incoming, current->as_ref().unwrap().index));
    }
    return write_cached_index(record, package, incoming, body, etag);
}

auto http_status_failure(const RegistryHttpResponse& response, const RegistryPackageId& package)
    -> RegistryIndexLoadResult {
    auto kind = RegistryIndexErrorKind::Network;
    if (response.status == u16(404)) kind = RegistryIndexErrorKind::NotFound;
    if (response.status == u16(410)) kind = RegistryIndexErrorKind::Gone;
    if (response.status == u16(451)) kind = RegistryIndexErrorKind::LegalUnavailable;
    return index_failure<VerifiedPackageIndex>(
        kind, package, rstd::format("Registry index request returned HTTP {}", response.status));
}

} // namespace

auto lito::registry::RegistryIndexClient::load_provider(void*                    context,
                                                        const RegistryPackageId& package) noexcept
    -> RegistryIndexLoadResult {
    return static_cast<RegistryIndexClient*>(context)->load(package);
}

auto lito::registry::RegistryIndexClient::load(const RegistryPackageId& package)
    -> RegistryIndexLoadResult {
    if (! (package.registry == registry_)) {
        return index_failure<VerifiedPackageIndex>(
            RegistryIndexErrorKind::ContextMismatch,
            package,
            "Registry index client cannot serve another registry identity"_str);
    }
    auto record = cache_record_path(cache_root_.as_path(), package);
    auto cached = read_cached_index(record.as_path(), package, trusted_key_);
    if (network_ == RegistryNetworkPolicy::Offline) {
        if (cached.is_err()) return Err(rstd::move(cached).unwrap_err());
        if (cached->is_none()) {
            return index_failure<VerifiedPackageIndex>(
                RegistryIndexErrorKind::OfflineCacheMiss,
                package,
                "offline Registry resolve has no verified package index cache entry"_str);
        }
        return Ok(cached->as_ref().unwrap().index.clone());
    }

    auto request = RegistryHttpRequest {
        .package = package.clone(),
        .url     = endpoint_.render(package.name.as_str()),
    };
    if (cached.is_ok() && cached->is_some() && cached->as_ref().unwrap().etag.is_some()) {
        request.if_none_match = Some(cached->as_ref().unwrap().etag->clone());
    }
    if (transport_.get == nullptr) {
        return index_failure<VerifiedPackageIndex>(
            RegistryIndexErrorKind::Network,
            package,
            "online Registry resolve has no HTTP transport"_str);
    }
    auto response = transport_.get(transport_.context, request);
    if (response.is_err()) return Err(rstd::move(response).unwrap_err());
    auto received = rstd::move(response).unwrap();
    if (received.etag.is_some() && ! valid_etag(received.etag->as_str())) {
        return index_failure<VerifiedPackageIndex>(
            RegistryIndexErrorKind::Schema,
            package,
            "Registry index response has an invalid ETag"_str);
    }
    if (received.status == u16(304)) {
        if (cached.is_err() || cached->is_none()) {
            return index_failure<VerifiedPackageIndex>(
                RegistryIndexErrorKind::CorruptCache,
                package,
                "Registry returned 304 without a verified matching cache entry"_str);
        }
        auto& current = cached->as_ref().unwrap();
        auto  etag    = received.etag.is_some() ? received.etag.clone() : current.etag.clone();
        rstd_try(commit_cached_index(
            record.as_path(), package, trusted_key_, current.index, current.body.as_str(), etag));
        return Ok(current.index.clone());
    }
    if (received.status != u16(200)) return http_status_failure(received, package);
    if (received.body.len() > MAX_INDEX_BYTES) {
        return index_failure<VerifiedPackageIndex>(
            RegistryIndexErrorKind::Schema, package, "Registry index response exceeds 16 MiB"_str);
    }
    auto verified =
        parse_verified_package_index(received.body.as_str().as_bytes(), package, trusted_key_);
    if (verified.is_err()) {
        return index_failure<VerifiedPackageIndex>(
            RegistryIndexErrorKind::Schema,
            package,
            rstd::format("Registry index response does not verify: {}",
                         rstd::move(verified).unwrap_err()));
    }
    if (cached.is_ok() && cached->is_some()) {
        rstd_try(reject_rollback(*verified, cached->as_ref().unwrap().index));
    }
    rstd_try(commit_cached_index(
        record.as_path(), package, trusted_key_, *verified, received.body.as_str(), received.etag));
    return Ok(rstd::move(verified).unwrap());
}
