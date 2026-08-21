module;
#include <initializer_list>

export module lito.core:parse.json;

import rstd;
import rstd.json;
import :parse.error;
import :parse.value;

using namespace rstd::prelude;

export namespace lito::parse::json
{

auto kind(const rstd::json::Value& value) noexcept -> ValueKind;
auto object(const rstd::json::Value& value, const NodePath& path)
    -> ParseResult<ref<rstd::json::Map>>;
auto array(const rstd::json::Value& value, const NodePath& path)
    -> ParseResult<ref<rstd::json::Array>>;
auto string(const rstd::json::Value& value, const NodePath& path) -> ParseResult<ref<str>>;
auto non_empty_string(const rstd::json::Value& value, const NodePath& path) -> ParseResult<String>;
auto u64_value(const rstd::json::Value& value, const NodePath& path) -> ParseResult<u64>;
auto boolean(const rstd::json::Value& value, const NodePath& path) -> ParseResult<bool>;

auto required_member(const rstd::json::Value& value, ref<str> key, const NodePath& path)
    -> ParseResult<ref<rstd::json::Value>>;
auto required_string(const rstd::json::Value& value, ref<str> key, const NodePath& path)
    -> ParseResult<String>;
auto required_non_empty_string(const rstd::json::Value& value, ref<str> key, const NodePath& path)
    -> ParseResult<String>;
auto required_u64(const rstd::json::Value& value, ref<str> key, const NodePath& path)
    -> ParseResult<u64>;
auto required_array(const rstd::json::Value& value, ref<str> key, const NodePath& path)
    -> ParseResult<ref<rstd::json::Array>>;
auto required_sha256(const rstd::json::Value& value,
                     ref<str>                 key,
                     const NodePath&          path,
                     Sha256TextMode           mode) -> ParseResult<rstd::crypto::Sha256Digest>;
auto required_fetch_url(const rstd::json::Value& value, ref<str> key, const NodePath& path)
    -> ParseResult<FetchUrl>;
auto required_https_url(const rstd::json::Value& value, ref<str> key, const NodePath& path)
    -> ParseResult<HttpsUrl>;
auto required_normal_relative_path(const rstd::json::Value& value,
                                   ref<str>                 key,
                                   const NodePath& path) -> ParseResult<NormalRelativePath>;
auto required_path_component(const rstd::json::Value& value, ref<str> key, const NodePath& path)
    -> ParseResult<PathComponent>;
auto reject_unknown(const rstd::json::Map&          value,
                    const NodePath&                 path,
                    std::initializer_list<ref<str>> allowed) -> ParseResult<empty>;
auto reject_unknown(const rstd::json::Map& value, const NodePath& path, bool (*allowed)(ref<str>))
    -> ParseResult<empty>;
auto reject_unknown(const rstd::json::Value&        value,
                    const NodePath&                 path,
                    std::initializer_list<ref<str>> allowed) -> ParseResult<empty>;
auto reject_unknown(const rstd::json::Value& value, const NodePath& path, bool (*allowed)(ref<str>))
    -> ParseResult<empty>;

auto sha256(const rstd::json::Value& value, const NodePath& path, Sha256TextMode mode)
    -> ParseResult<rstd::crypto::Sha256Digest>;
auto fetch_url(const rstd::json::Value& value, const NodePath& path) -> ParseResult<FetchUrl>;
auto https_url(const rstd::json::Value& value, const NodePath& path) -> ParseResult<HttpsUrl>;
auto normal_relative_path(const rstd::json::Value& value, const NodePath& path)
    -> ParseResult<NormalRelativePath>;
auto path_component(const rstd::json::Value& value, const NodePath& path)
    -> ParseResult<PathComponent>;

} // namespace lito::parse::json
