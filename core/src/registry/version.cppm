export module lito.core:registry.version;

import rstd;
import :parse.value;
import :registry.error;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::registry
{

class SemanticVersion : public DefaultInClass<SemanticVersion, Clone> {
    u64         major_ {};
    u64         minor_ {};
    u64         patch_ {};
    Vec<String> prerelease_;

    SemanticVersion(u64 major, u64 minor, u64 patch, Vec<String> prerelease)
        : major_(major), minor_(minor), patch_(patch), prerelease_(rstd::move(prerelease)) {}

public:
    SemanticVersion(const SemanticVersion&)                = delete;
    SemanticVersion& operator=(const SemanticVersion&)     = delete;
    SemanticVersion(SemanticVersion&&) noexcept            = default;
    SemanticVersion& operator=(SemanticVersion&&) noexcept = default;

    static auto parse(ref<str> value) -> RegistryValueResult<SemanticVersion>;

    auto major() const noexcept -> u64 { return major_; }
    auto minor() const noexcept -> u64 { return minor_; }
    auto patch() const noexcept -> u64 { return patch_; }
    auto prerelease() const noexcept -> slice<String> { return prerelease_.as_slice(); }
    auto is_prerelease() const noexcept -> bool { return ! prerelease_.is_empty(); }
    auto text() const -> String;
    auto clone() const -> SemanticVersion;
    auto cmp(const SemanticVersion& other) const noexcept -> strong_ordering;

    friend auto operator==(const SemanticVersion& left, const SemanticVersion& right) noexcept
        -> bool {
        return left.cmp(right) == strong_ordering::equal;
    }
    friend auto operator<=>(const SemanticVersion& left, const SemanticVersion& right) noexcept {
        return left.cmp(right);
    }
};

enum class VersionComparatorOp
{
    Less,
    LessEqual,
    Equal,
    GreaterEqual,
    Greater,
};

struct VersionComparator {
    VersionComparatorOp operation { VersionComparatorOp::Equal };
    SemanticVersion     version;

    auto clone() const -> VersionComparator {
        return VersionComparator { .operation = operation, .version = version.clone() };
    }
};

class VersionRequirement : public DefaultInClass<VersionRequirement, Clone> {
    String                 text_;
    Vec<VersionComparator> comparators_;
    bool                   admits_prerelease_ {};
    u64                    prerelease_major_ {};
    u64                    prerelease_minor_ {};
    u64                    prerelease_patch_ {};

    VersionRequirement(String                 text,
                       Vec<VersionComparator> comparators,
                       bool                   admits_prerelease,
                       u64                    prerelease_major,
                       u64                    prerelease_minor,
                       u64                    prerelease_patch)
        : text_(rstd::move(text)),
          comparators_(rstd::move(comparators)),
          admits_prerelease_(admits_prerelease),
          prerelease_major_(prerelease_major),
          prerelease_minor_(prerelease_minor),
          prerelease_patch_(prerelease_patch) {}

public:
    VersionRequirement(const VersionRequirement&)                = delete;
    VersionRequirement& operator=(const VersionRequirement&)     = delete;
    VersionRequirement(VersionRequirement&&) noexcept            = default;
    VersionRequirement& operator=(VersionRequirement&&) noexcept = default;

    static auto parse(ref<str> value) -> RegistryValueResult<VersionRequirement>;

    auto comparators() const noexcept -> slice<VersionComparator> {
        return comparators_.as_slice();
    }
    auto text() const noexcept -> ref<str> { return text_.as_str(); }
    auto matches(const SemanticVersion& version) const noexcept -> bool;
    auto clone() const -> VersionRequirement;
};

auto matches(const VersionRequirement& requirement, const SemanticVersion& version) noexcept
    -> bool;

} // namespace lito::registry

