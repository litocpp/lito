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

enum class TokenKind
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
    TokenText(String text)
        : owned_(rstd::move(text)), hash_(comparable_name_hash(owned_.as_str())) {}

    static auto borrowed(ref<str> text) -> TokenText {
        auto result         = TokenText {};
        result.borrowed_    = text;
        result.hash_        = comparable_name_hash(text);
        result.is_borrowed_ = true;
        return result;
    }

    auto as_str() const noexcept -> ref<str> { return is_borrowed_ ? borrowed_ : owned_.as_str(); }

    auto len() const noexcept -> usize { return as_str().len(); }
    auto is_empty() const noexcept -> bool { return as_str().is_empty(); }
    auto clone() const -> String { return String::make(as_str()); }
    auto comparable_hash() const noexcept -> uint64_t { return hash_; }

    auto matches(uint64_t hash, ref<str> text) const noexcept -> bool {
        return hash_ == hash && as_str() == text;
    }

    template<typename Name>
    auto matches() const noexcept -> bool {
        return matches(Name::hash, Name::name);
    }

    auto borrowed_identity() const noexcept -> TokenTextIdentity {
        return TokenTextIdentity(is_borrowed_ ? borrowed_.data() : nullptr);
    }

    auto matches(TokenTextIdentity identity) const noexcept -> bool {
        return identity.is_valid() && is_borrowed_ && borrowed_.data() == identity.data_;
    }

    auto shared_clone() const -> TokenText {
        return is_borrowed_ ? borrowed(borrowed_) : TokenText { owned_.clone() };
    }

    auto operator=(String text) -> TokenText& {
        borrowed_    = ref<str> {};
        is_borrowed_ = false;
        owned_       = rstd::move(text);
        hash_        = comparable_name_hash(owned_.as_str());
        return *this;
    }

    auto push_str(ref<str> text) -> void {
        if (is_borrowed_) {
            owned_       = String::make(borrowed_);
            borrowed_    = ref<str> {};
            is_borrowed_ = false;
        }
        owned_.push_str(text);
        hash_ = comparable_name_hash(owned_.as_str());
    }

private:
    ref<str> borrowed_;
    String   owned_;
    uint64_t hash_ { COMPARABLE_NAME_HASH_OFFSET };
    bool     is_borrowed_ { false };
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
