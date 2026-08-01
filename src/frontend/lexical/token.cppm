export module tenon.frontend.lexical:token;

import rstd;

using namespace rstd::prelude;

export namespace tenon::frontend::lexical {

using SourceId = usize;

struct SourceLocation {
  SourceId source{};
  usize offset{};
  usize line{usize(1)};
  usize column{usize(1)};
};

enum class TokenKind {
  Identifier,
  PpNumber,
  StringLiteral,
  CharacterLiteral,
  HeaderName,
  Punctuation,
  Newline,
};

class TokenText {
public:
  TokenText() = default;
  TokenText(String text) : owned_(rstd::move(text)) {}

  static auto borrowed(ref<str> text) -> TokenText {
    auto result = TokenText{};
    result.borrowed_ = text;
    result.is_borrowed_ = true;
    return result;
  }

  auto as_str() const noexcept -> ref<str> {
    return is_borrowed_ ? borrowed_ : owned_.as_str();
  }

  auto len() const noexcept -> usize { return as_str().len(); }
  auto is_empty() const noexcept -> bool { return as_str().is_empty(); }
  auto clone() const -> String { return String::make(as_str()); }

  auto shared_clone() const -> TokenText {
    return is_borrowed_ ? borrowed(borrowed_) : TokenText{owned_.clone()};
  }

  auto operator=(String text) -> TokenText & {
    borrowed_ = ref<str>{};
    is_borrowed_ = false;
    owned_ = rstd::move(text);
    return *this;
  }

  auto push_str(ref<str> text) -> void {
    if (is_borrowed_) {
      owned_ = String::make(borrowed_);
      borrowed_ = ref<str>{};
      is_borrowed_ = false;
    }
    owned_.push_str(text);
  }

private:
  ref<str> borrowed_;
  String owned_;
  bool is_borrowed_{false};
};

struct Token {
  TokenKind kind{TokenKind::Punctuation};
  TokenText text;
  SourceLocation spelling;
  SourceLocation expansion;
  Option<rstd::path::PathBuf> presumed_path;
  bool start_of_line{false};
  bool leading_space{false};
  bool disable_expand{false};

  auto clone() const -> Token {
    return Token{
        .kind = kind,
        .text = text.shared_clone(),
        .spelling = spelling,
        .expansion = expansion,
        .presumed_path =
            presumed_path.is_some()
                ? Some(rstd::path::PathBuf::from((*presumed_path).as_path()))
                : Option<rstd::path::PathBuf>{},
        .start_of_line = start_of_line,
        .leading_space = leading_space,
        .disable_expand = disable_expand,
    };
  }
};

auto is_identifier_start(u8 value) noexcept -> bool {
  return (value >= u8('a') && value <= u8('z')) ||
         (value >= u8('A') && value <= u8('Z')) || value == u8('_') ||
         value >= u8(0x80);
}

auto is_identifier_continue(u8 value) noexcept -> bool {
  return is_identifier_start(value) || (value >= u8('0') && value <= u8('9'));
}

} // namespace tenon::frontend::lexical