namespace lito::registry
{

auto parse_version_number(ref<str> value, ref<str> component) -> RegistryValueResult<u64> {
    auto parsed = lito::parse::parse_canonical_u64_decimal(value);
    if (parsed.is_err()) {
        return registry_value_failure<u64>(
            rstd::format("invalid semantic version {} component", component));
    }
    return Ok(rstd::move(parsed).unwrap());
}

auto prerelease_identifier_is_numeric(ref<str> value) noexcept -> bool {
    for (auto byte : value.as_bytes()) {
        const auto ascii = byte.to_primitive();
        if (ascii < '0' || ascii > '9') return false;
    }
    return ! value.is_empty();
}

auto parse_prerelease(ref<str> value) -> RegistryValueResult<Vec<String>> {
    if (value.is_empty()) {
        return registry_value_failure<Vec<String>>(
            "semantic version prerelease must not be empty"_str);
    }
    auto result    = Vec<String>::make();
    auto remaining = value;
    while (true) {
        auto separated  = remaining.split_once("."_str);
        auto identifier = separated.is_some() ? separated->template get<0>() : remaining;
        if (identifier.is_empty()) {
            return registry_value_failure<Vec<String>>(
                "semantic version prerelease identifiers must not be empty"_str);
        }
        for (auto byte : identifier.as_bytes()) {
            const auto ascii = byte.to_primitive();
            const auto valid = (ascii >= 'a' && ascii <= 'z') || (ascii >= 'A' && ascii <= 'Z') ||
                               (ascii >= '0' && ascii <= '9') || ascii == '-';
            if (! valid) {
                return registry_value_failure<Vec<String>>(
                    "semantic version prerelease contains an invalid byte"_str);
            }
        }
        if (identifier.len() > usize(1) && identifier[usize()] == u8('0') &&
            prerelease_identifier_is_numeric(identifier)) {
            return registry_value_failure<Vec<String>>(
                "numeric prerelease identifiers must not contain leading zeroes"_str);
        }
        result.push(String::make(identifier));
        if (separated.is_none()) break;
        remaining = separated->template get<1>();
    }
    return Ok(rstd::move(result));
}

auto compare_prerelease_identifier(ref<str> left, ref<str> right) noexcept -> strong_ordering {
    const auto left_numeric  = prerelease_identifier_is_numeric(left);
    const auto right_numeric = prerelease_identifier_is_numeric(right);
    if (left_numeric && ! right_numeric) return strong_ordering::less;
    if (! left_numeric && right_numeric) return strong_ordering::greater;
    if (left_numeric && right_numeric && left.len() != right.len()) {
        return left.len() < right.len() ? strong_ordering::less : strong_ordering::greater;
    }
    const auto common = left.len() < right.len() ? left.len() : right.len();
    for (usize index {}; index < common; ++index) {
        if (left[index] < right[index]) return strong_ordering::less;
        if (right[index] < left[index]) return strong_ordering::greater;
    }
    if (left.len() < right.len()) return strong_ordering::less;
    if (right.len() < left.len()) return strong_ordering::greater;
    return strong_ordering::equal;
}

struct PartialVersion {
    u64         major {};
    u64         minor {};
    u64         patch {};
    usize       components {};
    bool        has_prerelease {};
    Vec<String> prerelease;
};

auto parse_partial_version(ref<str> value, bool allow_prerelease)
    -> RegistryValueResult<PartialVersion> {
    if (value.is_empty() || value.contains("+"_str)) {
        return registry_value_failure<PartialVersion>(
            "version must not be empty or contain build metadata"_str);
    }
    auto separated  = value.split_once("-"_str);
    auto core       = separated.is_some() ? separated->template get<0>() : value;
    auto prerelease = separated.is_some() ? Some(separated->template get<1>()) : None<ref<str>>();
    if (prerelease.is_some() && ! allow_prerelease) {
        return registry_value_failure<PartialVersion>(
            "this version requirement form does not accept prerelease versions"_str);
    }

    auto pieces    = Vec<ref<str>>::make();
    auto remaining = core;
    while (true) {
        auto part  = remaining.split_once("."_str);
        auto piece = part.is_some() ? part->template get<0>() : remaining;
        pieces.push(rstd::move(piece));
        if (part.is_none()) break;
        remaining = part->template get<1>();
    }
    if (pieces.is_empty() || pieces.len() > usize(3)) {
        return registry_value_failure<PartialVersion>(
            "semantic version must contain one to three numeric components here"_str);
    }
    auto result       = PartialVersion {};
    result.components = pieces.len();
    auto major        = parse_version_number(pieces[usize()], "major"_str);
    if (major.is_err()) return Err(rstd::move(major).unwrap_err());
    result.major = rstd::move(major).unwrap();
    if (pieces.len() >= usize(2)) {
        auto minor = parse_version_number(pieces[usize(1)], "minor"_str);
        if (minor.is_err()) return Err(rstd::move(minor).unwrap_err());
        result.minor = rstd::move(minor).unwrap();
    }
    if (pieces.len() == usize(3)) {
        auto patch = parse_version_number(pieces[usize(2)], "patch"_str);
        if (patch.is_err()) return Err(rstd::move(patch).unwrap_err());
        result.patch = rstd::move(patch).unwrap();
    }
    if (prerelease.is_some()) {
        if (pieces.len() != usize(3)) {
            return registry_value_failure<PartialVersion>(
                "prerelease requirements require major, minor, and patch"_str);
        }
        auto parsed = parse_prerelease(*prerelease);
        if (parsed.is_err()) return Err(rstd::move(parsed).unwrap_err());
        result.has_prerelease = true;
        result.prerelease     = rstd::move(parsed).unwrap();
    }
    return Ok(rstd::move(result));
}

auto version_from_partial(const PartialVersion& value) -> SemanticVersion {
    auto text = rstd::format("{}.{}.{}", value.major, value.minor, value.patch);
    if (value.has_prerelease) {
        text.push_ascii('-');
        for (usize index {}; index < value.prerelease.len(); ++index) {
            if (index != usize {}) text.push_ascii('.');
            text.push_str(value.prerelease[index].as_str());
        }
    }
    return rstd::move(SemanticVersion::parse(text.as_str())).unwrap();
}

auto plain_version(u64 major, u64 minor, u64 patch) -> SemanticVersion {
    return rstd::move(
               SemanticVersion::parse(rstd::format("{}.{}.{}", major, minor, patch).as_str()))
        .unwrap();
}

auto incremented(u64 value, ref<str> component) -> RegistryValueResult<u64> {
    auto next = value.checked_add(u64(1));
    if (next.is_none()) {
        return registry_value_failure<u64>(
            rstd::format("version {} component cannot form an upper bound", component));
    }
    return Ok(*next);
}

auto append_range(Vec<VersionComparator>& output, SemanticVersion lower, SemanticVersion upper)
    -> void {
    output.push(VersionComparator {
        .operation = VersionComparatorOp::GreaterEqual,
        .version   = rstd::move(lower),
    });
    output.push(VersionComparator {
        .operation = VersionComparatorOp::Less,
        .version   = rstd::move(upper),
    });
}

auto append_caret(Vec<VersionComparator>& output, const PartialVersion& value)
    -> RegistryValueResult<empty> {
    auto lower = version_from_partial(value);
    if (value.major != u64 {}) {
        auto next = incremented(value.major, "major"_str);
        if (next.is_err()) return Err(rstd::move(next).unwrap_err());
        append_range(output, rstd::move(lower), plain_version(*next, u64 {}, u64 {}));
        return Ok(empty {});
    }
    if (value.components == usize(1)) {
        append_range(output, rstd::move(lower), plain_version(u64(1), u64 {}, u64 {}));
        return Ok(empty {});
    }
    if (value.minor != u64 {} || value.components == usize(2)) {
        auto next = incremented(value.minor, "minor"_str);
        if (next.is_err()) return Err(rstd::move(next).unwrap_err());
        append_range(output, rstd::move(lower), plain_version(u64 {}, *next, u64 {}));
        return Ok(empty {});
    }
    auto next = incremented(value.patch, "patch"_str);
    if (next.is_err()) return Err(rstd::move(next).unwrap_err());
    append_range(output, rstd::move(lower), plain_version(u64 {}, u64 {}, *next));
    return Ok(empty {});
}

auto append_tilde(Vec<VersionComparator>& output, const PartialVersion& value)
    -> RegistryValueResult<empty> {
    auto lower = version_from_partial(value);
    if (value.components == usize(1)) {
        auto next = incremented(value.major, "major"_str);
        if (next.is_err()) return Err(rstd::move(next).unwrap_err());
        append_range(output, rstd::move(lower), plain_version(*next, u64 {}, u64 {}));
        return Ok(empty {});
    }
    auto next = incremented(value.minor, "minor"_str);
    if (next.is_err()) return Err(rstd::move(next).unwrap_err());
    append_range(output, rstd::move(lower), plain_version(value.major, *next, u64 {}));
    return Ok(empty {});
}

auto append_wildcard(Vec<VersionComparator>& output, ref<str> value) -> RegistryValueResult<empty> {
    if (value == "*"_str) {
        return registry_value_failure<empty>("bare '*' version requirement is not supported"_str);
    }
    if (! value.ends_with(".*"_str) || value.contains("-"_str) || value.contains("+"_str)) {
        return registry_value_failure<empty>("wildcard must be written as 'x.*' or 'x.y.*'"_str);
    }
    auto prefix = value.get(usize {}, value.len() - usize(2)).unwrap();
    auto parsed = parse_partial_version(prefix, false);
    if (parsed.is_err()) return Err(rstd::move(parsed).unwrap_err());
    auto partial = rstd::move(parsed).unwrap();
    if (partial.components > usize(2)) {
        return registry_value_failure<empty>("wildcard has too many version components"_str);
    }
    return append_tilde(output, partial);
}

auto append_requirement_clause(Vec<VersionComparator>& output, ref<str> clause)
    -> RegistryValueResult<Option<SemanticVersion>> {
    if (clause.is_empty()) {
        return registry_value_failure<Option<SemanticVersion>>(
            "version requirement contains an empty clause"_str);
    }
    if (clause.contains("||"_str) || clause.contains("+"_str)) {
        return registry_value_failure<Option<SemanticVersion>>(
            "version requirement unions and build metadata are not supported"_str);
    }
    if (clause.contains("*"_str)) {
        auto appended = append_wildcard(output, clause);
        if (appended.is_err()) return Err(rstd::move(appended).unwrap_err());
        return Ok(None());
    }

    auto operation = VersionComparatorOp::Equal;
    auto version   = clause;
    bool range     = false;
    bool tilde     = false;
    if (clause.starts_with(">="_str)) {
        operation = VersionComparatorOp::GreaterEqual;
        version   = clause.get(usize(2), clause.len()).unwrap();
    } else if (clause.starts_with("<="_str)) {
        operation = VersionComparatorOp::LessEqual;
        version   = clause.get(usize(2), clause.len()).unwrap();
    } else if (clause.starts_with(">"_str)) {
        operation = VersionComparatorOp::Greater;
        version   = clause.get(usize(1), clause.len()).unwrap();
    } else if (clause.starts_with("<"_str)) {
        operation = VersionComparatorOp::Less;
        version   = clause.get(usize(1), clause.len()).unwrap();
    } else if (clause.starts_with("="_str)) {
        operation = VersionComparatorOp::Equal;
        version   = clause.get(usize(1), clause.len()).unwrap();
    } else if (clause.starts_with("^"_str)) {
        range   = true;
        version = clause.get(usize(1), clause.len()).unwrap();
    } else if (clause.starts_with("~"_str)) {
        range   = true;
        tilde   = true;
        version = clause.get(usize(1), clause.len()).unwrap();
    } else {
        range = true;
    }

    auto parsed = parse_partial_version(version, true);
    if (parsed.is_err()) return Err(rstd::move(parsed).unwrap_err());
    auto partial = rstd::move(parsed).unwrap();
    if (! range && operation == VersionComparatorOp::Equal && partial.components != usize(3)) {
        return registry_value_failure<Option<SemanticVersion>>(
            "exact version requirement requires major, minor, and patch"_str);
    }
    auto prerelease =
        partial.has_prerelease ? Some(version_from_partial(partial)) : None<SemanticVersion>();
    if (range) {
        auto appended = tilde ? append_tilde(output, partial) : append_caret(output, partial);
        if (appended.is_err()) return Err(rstd::move(appended).unwrap_err());
    } else {
        output.push(VersionComparator {
            .operation = operation,
            .version   = version_from_partial(partial),
        });
    }
    return Ok(rstd::move(prerelease));
}

} // namespace lito::registry

