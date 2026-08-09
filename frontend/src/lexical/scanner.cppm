export module lito.frontend.lexical:scanner;

import rstd;
import :symbol;
import :token;
import :source;
import :error;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::frontend::lexical
{

class ValidSymbols {
public:
    constexpr ValidSymbols() noexcept = default;
    explicit constexpr ValidSymbols(slice<SymbolId> symbols) noexcept: symbols_(symbols) {}

    constexpr auto contains(SymbolId symbol) const noexcept -> bool {
        for (auto candidate : symbols_) {
            if (candidate == symbol) return true;
        }
        return false;
    }

    constexpr auto belongs_to(LanguageId language) const noexcept -> bool {
        for (auto symbol : symbols_) {
            if (symbol.language() != language) return false;
        }
        return true;
    }

    constexpr auto as_slice() const noexcept -> slice<SymbolId> { return symbols_; }

private:
    slice<SymbolId> symbols_;
};

class ScannerCursor {
    struct Point {
        SourceLocation location;
        bool           after_carriage_return { false };
    };

public:
    auto lookahead() const noexcept -> Option<u8> {
        auto bytes = contents_.as_bytes();
        if (current_.location.offset >= bytes.len()) return None();
        return Some(bytes[current_.location.offset]);
    }

    auto advance(bool skip = false) noexcept -> void {
        auto value = lookahead();
        if (value.is_none()) return;

        ++current_.location.offset;
        if (*value == u8('\r')) {
            ++current_.location.line;
            current_.location.column       = usize(1);
            current_.after_carriage_return = true;
        } else if (*value == u8('\n')) {
            if (! current_.after_carriage_return) ++current_.location.line;
            current_.location.column       = usize(1);
            current_.after_carriage_return = false;
        } else {
            ++current_.location.column;
            current_.after_carriage_return = false;
        }

        if (skip) {
            begin_      = current_;
            has_marked_ = false;
        }
    }

    auto mark_end() noexcept -> void {
        marked_     = current_;
        has_marked_ = true;
    }

    auto location() const noexcept -> SourceLocation { return current_.location; }
    auto begin() const noexcept -> SourceLocation { return begin_.location; }
    auto is_eof() const noexcept -> bool { return lookahead().is_none(); }

    auto lexeme() const noexcept -> Option<ref<str>> {
        auto end = selected_end().location.offset;
        return contents_.get(begin_.location.offset, end);
    }

private:
    ScannerCursor(ref<str> contents, Point start) noexcept
        : contents_(contents), begin_(start), current_(start), marked_(start) {}

    auto selected_end() const noexcept -> Point { return has_marked_ ? marked_ : current_; }

    ref<str> contents_;
    Point    begin_;
    Point    current_;
    Point    marked_;
    bool     has_marked_ { false };

    friend class ScannerSession;
};

struct Scanner {
    template<typename Self, typename = void>
    struct Api {
        using Trait = Scanner;

        auto scan(ScannerCursor& cursor, ValidSymbols valid_symbols) -> Result<Option<SymbolId>> {
            return rstd::trait_call<0>(this, cursor, valid_symbols);
        }
    };

    template<typename T>
    using Funcs = TraitFuncs<&T::scan>;
};

struct ScannedToken {
    SymbolId       symbol;
    SourceLocation begin;
    SourceLocation end;
};

class ScannerSession {
public:
    template<typename LanguageType>
        requires rstd::Impled<LanguageType, Language>
    static auto make(SourceFile source) -> ScannerSession {
        auto result      = ScannerSession {};
        result.source_   = rstd::move(source);
        result.language_ = LanguageId::of<LanguageType>();
        result.position_ = ScannerCursor::Point {
            .location =
                SourceLocation {
                    .source = result.source_.id,
                    .offset = usize {},
                    .line   = usize(1),
                    .column = usize(1),
                },
        };
        return result;
    }

    auto position() const noexcept -> SourceLocation { return position_.location; }
    auto is_eof() const noexcept -> bool {
        return position_.location.offset >= source_.contents().len();
    }

    template<typename ScannerType>
        requires rstd::Impled<ScannerType, Scanner>
    auto scan(ScannerType& scanner, slice<SymbolId> valid_symbol_slice)
        -> Result<Option<ScannedToken>> {
        auto valid_symbols = ValidSymbols { valid_symbol_slice };
        if (! valid_symbols.belongs_to(language_)) {
            return Err(Error::at(String::make("valid symbol belongs to another language"_str),
                                 position_.location));
        }

        auto cursor  = ScannerCursor { source_.contents(), position_ };
        auto scanned = rstd::as<Scanner>(scanner).scan(cursor, valid_symbols);
        if (scanned.is_err()) return Err(rstd::move(scanned).unwrap_err());
        if (scanned->is_none()) return Ok(None());

        auto symbol = **scanned;
        if (symbol.language() != language_) {
            return Err(
                Error::at(String::make("scanner returned a symbol from another language"_str),
                          position_.location));
        }
        if (! valid_symbols.contains(symbol)) {
            return Err(Error::at(String::make("scanner returned a symbol that is not valid"_str),
                                 position_.location));
        }

        auto end = cursor.selected_end();
        if (end.location.offset <= cursor.begin_.location.offset) {
            return Err(
                Error::at(String::make("scanner returned a token without consuming input"_str),
                          position_.location));
        }

        auto token = ScannedToken {
            .symbol = symbol,
            .begin  = cursor.begin_.location,
            .end    = end.location,
        };
        position_ = end;
        return Ok(Some(token));
    }

private:
    ScannerSession() = default;

    SourceFile           source_;
    LanguageId           language_;
    ScannerCursor::Point position_;
};

} // namespace lito::frontend::lexical
