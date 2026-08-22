module;
#include <rstd/macro.hpp>

module lito.core;

import rstd;
import lito.crypto;
import rstd.json;
import rstd.toml;
import :parse;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito::parse;

namespace
{

auto ascii_alpha(u8 value) noexcept -> bool {
    const auto byte = value.to_primitive();
    return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z');
}

auto ascii_digit(u8 value) noexcept -> bool {
    const auto byte = value.to_primitive();
    return byte >= '0' && byte <= '9';
}

auto ascii_hex(u8 value) noexcept -> bool {
    const auto byte = value.to_primitive();
    return (byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f') ||
           (byte >= 'A' && byte <= 'F');
}

auto lower_ascii(u8 value) noexcept -> u8 {
    const auto byte = value.to_primitive();
    if (byte >= 'A' && byte <= 'Z') return u8(byte + ('a' - 'A'));
    return value;
}

auto json_kind(const rstd::json::Value& value) noexcept -> ValueKind {
    if (value.is_null()) return ValueKind::Null;
    if (value.is_boolean()) return ValueKind::Boolean;
    if (value.is_string()) return ValueKind::String;
    if (value.is_array()) return ValueKind::Array;
    if (value.is_object()) return ValueKind::Object;
    if (value.is_u64()) return ValueKind::UnsignedInteger;
    if (value.is_i64()) return ValueKind::Integer;
    return ValueKind::Number;
}

auto toml_kind(const rstd::toml::Value& value) noexcept -> ValueKind {
    if (value.is_boolean()) return ValueKind::Boolean;
    if (value.is_integer()) return ValueKind::Integer;
    if (value.is_float()) return ValueKind::Number;
    if (value.is_string()) return ValueKind::String;
    if (value.is_array()) return ValueKind::Array;
    if (value.is_table()) return ValueKind::Table;
    if (value.is_local_date()) return ValueKind::Date;
    if (value.is_local_time()) return ValueKind::Time;
    return ValueKind::DateTime;
}

template<typename T, typename E>
auto invalid_value(ref<NodePath> path, E error) -> ParseResult<T> {
    return Err(Error::InvalidValue(path->clone(), rstd::format("{}", error)));
}

} // namespace

auto NodeSegment::field(ref<str> value) -> NodeSegment {
    auto result   = NodeSegment {};
    result.kind_  = NodeSegmentKind::Field;
    result.field_ = String::make(value);
    return result;
}

auto NodeSegment::index(usize value) -> NodeSegment {
    auto result   = NodeSegment {};
    result.kind_  = NodeSegmentKind::Index;
    result.index_ = value;
    return result;
}

auto NodeSegment::clone() const -> NodeSegment {
    auto result   = NodeSegment {};
    result.kind_  = kind_;
    result.field_ = field_.clone();
    result.index_ = index_;
    return result;
}

auto NodePath::root(ref<str> value) -> NodePath {
    auto result  = NodePath {};
    result.root_ = String::make(value);
    return result;
}

auto NodePath::field(ref<str> value) const -> NodePath {
    auto result = clone();
    result.segments_.push(NodeSegment::field(value));
    return result;
}

auto NodePath::index(usize value) const -> NodePath {
    auto result = clone();
    result.segments_.push(NodeSegment::index(value));
    return result;
}

auto NodePath::clone() const -> NodePath {
    auto segments = Vec<NodeSegment>::with_capacity(segments_.len());
    for (const auto& segment : segments_) segments.push(segment.clone());
    auto result      = NodePath {};
    result.root_     = root_.clone();
    result.segments_ = rstd::move(segments);
    return result;
}

auto lito::parse::value_kind_name(ValueKind kind) noexcept -> ref<str> {
    switch (kind) {
    case ValueKind::Null: return "null"_str;
    case ValueKind::Boolean: return "a boolean"_str;
    case ValueKind::Integer: return "an integer"_str;
    case ValueKind::UnsignedInteger: return "an unsigned integer"_str;
    case ValueKind::Number: return "a number"_str;
    case ValueKind::String: return "a string"_str;
    case ValueKind::Array: return "an array"_str;
    case ValueKind::Object: return "an object"_str;
    case ValueKind::Table: return "a table"_str;
    case ValueKind::DateTime: return "a date-time"_str;
    case ValueKind::Date: return "a date"_str;
    case ValueKind::Time: return "a time"_str;
    }
    return "a value"_str;
}

auto lito::parse::parse_sha256(ref<str> value, Sha256TextMode mode)
    -> Result<lito::crypto::Sha256Digest, Sha256Error> {
    if (mode == Sha256TextMode::Canonical) {
        for (auto index = usize {}; index < value.len(); ++index) {
            const auto byte = value[index].to_primitive();
            if (byte >= 'A' && byte <= 'F') return Err(Sha256Error::NonCanonicalCase(index));
        }
    }
    auto parsed = lito::crypto::Sha256Digest::parse_hex(value);
    if (parsed.is_err()) return Err(Sha256Error::Digest(rstd::move(parsed).unwrap_err()));
    return Ok(rstd::move(parsed).unwrap());
}

auto Url::parse(ref<str> value) -> Result<Url, UrlError> {
    if (value.is_empty()) return Err(UrlError::Empty());
    if (! ascii_alpha(value[usize {}])) return Err(UrlError::MissingScheme());

    auto scheme_end = usize(1);
    while (scheme_end < value.len() && value[scheme_end] != u8(':')) {
        const auto byte = value[scheme_end];
        if (! ascii_alpha(byte) && ! ascii_digit(byte) && byte != u8('+') && byte != u8('-') &&
            byte != u8('.')) {
            return Err(UrlError::InvalidScheme(scheme_end));
        }
        ++scheme_end;
    }
    if (scheme_end >= value.len() || value[scheme_end] != u8(':')) {
        return Err(UrlError::MissingScheme());
    }
    if (scheme_end + usize(2) >= value.len() || value[scheme_end + usize(1)] != u8('/') ||
        value[scheme_end + usize(2)] != u8('/')) {
        return Err(UrlError::MissingAuthority());
    }

    const auto authority_begin = scheme_end + usize(3);
    auto       authority_end   = authority_begin;
    while (authority_end < value.len() && value[authority_end] != u8('/') &&
           value[authority_end] != u8('?') && value[authority_end] != u8('#')) {
        ++authority_end;
    }
    auto query_begin    = value.len();
    auto fragment_begin = value.len();
    for (auto index = usize {}; index < value.len(); ++index) {
        const auto byte = value[index];
        const auto raw  = byte.to_primitive();
        if (raw <= 0x20 || raw == 0x7f || raw == '\\' || raw == '"' || raw == '<' || raw == '>' ||
            raw == '`') {
            return Err(UrlError::InvalidCharacter(index));
        }
        if (byte == u8('%')) {
            if (index + usize(2) >= value.len() || ! ascii_hex(value[index + usize(1)]) ||
                ! ascii_hex(value[index + usize(2)])) {
                return Err(UrlError::InvalidPercentEncoding(index));
            }
            index += usize(2);
            continue;
        }
        if (byte == u8('?') && query_begin == value.len() && fragment_begin == value.len()) {
            query_begin = index;
        } else if (byte == u8('#') && fragment_begin == value.len()) {
            fragment_begin = index;
        }
    }

    auto canonical = String::make();
    canonical.reserve(value.len());
    for (auto index = usize {}; index < scheme_end; ++index) {
        canonical.push_ascii(lower_ascii(value[index]));
    }
    auto suffix = value.get(scheme_end, value.len());
    if (suffix.is_some()) canonical.push_str(*suffix);
    return Ok(Url(rstd::move(canonical),
                  scheme_end,
                  authority_begin,
                  authority_end,
                  authority_end,
                  query_begin,
                  fragment_begin));
}

auto Url::scheme() const noexcept -> ref<str> {
    return *value_.as_str().get(usize {}, scheme_end_);
}

auto Url::authority() const noexcept -> ref<str> {
    return *value_.as_str().get(authority_begin_, authority_end_);
}

auto Url::path() const noexcept -> ref<str> {
    auto end = query_begin_ < fragment_begin_ ? query_begin_ : fragment_begin_;
    return *value_.as_str().get(path_begin_, end);
}

auto Url::fragment() const noexcept -> Option<ref<str>> {
    if (fragment_begin_ == value_.len()) return None();
    return value_.as_str().get(fragment_begin_ + usize(1), value_.len());
}

auto Url::clone() const -> Url {
    return Url(value_.clone(),
               scheme_end_,
               authority_begin_,
               authority_end_,
               path_begin_,
               query_begin_,
               fragment_begin_);
}

auto FetchUrl::parse(ref<str> value) -> Result<FetchUrl, UrlError> {
    return try_from(rstd_try(Url::parse(value)));
}

auto FetchUrl::try_from(Url value) -> Result<FetchUrl, UrlError> {
    if (value.fragment().is_some()) return Err(UrlError::FragmentNotAllowed());
    if (value.scheme() != "http"_str && value.scheme() != "https"_str &&
        value.scheme() != "file"_str) {
        return Err(UrlError::UnsupportedFetchScheme(String::make(value.scheme())));
    }
    if (value.scheme() != "file"_str && value.authority().is_empty()) {
        return Err(UrlError::MissingAuthority());
    }
    return Ok(FetchUrl(rstd::move(value)));
}

auto HttpsUrl::parse(ref<str> value) -> Result<HttpsUrl, UrlError> {
    return try_from(rstd_try(FetchUrl::parse(value)));
}

auto HttpsUrl::try_from(FetchUrl value) -> Result<HttpsUrl, UrlError> {
    if (value.url()->scheme() != "https"_str) {
        return Err(UrlError::HttpsRequired(String::make(value.url()->scheme())));
    }
    return Ok(HttpsUrl(rstd::move(value)));
}

auto NormalRelativePath::parse(ref<str> value) -> Result<NormalRelativePath, PathValueError> {
    return parse(rstd::path::PathBuf::from(value));
}

auto NormalRelativePath::parse(rstd::path::PathBuf value)
    -> Result<NormalRelativePath, PathValueError> {
    if (value.is_empty()) return Err(PathValueError::Empty());
    if (value.as_path().is_absolute() || value.as_path().has_root()) {
        return Err(PathValueError::Absolute());
    }
    auto components = value.as_path().components();
    auto count      = usize {};
    for (auto component : components) {
        if (! component.is_normal()) return Err(PathValueError::NonNormalComponent());
        ++count;
    }
    if (count == usize {}) return Err(PathValueError::Empty());
    return Ok(NormalRelativePath(rstd::move(value)));
}

auto PathComponent::parse(ref<str> value) -> Result<PathComponent, PathValueError> {
    return parse(rstd::path::PathBuf::from(value));
}

auto PathComponent::parse(rstd::path::PathBuf value) -> Result<PathComponent, PathValueError> {
    if (value.is_empty()) return Err(PathValueError::Empty());
    if (value.as_path().is_absolute() || value.as_path().has_root()) {
        return Err(PathValueError::Absolute());
    }
    auto components = value.as_path().components();
    auto first      = components.next();
    if (first.is_none()) return Err(PathValueError::Empty());
    if (! first->is_normal()) return Err(PathValueError::NonNormalComponent());
    if (components.next().is_some()) return Err(PathValueError::MultipleComponents());
    return Ok(PathComponent(rstd::move(value)));
}

auto lito::parse::parse_canonical_u64_decimal(ref<str> value) -> Result<u64, DecimalError> {
    if (value.is_empty()) return Err(DecimalError::Empty());
    if (value.len() > usize(1) && value[usize {}] == u8('0')) {
        return Err(DecimalError::LeadingZero());
    }
    auto result = u64 {};
    for (auto index = usize {}; index < value.len(); ++index) {
        const auto byte = value[index];
        if (! ascii_digit(byte)) return Err(DecimalError::Character(index));
        const auto digit = u64((byte - u8('0')).to_primitive());
        if (result > (u64::MAX - digit) / u64(10)) return Err(DecimalError::Overflow());
        result = result * u64(10) + digit;
    }
    return Ok(result);
}

auto lito::parse::json::kind(const rstd::json::Value& value) noexcept -> ValueKind {
    return json_kind(value);
}

auto lito::parse::json::object(const rstd::json::Value& value, const NodePath& path)
    -> ParseResult<ref<rstd::json::Map>> {
    auto parsed = value.as_object();
    if (parsed.is_none())
        return Err(Error::WrongType(path.clone(), ValueKind::Object, kind(value)));
    return Ok(*parsed);
}

auto lito::parse::json::array(const rstd::json::Value& value, const NodePath& path)
    -> ParseResult<ref<rstd::json::Array>> {
    auto parsed = value.as_array();
    if (parsed.is_none()) return Err(Error::WrongType(path.clone(), ValueKind::Array, kind(value)));
    return Ok(*parsed);
}

auto lito::parse::json::string(const rstd::json::Value& value, const NodePath& path)
    -> ParseResult<ref<str>> {
    auto parsed = value.as_str();
    if (parsed.is_none())
        return Err(Error::WrongType(path.clone(), ValueKind::String, kind(value)));
    return Ok(*parsed);
}

auto lito::parse::json::non_empty_string(const rstd::json::Value& value, const NodePath& path)
    -> ParseResult<String> {
    auto parsed = rstd_try(string(value, path));
    if (parsed.is_empty()) return Err(Error::EmptyValue(path.clone()));
    return Ok(String::make(parsed));
}

auto lito::parse::json::u64_value(const rstd::json::Value& value, const NodePath& path)
    -> ParseResult<u64> {
    auto parsed = value.as_u64();
    if (parsed.is_none()) {
        return Err(Error::WrongType(path.clone(), ValueKind::UnsignedInteger, kind(value)));
    }
    return Ok(*parsed);
}

auto lito::parse::json::boolean(const rstd::json::Value& value, const NodePath& path)
    -> ParseResult<bool> {
    auto parsed = value.as_bool();
    if (parsed.is_none())
        return Err(Error::WrongType(path.clone(), ValueKind::Boolean, kind(value)));
    return Ok(*parsed);
}

auto lito::parse::json::required_member(const rstd::json::Value& value,
                                        ref<str>                 key,
                                        const NodePath&          path)
    -> ParseResult<ref<rstd::json::Value>> {
    rstd_try(object(value, path));
    auto member = value.get(key);
    if (member.is_none()) return Err(Error::MissingField(path.clone(), String::make(key)));
    return Ok(*member);
}

auto lito::parse::json::required_string(const rstd::json::Value& value,
                                        ref<str>                 key,
                                        const NodePath&          path) -> ParseResult<String> {
    auto member = rstd_try(required_member(value, key, path));
    return Ok(String::make(rstd_try(string(*member, path.field(key)))));
}

auto lito::parse::json::required_non_empty_string(const rstd::json::Value& value,
                                                  ref<str>                 key,
                                                  const NodePath& path) -> ParseResult<String> {
    auto member = rstd_try(required_member(value, key, path));
    return non_empty_string(*member, path.field(key));
}

auto lito::parse::json::required_u64(const rstd::json::Value& value,
                                     ref<str>                 key,
                                     const NodePath&          path) -> ParseResult<u64> {
    auto member = rstd_try(required_member(value, key, path));
    return u64_value(*member, path.field(key));
}

auto lito::parse::json::required_array(const rstd::json::Value& value,
                                       ref<str>                 key,
                                       const NodePath&          path)
    -> ParseResult<ref<rstd::json::Array>> {
    auto member = rstd_try(required_member(value, key, path));
    return array(*member, path.field(key));
}

auto lito::parse::json::required_sha256(const rstd::json::Value& value,
                                        ref<str>                 key,
                                        const NodePath&          path,
                                        Sha256TextMode           mode)
    -> ParseResult<lito::crypto::Sha256Digest> {
    auto member = rstd_try(required_member(value, key, path));
    return sha256(*member, path.field(key), mode);
}

auto lito::parse::json::required_fetch_url(const rstd::json::Value& value,
                                           ref<str>                 key,
                                           const NodePath&          path) -> ParseResult<FetchUrl> {
    auto member = rstd_try(required_member(value, key, path));
    return fetch_url(*member, path.field(key));
}

auto lito::parse::json::required_https_url(const rstd::json::Value& value,
                                           ref<str>                 key,
                                           const NodePath&          path) -> ParseResult<HttpsUrl> {
    auto member = rstd_try(required_member(value, key, path));
    return https_url(*member, path.field(key));
}

auto lito::parse::json::required_normal_relative_path(const rstd::json::Value& value,
                                                      ref<str>                 key,
                                                      const NodePath&          path)
    -> ParseResult<NormalRelativePath> {
    auto member = rstd_try(required_member(value, key, path));
    return normal_relative_path(*member, path.field(key));
}

auto lito::parse::json::required_path_component(const rstd::json::Value& value,
                                                ref<str>                 key,
                                                const NodePath&          path)
    -> ParseResult<PathComponent> {
    auto member = rstd_try(required_member(value, key, path));
    return path_component(*member, path.field(key));
}

auto lito::parse::json::reject_unknown(const rstd::json::Map&     value,
                                       const NodePath&            path,
                                       initializer_list<ref<str>> allowed) -> ParseResult<empty> {
    auto keys = value.keys();
    for (auto key : keys) {
        auto known = false;
        for (const auto candidate : allowed) {
            if ((*key).as_str() == candidate) {
                known = true;
                break;
            }
        }
        if (! known) return Err(Error::UnknownField(path.clone(), (*key).clone()));
    }
    return Ok(empty {});
}

auto lito::parse::json::reject_unknown(const rstd::json::Map& value,
                                       const NodePath&        path,
                                       bool (*allowed)(ref<str>)) -> ParseResult<empty> {
    auto keys = value.keys();
    for (auto key : keys) {
        if (! allowed((*key).as_str())) {
            return Err(Error::UnknownField(path.clone(), (*key).clone()));
        }
    }
    return Ok(empty {});
}

auto lito::parse::json::reject_unknown(const rstd::json::Value&   value,
                                       const NodePath&            path,
                                       initializer_list<ref<str>> allowed) -> ParseResult<empty> {
    return reject_unknown(*rstd_try(object(value, path)), path, allowed);
}

auto lito::parse::json::reject_unknown(const rstd::json::Value& value,
                                       const NodePath&          path,
                                       bool (*allowed)(ref<str>)) -> ParseResult<empty> {
    return reject_unknown(*rstd_try(object(value, path)), path, allowed);
}

auto lito::parse::json::sha256(const rstd::json::Value& value,
                               const NodePath&          path,
                               Sha256TextMode mode) -> ParseResult<lito::crypto::Sha256Digest> {
    auto parsed = parse_sha256(rstd_try(string(value, path)), mode);
    if (parsed.is_err()) {
        return invalid_value<lito::crypto::Sha256Digest>(ref<NodePath>::from_raw_parts(&path),
                                                         rstd::move(parsed).unwrap_err());
    }
    return Ok(rstd::move(parsed).unwrap());
}

auto lito::parse::json::fetch_url(const rstd::json::Value& value, const NodePath& path)
    -> ParseResult<FetchUrl> {
    auto parsed = FetchUrl::parse(rstd_try(string(value, path)));
    if (parsed.is_err()) {
        return invalid_value<FetchUrl>(ref<NodePath>::from_raw_parts(&path),
                                       rstd::move(parsed).unwrap_err());
    }
    return Ok(rstd::move(parsed).unwrap());
}

auto lito::parse::json::https_url(const rstd::json::Value& value, const NodePath& path)
    -> ParseResult<HttpsUrl> {
    auto parsed = HttpsUrl::parse(rstd_try(string(value, path)));
    if (parsed.is_err()) {
        return invalid_value<HttpsUrl>(ref<NodePath>::from_raw_parts(&path),
                                       rstd::move(parsed).unwrap_err());
    }
    return Ok(rstd::move(parsed).unwrap());
}

auto lito::parse::json::normal_relative_path(const rstd::json::Value& value, const NodePath& path)
    -> ParseResult<NormalRelativePath> {
    auto parsed = NormalRelativePath::parse(rstd_try(string(value, path)));
    if (parsed.is_err()) {
        return invalid_value<NormalRelativePath>(ref<NodePath>::from_raw_parts(&path),
                                                 rstd::move(parsed).unwrap_err());
    }
    return Ok(rstd::move(parsed).unwrap());
}

auto lito::parse::json::path_component(const rstd::json::Value& value, const NodePath& path)
    -> ParseResult<PathComponent> {
    auto parsed = PathComponent::parse(rstd_try(string(value, path)));
    if (parsed.is_err()) {
        return invalid_value<PathComponent>(ref<NodePath>::from_raw_parts(&path),
                                            rstd::move(parsed).unwrap_err());
    }
    return Ok(rstd::move(parsed).unwrap());
}

auto lito::parse::toml::kind(const rstd::toml::Value& value) noexcept -> ValueKind {
    return toml_kind(value);
}

auto lito::parse::toml::table(const rstd::toml::Value& value, const NodePath& path)
    -> ParseResult<ref<rstd::toml::Table>> {
    auto parsed = value.as_table();
    if (parsed.is_none()) return Err(Error::WrongType(path.clone(), ValueKind::Table, kind(value)));
    return Ok(*parsed);
}

auto lito::parse::toml::array(const rstd::toml::Value& value, const NodePath& path)
    -> ParseResult<ref<rstd::toml::Array>> {
    auto parsed = value.as_array();
    if (parsed.is_none()) return Err(Error::WrongType(path.clone(), ValueKind::Array, kind(value)));
    return Ok(*parsed);
}

auto lito::parse::toml::string(const rstd::toml::Value& value, const NodePath& path)
    -> ParseResult<ref<str>> {
    auto parsed = value.as_str();
    if (parsed.is_none())
        return Err(Error::WrongType(path.clone(), ValueKind::String, kind(value)));
    return Ok(*parsed);
}

auto lito::parse::toml::non_empty_string(const rstd::toml::Value& value, const NodePath& path)
    -> ParseResult<String> {
    auto parsed = rstd_try(string(value, path));
    if (parsed.is_empty()) return Err(Error::EmptyValue(path.clone()));
    return Ok(String::make(parsed));
}

auto lito::parse::toml::integer(const rstd::toml::Value& value, const NodePath& path)
    -> ParseResult<i64> {
    auto parsed = value.as_integer();
    if (parsed.is_none())
        return Err(Error::WrongType(path.clone(), ValueKind::Integer, kind(value)));
    return Ok(*parsed);
}

auto lito::parse::toml::u64_value(const rstd::toml::Value& value, const NodePath& path)
    -> ParseResult<u64> {
    auto parsed = rstd_try(integer(value, path));
    if (parsed.to_primitive() < 0) {
        return Err(Error::WrongType(path.clone(), ValueKind::UnsignedInteger, ValueKind::Integer));
    }
    return Ok(u64(parsed.to_primitive()));
}

auto lito::parse::toml::boolean(const rstd::toml::Value& value, const NodePath& path)
    -> ParseResult<bool> {
    auto parsed = value.as_bool();
    if (parsed.is_none())
        return Err(Error::WrongType(path.clone(), ValueKind::Boolean, kind(value)));
    return Ok(*parsed);
}

auto lito::parse::toml::required_member(const rstd::toml::Value& value,
                                        ref<str>                 key,
                                        const NodePath&          path)
    -> ParseResult<ref<rstd::toml::Value>> {
    rstd_try(table(value, path));
    auto member = value.get(key);
    if (member.is_none()) return Err(Error::MissingField(path.clone(), String::make(key)));
    return Ok(*member);
}

auto lito::parse::toml::required_string(const rstd::toml::Value& value,
                                        ref<str>                 key,
                                        const NodePath&          path) -> ParseResult<String> {
    auto member = rstd_try(required_member(value, key, path));
    return Ok(String::make(rstd_try(string(*member, path.field(key)))));
}

auto lito::parse::toml::required_non_empty_string(const rstd::toml::Value& value,
                                                  ref<str>                 key,
                                                  const NodePath& path) -> ParseResult<String> {
    auto member = rstd_try(required_member(value, key, path));
    return non_empty_string(*member, path.field(key));
}

auto lito::parse::toml::required_sha256(const rstd::toml::Value& value,
                                        ref<str>                 key,
                                        const NodePath&          path,
                                        Sha256TextMode           mode)
    -> ParseResult<lito::crypto::Sha256Digest> {
    auto member = rstd_try(required_member(value, key, path));
    return sha256(*member, path.field(key), mode);
}

auto lito::parse::toml::required_fetch_url(const rstd::toml::Value& value,
                                           ref<str>                 key,
                                           const NodePath&          path) -> ParseResult<FetchUrl> {
    auto member = rstd_try(required_member(value, key, path));
    return fetch_url(*member, path.field(key));
}

auto lito::parse::toml::required_https_url(const rstd::toml::Value& value,
                                           ref<str>                 key,
                                           const NodePath&          path) -> ParseResult<HttpsUrl> {
    auto member = rstd_try(required_member(value, key, path));
    return https_url(*member, path.field(key));
}

auto lito::parse::toml::required_normal_relative_path(const rstd::toml::Value& value,
                                                      ref<str>                 key,
                                                      const NodePath&          path)
    -> ParseResult<NormalRelativePath> {
    auto member = rstd_try(required_member(value, key, path));
    return normal_relative_path(*member, path.field(key));
}

auto lito::parse::toml::required_path_component(const rstd::toml::Value& value,
                                                ref<str>                 key,
                                                const NodePath&          path)
    -> ParseResult<PathComponent> {
    auto member = rstd_try(required_member(value, key, path));
    return path_component(*member, path.field(key));
}

auto lito::parse::toml::reject_unknown(const rstd::toml::Table&   value,
                                       const NodePath&            path,
                                       initializer_list<ref<str>> allowed) -> ParseResult<empty> {
    auto keys = value.keys();
    for (auto key : keys) {
        auto known = false;
        for (const auto candidate : allowed) {
            if ((*key).as_str() == candidate) {
                known = true;
                break;
            }
        }
        if (! known) return Err(Error::UnknownField(path.clone(), (*key).clone()));
    }
    return Ok(empty {});
}

auto lito::parse::toml::reject_unknown(const rstd::toml::Table& value,
                                       const NodePath&          path,
                                       bool (*allowed)(ref<str>)) -> ParseResult<empty> {
    auto keys = value.keys();
    for (auto key : keys) {
        if (! allowed((*key).as_str())) {
            return Err(Error::UnknownField(path.clone(), (*key).clone()));
        }
    }
    return Ok(empty {});
}

auto lito::parse::toml::reject_unknown(const rstd::toml::Value&   value,
                                       const NodePath&            path,
                                       initializer_list<ref<str>> allowed) -> ParseResult<empty> {
    return reject_unknown(*rstd_try(table(value, path)), path, allowed);
}

auto lito::parse::toml::reject_unknown(const rstd::toml::Value& value,
                                       const NodePath&          path,
                                       bool (*allowed)(ref<str>)) -> ParseResult<empty> {
    return reject_unknown(*rstd_try(table(value, path)), path, allowed);
}

auto lito::parse::toml::sha256(const rstd::toml::Value& value,
                               const NodePath&          path,
                               Sha256TextMode mode) -> ParseResult<lito::crypto::Sha256Digest> {
    auto parsed = parse_sha256(rstd_try(string(value, path)), mode);
    if (parsed.is_err()) {
        return invalid_value<lito::crypto::Sha256Digest>(ref<NodePath>::from_raw_parts(&path),
                                                         rstd::move(parsed).unwrap_err());
    }
    return Ok(rstd::move(parsed).unwrap());
}

auto lito::parse::toml::fetch_url(const rstd::toml::Value& value, const NodePath& path)
    -> ParseResult<FetchUrl> {
    auto parsed = FetchUrl::parse(rstd_try(string(value, path)));
    if (parsed.is_err()) {
        return invalid_value<FetchUrl>(ref<NodePath>::from_raw_parts(&path),
                                       rstd::move(parsed).unwrap_err());
    }
    return Ok(rstd::move(parsed).unwrap());
}

auto lito::parse::toml::https_url(const rstd::toml::Value& value, const NodePath& path)
    -> ParseResult<HttpsUrl> {
    auto parsed = HttpsUrl::parse(rstd_try(string(value, path)));
    if (parsed.is_err()) {
        return invalid_value<HttpsUrl>(ref<NodePath>::from_raw_parts(&path),
                                       rstd::move(parsed).unwrap_err());
    }
    return Ok(rstd::move(parsed).unwrap());
}

auto lito::parse::toml::normal_relative_path(const rstd::toml::Value& value, const NodePath& path)
    -> ParseResult<NormalRelativePath> {
    auto parsed = NormalRelativePath::parse(rstd_try(string(value, path)));
    if (parsed.is_err()) {
        return invalid_value<NormalRelativePath>(ref<NodePath>::from_raw_parts(&path),
                                                 rstd::move(parsed).unwrap_err());
    }
    return Ok(rstd::move(parsed).unwrap());
}

auto lito::parse::toml::path_component(const rstd::toml::Value& value, const NodePath& path)
    -> ParseResult<PathComponent> {
    auto parsed = PathComponent::parse(rstd_try(string(value, path)));
    if (parsed.is_err()) {
        return invalid_value<PathComponent>(ref<NodePath>::from_raw_parts(&path),
                                            rstd::move(parsed).unwrap_err());
    }
    return Ok(rstd::move(parsed).unwrap());
}

namespace rstd
{

auto Impl<fmt::Display, lito::parse::NodePath>::fmt(fmt::Formatter& formatter) const -> bool {
    const auto& path = this->self();
    if (! formatter.write_str(path.root_name())) return false;
    for (const auto& segment : path.segments()) {
        if (segment.kind() == lito::parse::NodeSegmentKind::Field) {
            if (! formatter.write_str("."_str) || ! formatter.write_str(segment.field())) {
                return false;
            }
        } else if (! formatter.write_fmt(fmt::Arguments::make("[{}]", segment.index()))) {
            return false;
        }
    }
    return true;
}

auto Impl<fmt::Debug, lito::parse::NodePath>::fmt(fmt::Formatter& formatter) const -> bool {
    return as<fmt::Display>(this->self()).fmt(formatter);
}

auto Impl<fmt::Display, lito::parse::Error>::fmt(fmt::Formatter& formatter) const -> bool {
    const auto& error = this->self();
    if (error.is_MissingField()) {
        const auto& value = error.as_MissingField();
        return formatter.write_fmt(
            fmt::Arguments::make("{} is missing '{}'", value.node, value.field));
    }
    if (error.is_UnknownField()) {
        const auto& value = error.as_UnknownField();
        return formatter.write_fmt(
            fmt::Arguments::make("{} contains unknown field '{}'", value.node, value.field));
    }
    if (error.is_WrongType()) {
        const auto& value = error.as_WrongType();
        return formatter.write_fmt(
            fmt::Arguments::make("{} must be {}; found {}",
                                 value.node,
                                 lito::parse::value_kind_name(value.expected),
                                 lito::parse::value_kind_name(value.actual)));
    }
    if (error.is_EmptyValue()) {
        return formatter.write_fmt(
            fmt::Arguments::make("{} must not be empty", error.as_EmptyValue().node));
    }
    const auto& value = error.as_InvalidValue();
    return formatter.write_fmt(
        fmt::Arguments::make("{} is invalid: {}", value.node, value.requirement));
}

auto Impl<fmt::Debug, lito::parse::Error>::fmt(fmt::Formatter& formatter) const -> bool {
    return as<fmt::Display>(this->self()).fmt(formatter);
}

auto Impl<fmt::Display, lito::parse::Sha256Error>::fmt(fmt::Formatter& formatter) const -> bool {
    const auto& error = this->self();
    if (error.is_Digest()) {
        return as<fmt::Display>(error.as_Digest().source).fmt(formatter);
    }
    return formatter.write_fmt(fmt::Arguments::make(
        "SHA-256 must use lowercase hexadecimal at byte {}", error.as_NonCanonicalCase().index));
}

auto Impl<fmt::Debug, lito::parse::Sha256Error>::fmt(fmt::Formatter& formatter) const -> bool {
    return as<fmt::Display>(this->self()).fmt(formatter);
}

auto Impl<error::Error, lito::parse::Sha256Error>::source() const noexcept
    -> Option<error::ErrorRef> {
    const auto& error = this->self();
    if (! error.is_Digest()) return None();
    return Some(dyn<error::Error>::from_ref(error.as_Digest().source));
}

auto Impl<fmt::Display, lito::parse::Url>::fmt(fmt::Formatter& formatter) const -> bool {
    return formatter.write_str(this->self().as_str());
}

auto Impl<fmt::Debug, lito::parse::Url>::fmt(fmt::Formatter& formatter) const -> bool {
    auto value = this->self().as_str();
    return as<fmt::Debug>(value).fmt(formatter);
}

auto Impl<fmt::Display, lito::parse::FetchUrl>::fmt(fmt::Formatter& formatter) const -> bool {
    return formatter.write_str(this->self().as_str());
}

auto Impl<fmt::Debug, lito::parse::FetchUrl>::fmt(fmt::Formatter& formatter) const -> bool {
    auto value = this->self().as_str();
    return as<fmt::Debug>(value).fmt(formatter);
}

auto Impl<fmt::Display, lito::parse::HttpsUrl>::fmt(fmt::Formatter& formatter) const -> bool {
    return formatter.write_str(this->self().as_str());
}

auto Impl<fmt::Debug, lito::parse::HttpsUrl>::fmt(fmt::Formatter& formatter) const -> bool {
    auto value = this->self().as_str();
    return as<fmt::Debug>(value).fmt(formatter);
}

auto Impl<fmt::Display, lito::parse::UrlError>::fmt(fmt::Formatter& formatter) const -> bool {
    const auto& error = this->self();
    if (error.is_Empty()) return formatter.write_str("URL must not be empty"_str);
    if (error.is_MissingScheme()) {
        return formatter.write_str("URL must contain an absolute scheme and authority"_str);
    }
    if (error.is_InvalidScheme()) {
        return formatter.write_fmt(fmt::Arguments::make(
            "URL scheme contains an invalid character at byte {}", error.as_InvalidScheme().index));
    }
    if (error.is_MissingAuthority()) {
        return formatter.write_str("URL must contain a non-empty authority"_str);
    }
    if (error.is_InvalidCharacter()) {
        return formatter.write_fmt(fmt::Arguments::make(
            "URL contains an invalid character at byte {}", error.as_InvalidCharacter().index));
    }
    if (error.is_InvalidPercentEncoding()) {
        return formatter.write_fmt(
            fmt::Arguments::make("URL contains invalid percent encoding at byte {}",
                                 error.as_InvalidPercentEncoding().index));
    }
    if (error.is_FragmentNotAllowed()) {
        return formatter.write_str("fetch URL must not contain a fragment"_str);
    }
    if (error.is_UnsupportedFetchScheme()) {
        return formatter.write_fmt(fmt::Arguments::make("fetch URL scheme '{}' is not supported",
                                                        error.as_UnsupportedFetchScheme().scheme));
    }
    return formatter.write_fmt(fmt::Arguments::make("HTTPS URL required; found scheme '{}'",
                                                    error.as_HttpsRequired().scheme));
}

auto Impl<fmt::Debug, lito::parse::UrlError>::fmt(fmt::Formatter& formatter) const -> bool {
    return as<fmt::Display>(this->self()).fmt(formatter);
}

auto Impl<fmt::Display, lito::parse::PathValueError>::fmt(fmt::Formatter& formatter) const -> bool {
    const auto& error = this->self();
    if (error.is_Empty()) return formatter.write_str("path must not be empty"_str);
    if (error.is_Absolute()) return formatter.write_str("path must be relative"_str);
    if (error.is_NonNormalComponent()) {
        return formatter.write_str("path must contain only normal components"_str);
    }
    return formatter.write_str("path must contain exactly one component"_str);
}

auto Impl<fmt::Debug, lito::parse::PathValueError>::fmt(fmt::Formatter& formatter) const -> bool {
    return as<fmt::Display>(this->self()).fmt(formatter);
}

auto Impl<fmt::Display, lito::parse::DecimalError>::fmt(fmt::Formatter& formatter) const -> bool {
    const auto& error = this->self();
    if (error.is_Empty()) return formatter.write_str("decimal value must not be empty"_str);
    if (error.is_LeadingZero()) {
        return formatter.write_str("decimal value must not contain a leading zero"_str);
    }
    if (error.is_Character()) {
        return formatter.write_fmt(fmt::Arguments::make(
            "decimal value contains a non-digit at byte {}", error.as_Character().index));
    }
    return formatter.write_str("decimal value exceeds u64 range"_str);
}

auto Impl<fmt::Debug, lito::parse::DecimalError>::fmt(fmt::Formatter& formatter) const -> bool {
    return as<fmt::Display>(this->self()).fmt(formatter);
}

auto Impl<convert::TryFrom<ref<str>>, lito::parse::Url>::try_from(ref<str> value)
    -> Result<lito::parse::Url, Error> {
    return lito::parse::Url::parse(value);
}

auto Impl<convert::TryFrom<ref<str>>, lito::parse::FetchUrl>::try_from(ref<str> value)
    -> Result<lito::parse::FetchUrl, Error> {
    return lito::parse::FetchUrl::parse(value);
}

auto Impl<convert::TryFrom<ref<str>>, lito::parse::HttpsUrl>::try_from(ref<str> value)
    -> Result<lito::parse::HttpsUrl, Error> {
    return lito::parse::HttpsUrl::parse(value);
}

} // namespace rstd