auto lito::registry::SemanticVersion::parse(ref<str> value)
    -> RegistryValueResult<SemanticVersion> {
    if (value.starts_with("v"_str)) {
        return registry_value_failure<SemanticVersion>(
            "semantic version must not start with 'v'"_str);
    }
    auto parsed = parse_partial_version(value, true);
    if (parsed.is_err()) return Err(rstd::move(parsed).unwrap_err());
    auto partial = rstd::move(parsed).unwrap();
    if (partial.components != usize(3)) {
        return registry_value_failure<SemanticVersion>(
            "semantic version requires major, minor, and patch"_str);
    }
    return Ok(SemanticVersion(
        partial.major, partial.minor, partial.patch, rstd::move(partial.prerelease)));
}

auto lito::registry::SemanticVersion::text() const -> String {
    auto result = rstd::format("{}.{}.{}", major_, minor_, patch_);
    if (! prerelease_.is_empty()) {
        result.push_ascii('-');
        for (usize index {}; index < prerelease_.len(); ++index) {
            if (index != usize {}) result.push_ascii('.');
            result.push_str(prerelease_[index].as_str());
        }
    }
    return result;
}

auto lito::registry::SemanticVersion::clone() const -> SemanticVersion {
    return SemanticVersion(major_, minor_, patch_, prerelease_.clone());
}

