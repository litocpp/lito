export module lito.frontend.lexical:token;

import rstd;
import lito.frontend.static_name;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::frontend::lexical
{

using SourceId = usize;

struct SourceLocation {
    SourceId source {};
    usize    offset {};
    usize    line { usize(1) };
    usize    column { usize(1) };
};

enum class TokenKind : uint8_t
{
    Identifier,
    PpNumber,
    StringLiteral,
    CharacterLiteral,
    HeaderName,
    Punctuation,
    Newline,
};

class TokenTextIdentity {
public:
    TokenTextIdentity() = default;

    auto is_valid() const noexcept -> bool { return data_ != nullptr; }

private:
    explicit TokenTextIdentity(const byte* data) noexcept: data_(data) {}

    const byte* data_ {};

    friend class TokenText;
};

class TokenText {
public:
    TokenText() = default;
    TokenText(String text): TokenText(rstd::move(text).into_bytes()) {}
    explicit TokenText(Vec<u8> bytes)
        : owned_(rstd::move(bytes)), hash_(comparable_name_hash(owned_.as_slice())) {}

    static auto borrowed(slice<u8> bytes) -> TokenText {
        return borrowed(bytes, comparable_name_hash(bytes));
    }
    static auto borrowed(ref<str> text) -> TokenText { return borrowed(text.as_bytes()); }
    static auto borrowed(ref<str> text, uint64_t hash) -> TokenText {
        return borrowed(text.as_bytes(), hash);
    }
    static auto borrowed(slice<u8> bytes, uint64_t hash) -> TokenText {
        auto result         = TokenText {};
        result.borrowed_    = bytes;
        result.hash_        = hash;
        result.is_borrowed_ = true;
        return result;
    }
    auto as_bytes() const noexcept -> slice<u8> {
        return is_borrowed_ ? borrowed_ : owned_.as_slice();
    }
    auto utf8() const noexcept { return rstd::str_::from_utf8(as_bytes()); }
    auto len() const noexcept -> usize { return as_bytes().len(); }
    auto is_empty() const noexcept -> bool { return as_bytes().is_empty(); }
    auto clone_bytes() const -> Vec<u8> {
        auto bytes = Vec<u8>::with_capacity(len());
        bytes.extend_from_slice(as_bytes());
        return bytes;
    }
    auto clone() const -> TokenText { return TokenText(clone_bytes()); }
    auto comparable_hash() const noexcept -> uint64_t { return hash_; }
    auto matches(ref<str> text) const noexcept -> bool {
        return matches(comparable_name_hash(text), text);
    }
    auto matches(uint64_t hash, ref<str> text) const noexcept -> bool {
        if (hash_ != hash || len() != text.len()) return false;
        auto bytes = as_bytes();
        for (auto i = usize {}; i < bytes.len(); ++i)
            if (bytes[i] != text[i]) return false;
        return true;
    }
    auto operator==(ref<str> text) const noexcept -> bool { return matches(text); }
    auto operator==(const TokenText& other) const noexcept -> bool { return same_bytes(other); }
    auto same_bytes(const TokenText& other) const noexcept -> bool {
        if (hash_ != other.hash_ || len() != other.len()) return false;
        for (auto i = usize {}; i < len(); ++i)
            if (as_bytes()[i] != other.as_bytes()[i]) return false;
        return true;
    }
    template<typename Name>
    auto matches() const noexcept -> bool {
        return matches(Name::hash, Name::name);
    }
    auto borrowed_identity() const noexcept -> TokenTextIdentity {
        return TokenTextIdentity(is_borrowed_ ? borrowed_.as_ptr() : nullptr);
    }
    auto matches(TokenTextIdentity identity) const noexcept -> bool {
        return identity.is_valid() && is_borrowed_ && borrowed_.as_ptr() == identity.data_;
    }
    auto shared_clone() const -> TokenText {
        return is_borrowed_ ? borrowed(borrowed_, hash_) : TokenText { clone_bytes() };
    }
    auto operator=(String text) -> TokenText& { return *this = TokenText(rstd::move(text)); }
    auto push_bytes(slice<u8> bytes) -> void {
        if (is_borrowed_) {
            owned_       = clone_bytes();
            borrowed_    = {};
            is_borrowed_ = false;
        }
        owned_.extend_from_slice(bytes);
        hash_ = comparable_name_hash(owned_.as_slice());
    }
    auto push_str(ref<str> text) -> void { push_bytes(text.as_bytes()); }

    auto display() const -> String {
        auto result = String::make();
        auto bytes  = as_bytes();
        auto offset = usize {};
        while (offset < bytes.len()) {
            auto remaining = slice<u8>::from_raw_parts(bytes.as_ptr() + offset.to_primitive(),
                                                       bytes.len() - offset);
            auto decoded   = rstd::str_::from_utf8(remaining);
            if (decoded.is_ok()) {
                result.push_str(*decoded);
                break;
            }
            auto valid  = decoded.unwrap_err().valid_up_to();
            auto prefix = slice<u8>::from_raw_parts(remaining.as_ptr(), valid);
            result.push_str(rstd::str_::from_utf8(prefix).unwrap());
            offset += valid;
            constexpr char digits[] = "0123456789ABCDEF";
            auto           value    = bytes[offset].to_primitive();
            result.push_ascii('\\');
            result.push_ascii('x');
            result.push_ascii(digits[value >> 4]);
            result.push_ascii(digits[value & 15]);
            ++offset;
        }
        return result;
    }

private:
    slice<u8> borrowed_;
    Vec<u8>   owned_;
    uint64_t  hash_ { COMPARABLE_NAME_HASH_OFFSET };
    bool      is_borrowed_ { false };
};
struct Token {
    TokenKind                   kind { TokenKind::Punctuation };
    TokenText                   text;
    SourceLocation              spelling;
    SourceLocation              expansion;
    Option<rstd::path::PathBuf> presumed_path;
    bool                        start_of_line { false };
    bool                        leading_space { false };
    bool                        disable_expand { false };
    u32                         unavailable_macro_revision {};
    TokenTextIdentity           unavailable_macro;

    auto is_known_unavailable_macro(u32 revision) const noexcept -> bool {
        return unavailable_macro_revision == revision && text.matches(unavailable_macro);
    }

    auto mark_unavailable_macro(u32 revision) noexcept -> void {
        unavailable_macro_revision = revision;
        unavailable_macro          = text.borrowed_identity();
    }

    auto clone() const -> Token {
        return Token {
            .kind           = kind,
            .text           = text.shared_clone(),
            .spelling       = spelling,
            .expansion      = expansion,
            .presumed_path  = presumed_path.is_some()
                                  ? Some(rstd::path::PathBuf::from((*presumed_path).as_path()))
                                  : Option<rstd::path::PathBuf> {},
            .start_of_line  = start_of_line,
            .leading_space  = leading_space,
            .disable_expand = disable_expand,
            .unavailable_macro_revision = unavailable_macro_revision,
            .unavailable_macro          = unavailable_macro,
        };
    }
};

auto is_identifier_start(u8 value) noexcept -> bool {
    return (value >= u8('a') && value <= u8('z')) || (value >= u8('A') && value <= u8('Z')) ||
           value == u8('_') || value == u8('$') || value >= u8(0x80);
}

auto is_identifier_continue(u8 value) noexcept -> bool {
    return is_identifier_start(value) || (value >= u8('0') && value <= u8('9'));
}

} // namespace lito::frontend::lexical
