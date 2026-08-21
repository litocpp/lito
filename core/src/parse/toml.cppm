export module lito.core:parse.toml;

import rstd;
import rstd.toml;
import :parse.error;
import :parse.value;

using namespace rstd::prelude;

export namespace lito::parse::toml
{

auto kind(const rstd::toml::Value& value) noexcept -> ValueKind;
auto table(const rstd::toml::Value& value, const NodePath& path)
    -> ParseResult<ref<rstd::toml::Table>>;
auto array(const rstd::toml::Value& value, const NodePath& path)
    -> ParseResult<ref<rstd::toml::Array>>;
auto string(const rstd::toml::Value& value, const NodePath& path) -> ParseResult<ref<str>>;
auto non_empty_string(const rstd::toml::Value& value, const NodePath& path) -> ParseResult<String>;
auto integer(const rstd::toml::Value& value, const NodePath& path) -> ParseResult<i64>;
auto u64_value(const rstd::toml::Value& value, const NodePath& path) -> ParseResult<u64>;
auto boolean(const rstd::toml::Value& value, const NodePath& path) -> ParseResult<bool>;

auto required_member(const rstd::toml::Value& value, ref<str> key, const NodePath& path)
    -> ParseResult<ref<rstd::toml::Value>>;
auto required_string(const rstd::toml::Value& value, ref<str> key, const NodePath& path)
    -> ParseResult<String>;
auto required_non_empty_string(const rstd::toml::Value& value, ref<str> key, const NodePath& path)
    -> ParseResult<String>;
auto required_sha256(const rstd::toml::Value& value,
                     ref<str>                 key,
                     const NodePath&          path,
                     Sha256TextMode           mode) -> ParseResult<rstd::crypto::Sha256Digest>;
auto required_fetch_url(const rstd::toml::Value& value, ref<str> key, const NodePath& path)
    -> ParseResult<FetchUrl>;
auto required_https_url(const rstd::toml::Value& value, ref<str> key, const NodePath& path)
    -> ParseResult<HttpsUrl>;
auto required_normal_relative_path(const rstd::toml::Value& value,
                                   ref<str>                 key,
                                   const NodePath& path) -> ParseResult<NormalRelativePath>;
auto required_path_component(const rstd::toml::Value& value, ref<str> key, const NodePath& path)
    -> ParseResult<PathComponent>;
auto reject_unknown(const rstd::toml::Table&   value,
                    const NodePath&            path,
                    initializer_list<ref<str>> allowed) -> ParseResult<empty>;
auto reject_unknown(const rstd::toml::Table& value, const NodePath& path, bool (*allowed)(ref<str>))
    -> ParseResult<empty>;
auto reject_unknown(const rstd::toml::Value&   value,
                    const NodePath&            path,
                    initializer_list<ref<str>> allowed) -> ParseResult<empty>;
auto reject_unknown(const rstd::toml::Value& value, const NodePath& path, bool (*allowed)(ref<str>))
    -> ParseResult<empty>;

auto sha256(const rstd::toml::Value& value, const NodePath& path, Sha256TextMode mode)
    -> ParseResult<rstd::crypto::Sha256Digest>;
auto fetch_url(const rstd::toml::Value& value, const NodePath& path) -> ParseResult<FetchUrl>;
auto https_url(const rstd::toml::Value& value, const NodePath& path) -> ParseResult<HttpsUrl>;
auto normal_relative_path(const rstd::toml::Value& value, const NodePath& path)
    -> ParseResult<NormalRelativePath>;
auto path_component(const rstd::toml::Value& value, const NodePath& path)
    -> ParseResult<PathComponent>;

} // namespace lito::parse::toml