auto lito::registry::SemanticVersion::cmp(const SemanticVersion& other) const noexcept
    -> strong_ordering {
    if (major_ != other.major_) return major_ <=> other.major_;
    if (minor_ != other.minor_) return minor_ <=> other.minor_;
    if (patch_ != other.patch_) return patch_ <=> other.patch_;
    if (prerelease_.is_empty() && other.prerelease_.is_empty()) return strong_ordering::equal;
    if (prerelease_.is_empty()) return strong_ordering::greater;
    if (other.prerelease_.is_empty()) return strong_ordering::less;
    const auto common =
        prerelease_.len() < other.prerelease_.len() ? prerelease_.len() : other.prerelease_.len();
    for (usize index {}; index < common; ++index) {
        auto compared = compare_prerelease_identifier(prerelease_[index].as_str(),
                                                      other.prerelease_[index].as_str());
        if (compared != strong_ordering::equal) return compared;
    }
    if (prerelease_.len() < other.prerelease_.len()) return strong_ordering::less;
    if (prerelease_.len() > other.prerelease_.len()) return strong_ordering::greater;
    return strong_ordering::equal;
}

auto lito::registry::VersionRequirement::parse(ref<str> value)
    -> RegistryValueResult<VersionRequirement> {
    if (value.trim_ascii().is_empty()) {
        return registry_value_failure<VersionRequirement>(
            "version requirement must not be empty"_str);
    }
    auto comparators       = Vec<VersionComparator>::make();
    auto canonical_text    = String::make();
    auto remaining         = value;
    bool admits_prerelease = false;
    u64  prerelease_major {};
    u64  prerelease_minor {};
    u64  prerelease_patch {};
    while (true) {
        auto separated = remaining.split_once(","_str);
        auto clause = (separated.is_some() ? separated->template get<0>() : remaining).trim_ascii();
        if (! canonical_text.is_empty()) canonical_text.push_str(", "_str);
        canonical_text.push_str(clause);
        auto appended = append_requirement_clause(comparators, clause);
        if (appended.is_err()) return Err(rstd::move(appended).unwrap_err());
        auto explicit_prerelease = rstd::move(appended).unwrap();
        if (explicit_prerelease.is_some()) {
            if (admits_prerelease && (prerelease_major != explicit_prerelease->major() ||
                                      prerelease_minor != explicit_prerelease->minor() ||
                                      prerelease_patch != explicit_prerelease->patch())) {
                return registry_value_failure<VersionRequirement>(
                    "one requirement cannot opt into multiple prerelease base versions"_str);
            }
            admits_prerelease = true;
            prerelease_major  = explicit_prerelease->major();
            prerelease_minor  = explicit_prerelease->minor();
            prerelease_patch  = explicit_prerelease->patch();
        }
        if (separated.is_none()) break;
        remaining = separated->template get<1>();
    }
    return Ok(VersionRequirement(rstd::move(canonical_text),
                                 rstd::move(comparators),
                                 admits_prerelease,
                                 prerelease_major,
                                 prerelease_minor,
                                 prerelease_patch));
}

