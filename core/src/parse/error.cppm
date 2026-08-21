module;
#include <rstd/enum.hpp>

export module lito.core:parse.error;

import rstd;

using namespace rstd::prelude;

export namespace lito::parse
{

enum class NodeSegmentKind
{
    Field,
    Index,
};

class NodeSegment {
    NodeSegmentKind kind_ { NodeSegmentKind::Field };
    String          field_;
    usize           index_ {};

public:
    static auto field(ref<str> value) -> NodeSegment;
    static auto index(usize value) -> NodeSegment;

    auto kind() const noexcept -> NodeSegmentKind { return kind_; }
    auto field() const noexcept -> ref<str> { return field_.as_str(); }
    auto index() const noexcept -> usize { return index_; }
    auto clone() const -> NodeSegment;
};

class NodePath {
    String           root_;
    Vec<NodeSegment> segments_;

public:
    static auto root(ref<str> value) -> NodePath;

    auto field(ref<str> value) const -> NodePath;
    auto index(usize value) const -> NodePath;
    auto root_name() const noexcept -> ref<str> { return root_.as_str(); }
    auto segments() const noexcept -> slice<NodeSegment> { return segments_.as_slice(); }
    auto clone() const -> NodePath;
};

enum class ValueKind
{
    Null,
    Boolean,
    Integer,
    UnsignedInteger,
    Number,
    String,
    Array,
    Object,
    Table,
    DateTime,
    Date,
    Time,
};

class Error {
    RSTD_ENUM(Error,
              (MissingField, (NodePath node; String field;)),
              (UnknownField, (NodePath node; String field;)),
              (WrongType, (NodePath node; ValueKind expected; ValueKind actual;)),
              (EmptyValue, (NodePath node;)),
              (InvalidValue, (NodePath node; String requirement;)))
};

template<typename T>
using ParseResult = Result<T, Error>;

auto value_kind_name(ValueKind kind) noexcept -> ref<str>;

} // namespace lito::parse

export namespace rstd
{

template<>
struct Impl<fmt::Display, lito::parse::NodePath> : ImplBase<lito::parse::NodePath> {
    auto fmt(fmt::Formatter& formatter) const -> bool;
};

template<>
struct Impl<fmt::Debug, lito::parse::NodePath> : ImplBase<lito::parse::NodePath> {
    auto fmt(fmt::Formatter& formatter) const -> bool;
};

template<>
struct Impl<fmt::Display, lito::parse::Error> : ImplBase<lito::parse::Error> {
    auto fmt(fmt::Formatter& formatter) const -> bool;
};

template<>
struct Impl<fmt::Debug, lito::parse::Error> : ImplBase<lito::parse::Error> {
    auto fmt(fmt::Formatter& formatter) const -> bool;
};

template<>
struct Impl<error::Error, lito::parse::Error> : DefaultInImpl<error::Error, lito::parse::Error> {};

} // namespace rstd
