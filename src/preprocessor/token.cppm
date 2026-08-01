export module tenon.preprocessor:token;

import rstd;

using namespace rstd::prelude;

export namespace tenon::preprocessor {

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
  RawLine,
};

struct Token {
  TokenKind kind{TokenKind::Punctuation};
  String text;
  SourceLocation spelling;
  SourceLocation expansion;
  Option<rstd::path::PathBuf> presumed_path;
  bool start_of_line{false};
  bool leading_space{false};
  bool disable_expand{false};

  auto clone() const -> Token {
    return Token{
        .kind = kind,
        .text = text.clone(),
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

} // namespace tenon::preprocessor
