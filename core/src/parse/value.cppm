module;
#include <rstd/enum.hpp>

export module lito.core:parse.value;

import rstd;
import lito.crypto;

using namespace rstd::prelude;

export namespace lito::parse
{

enum class Sha256TextMode
{
    Flexible,
    Canonical,
};

class Sha256Error {
    RSTD_ENUM(Sha256Error,
              (Digest, (lito::crypto::Sha256DigestParseError source;)),
              (NonCanonicalCase, (usize index;)))
};

auto parse_sha256(ref<str> value, Sha256TextMode mode)
    -> Result<lito::crypto::Sha256Digest, Sha256Error>;

class UrlError {
    RSTD_ENUM(UrlError,
              (Empty),
              (MissingScheme),
              (InvalidScheme, (usize index;)),
              (MissingAuthority),
              (InvalidCharacter, (usize index;)),
              (InvalidPercentEncoding, (usize index;)),
              (FragmentNotAllowed),
              (UnsupportedFetchScheme, (String scheme;)),
              (HttpsRequired, (String scheme;)))
};

class Url : public DefaultInClass<Url, Clone> {
    String value_;
    usize  scheme_end_ {};
    usize  authority_begin_ {};
    usize  authority_end_ {};
    usize  path_begin_ {};
    usize  query_begin_ {};
    usize  fragment_begin_ {};

    Url(String value,
        usize  scheme_end,
        usize  authority_begin,
        usize  authority_end,
        usize  path_begin,
        usize  query_begin,
        usize  fragment_begin)
        : value_(rstd::move(value)),
          scheme_end_(scheme_end),
          authority_begin_(authority_begin),
          authority_end_(authority_end),
          path_begin_(path_begin),
          query_begin_(query_begin),
          fragment_begin_(fragment_begin) {}

public:
    Url(const Url&)                = delete;
    Url& operator=(const Url&)     = delete;
    Url(Url&&) noexcept            = default;
    Url& operator=(Url&&) noexcept = default;

    static auto parse(ref<str> value) -> Result<Url, UrlError>;

    auto as_str() const noexcept -> ref<str> { return value_.as_str(); }
    auto scheme() const noexcept -> ref<str>;
    auto authority() const noexcept -> ref<str>;
    auto path() const noexcept -> ref<str>;
    auto fragment() const noexcept -> Option<ref<str>>;
    auto clone() const -> Url;

    friend auto operator==(const Url& left, const Url& right) noexcept -> bool {
        return left.value_ == right.value_;
    }
    friend auto operator<=>(const Url& left, const Url& right) noexcept {
        return left.value_ <=> right.value_;
    }
};

class FetchUrl : public DefaultInClass<FetchUrl, Clone> {
    Url value_;

    explicit FetchUrl(Url value): value_(rstd::move(value)) {}

public:
    FetchUrl(const FetchUrl&)                = delete;
    FetchUrl& operator=(const FetchUrl&)     = delete;
    FetchUrl(FetchUrl&&) noexcept            = default;
    FetchUrl& operator=(FetchUrl&&) noexcept = default;

    static auto parse(ref<str> value) -> Result<FetchUrl, UrlError>;
    static auto try_from(Url value) -> Result<FetchUrl, UrlError>;

    auto url() const noexcept -> ref<Url> { return ref<Url>::from_raw_parts(&value_); }
    auto as_str() const noexcept -> ref<str> { return value_.as_str(); }
    auto clone() const -> FetchUrl { return FetchUrl(value_.clone()); }

    friend auto operator==(const FetchUrl& left, const FetchUrl& right) noexcept -> bool {
        return left.value_ == right.value_;
    }
    friend auto operator<=>(const FetchUrl& left, const FetchUrl& right) noexcept {
        return left.value_ <=> right.value_;
    }
};

class HttpsUrl : public DefaultInClass<HttpsUrl, Clone> {
    FetchUrl value_;

    explicit HttpsUrl(FetchUrl value): value_(rstd::move(value)) {}

public:
    HttpsUrl(const HttpsUrl&)                = delete;
    HttpsUrl& operator=(const HttpsUrl&)     = delete;
    HttpsUrl(HttpsUrl&&) noexcept            = default;
    HttpsUrl& operator=(HttpsUrl&&) noexcept = default;