auto lito::registry::VersionRequirement::matches(const SemanticVersion& version) const noexcept
    -> bool {
    if (version.is_prerelease() &&
        (! admits_prerelease_ || prerelease_major_ != version.major() ||
         prerelease_minor_ != version.minor() || prerelease_patch_ != version.patch())) {
        return false;
    }
    for (const auto& comparator : comparators_) {
        const auto compared = version.cmp(comparator.version);
        switch (comparator.operation) {
        case VersionComparatorOp::Less:
            if (compared != strong_ordering::less) return false;
            break;
        case VersionComparatorOp::LessEqual:
            if (compared == strong_ordering::greater) return false;
            break;
        case VersionComparatorOp::Equal:
            if (compared != strong_ordering::equal) return false;
            break;
        case VersionComparatorOp::GreaterEqual:
            if (compared == strong_ordering::less) return false;
            break;
        case VersionComparatorOp::Greater:
            if (compared != strong_ordering::greater) return false;
            break;
        }
    }
    return true;
}

auto lito::registry::VersionRequirement::clone() const -> VersionRequirement {
    auto comparators = Vec<VersionComparator>::with_capacity(comparators_.len());
    for (const auto& comparator : comparators_) comparators.push(comparator.clone());
    return VersionRequirement(text_.clone(),
                              rstd::move(comparators),
                              admits_prerelease_,
                              prerelease_major_,
                              prerelease_minor_,
                              prerelease_patch_);
}

auto lito::registry::matches(const VersionRequirement& requirement,
                             const SemanticVersion&    version) noexcept -> bool {
    return requirement.matches(version);
}