    static auto parse(ref<str> value) -> Result<HttpsUrl, UrlError>;
    static auto try_from(FetchUrl value) -> Result<HttpsUrl, UrlError>;

    auto url() const noexcept -> ref<Url> { return value_.url(); }
    auto fetch_url() const noexcept -> ref<FetchUrl> {
        return ref<FetchUrl>::from_raw_parts(&value_);
    }
    auto as_str() const noexcept -> ref<str> { return value_.as_str(); }
    auto clone() const -> HttpsUrl { return HttpsUrl(value_.clone()); }

    friend auto operator==(const HttpsUrl& left, const HttpsUrl& right) noexcept -> bool {
        return left.value_ == right.value_;
    }
    friend auto operator<=>(const HttpsUrl& left, const HttpsUrl& right) noexcept {
        return left.value_ <=> right.value_;
    }
};

class PathValueError {
    RSTD_ENUM(PathValueError, (Empty), (Absolute), (NonNormalComponent), (MultipleComponents))
};

class NormalRelativePath : public DefaultInClass<NormalRelativePath, Clone> {
    rstd::path::PathBuf value_;

    explicit NormalRelativePath(rstd::path::PathBuf value): value_(rstd::move(value)) {}

public:
    NormalRelativePath(const NormalRelativePath&)                = delete;
    NormalRelativePath& operator=(const NormalRelativePath&)     = delete;
    NormalRelativePath(NormalRelativePath&&) noexcept            = default;
    NormalRelativePath& operator=(NormalRelativePath&&) noexcept = default;

    static auto parse(ref<str> value) -> Result<NormalRelativePath, PathValueError>;
    static auto parse(rstd::path::PathBuf value) -> Result<NormalRelativePath, PathValueError>;

    auto as_path() const noexcept -> ref<rstd::path::Path> { return value_.as_path(); }
    auto into_path() && noexcept -> rstd::path::PathBuf { return rstd::move(value_); }
    auto clone() const -> NormalRelativePath { return NormalRelativePath(value_.clone()); }
};

class PathComponent : public DefaultInClass<PathComponent, Clone> {
    rstd::path::PathBuf value_;

    explicit PathComponent(rstd::path::PathBuf value): value_(rstd::move(value)) {}

public:
    PathComponent(const PathComponent&)                = delete;
    PathComponent& operator=(const PathComponent&)     = delete;
    PathComponent(PathComponent&&) noexcept            = default;
    PathComponent& operator=(PathComponent&&) noexcept = default;

    static auto parse(ref<str> value) -> Result<PathComponent, PathValueError>;
    static auto parse(rstd::path::PathBuf value) -> Result<PathComponent, PathValueError>;

    auto as_path() const noexcept -> ref<rstd::path::Path> { return value_.as_path(); }
    auto as_str() const noexcept -> Option<ref<str>> { return value_.as_path().to_str(); }
    auto into_path() && noexcept -> rstd::path::PathBuf { return rstd::move(value_); }
    auto clone() const -> PathComponent { return PathComponent(value_.clone()); }
};

class DecimalError {
    RSTD_ENUM(DecimalError, (Empty), (LeadingZero), (Character, (usize index;)), (Overflow))
};

auto parse_canonical_u64_decimal(ref<str> value) -> Result<u64, DecimalError>;

} // namespace lito::parse

export namespace rstd
{

template<>
struct Impl<convert::TryFrom<ref<str>>, lito::parse::Url> {
    using Error = lito::parse::UrlError;
    static auto try_from(ref<str> value) -> Result<lito::parse::Url, Error>;
};

template<>
struct Impl<convert::TryFrom<ref<str>>, lito::parse::FetchUrl> {
    using Error = lito::parse::UrlError;
    static auto try_from(ref<str> value) -> Result<lito::parse::FetchUrl, Error>;
};

template<>
struct Impl<convert::TryFrom<ref<str>>, lito::parse::HttpsUrl> {
    using Error = lito::parse::UrlError;
    static auto try_from(ref<str> value) -> Result<lito::parse::HttpsUrl, Error>;
};

template<>
struct Impl<fmt::Display, lito::parse::Sha256Error> : ImplBase<lito::parse::Sha256Error> {
    auto fmt(fmt::Formatter& formatter) const -> bool;
};

template<>
struct Impl<fmt::Debug, lito::parse::Sha256Error> : ImplBase<lito::parse::Sha256Error> {
    auto fmt(fmt::Formatter& formatter) const -> bool;
};

template<>
struct Impl<error::Error, lito::parse::Sha256Error> : ImplBase<lito::parse::Sha256Error> {
    auto source() const noexcept -> Option<error::ErrorRef>;
};

template<>
struct Impl<fmt::Display, lito::parse::Url> : ImplBase<lito::parse::Url> {
    auto fmt(fmt::Formatter& formatter) const -> bool;
};

template<>
struct Impl<fmt::Debug, lito::parse::Url> : ImplBase<lito::parse::Url> {
    auto fmt(fmt::Formatter& formatter) const -> bool;
};

template<>
struct Impl<fmt::Display, lito::parse::FetchUrl> : ImplBase<lito::parse::FetchUrl> {
    auto fmt(fmt::Formatter& formatter) const -> bool;
};

template<>
struct Impl<fmt::Debug, lito::parse::FetchUrl> : ImplBase<lito::parse::FetchUrl> {
    auto fmt(fmt::Formatter& formatter) const -> bool;
};

template<>
struct Impl<fmt::Display, lito::parse::HttpsUrl> : ImplBase<lito::parse::HttpsUrl> {
    auto fmt(fmt::Formatter& formatter) const -> bool;
};

template<>
struct Impl<fmt::Debug, lito::parse::HttpsUrl> : ImplBase<lito::parse::HttpsUrl> {
    auto fmt(fmt::Formatter& formatter) const -> bool;
};

template<>
struct Impl<fmt::Display, lito::parse::UrlError> : ImplBase<lito::parse::UrlError> {
    auto fmt(fmt::Formatter& formatter) const -> bool;
};

template<>
struct Impl<fmt::Debug, lito::parse::UrlError> : ImplBase<lito::parse::UrlError> {
    auto fmt(fmt::Formatter& formatter) const -> bool;
};

template<>
struct Impl<error::Error, lito::parse::UrlError>
    : DefaultInImpl<error::Error, lito::parse::UrlError> {};

template<>
struct Impl<fmt::Display, lito::parse::PathValueError> : ImplBase<lito::parse::PathValueError> {
    auto fmt(fmt::Formatter& formatter) const -> bool;
};

template<>
struct Impl<fmt::Debug, lito::parse::PathValueError> : ImplBase<lito::parse::PathValueError> {
    auto fmt(fmt::Formatter& formatter) const -> bool;
};

template<>
struct Impl<error::Error, lito::parse::PathValueError>
    : DefaultInImpl<error::Error, lito::parse::PathValueError> {};

template<>
struct Impl<fmt::Display, lito::parse::DecimalError> : ImplBase<lito::parse::DecimalError> {
    auto fmt(fmt::Formatter& formatter) const -> bool;
};

template<>
struct Impl<fmt::Debug, lito::parse::DecimalError> : ImplBase<lito::parse::DecimalError> {
    auto fmt(fmt::Formatter& formatter) const -> bool;
};

template<>
struct Impl<error::Error, lito::parse::DecimalError>
    : DefaultInImpl<error::Error, lito::parse::DecimalError> {};

template<>
struct Impl<hash::Hash, lito::parse::Url> : ImplBase<lito::parse::Url> {
    template<typename H>
        requires Impled<H, hash::Hasher>
    void hash(H& state) const noexcept {
        hash::hash_into(this->self().as_str(), state);
    }
};

template<>
struct Impl<hash::Hash, lito::parse::FetchUrl> : ImplBase<lito::parse::FetchUrl> {
    template<typename H>
        requires Impled<H, hash::Hasher>
    void hash(H& state) const noexcept {
        hash::hash_into(this->self().as_str(), state);
    }
};

template<>
struct Impl<hash::Hash, lito::parse::HttpsUrl> : ImplBase<lito::parse::HttpsUrl> {
    template<typename H>
        requires Impled<H, hash::Hasher>
    void hash(H& state) const noexcept {
        hash::hash_into(this->self().as_str(), state);
    }
};

} // namespace rstd
