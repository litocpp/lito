export module lito.frontend.lexical:scanner;

import rstd;
import rstd.parse.core;
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
        SourceLocation                                 location;
        rstd::parse::PositionedCursor::checkpoint_type checkpoint;
    };

public:
    ScannerCursor(const ScannerCursor&)                    = delete;
    auto operator=(const ScannerCursor&) -> ScannerCursor& = delete;
    ~ScannerCursor() {
        if (! committed_) cursor_.rewind(start_.checkpoint);
    }

    auto lookahead() const noexcept -> Option<u8> {
        auto value = cursor_.peek();
        if (value.is_none()) return None();
        return Some(value->get());
    }

    auto advance(bool skip = false) noexcept -> void {
        if (! cursor_.advance(usize(1))) return;
        if (skip) {
            begin_      = point();
            has_marked_ = false;
        }
    }

    auto mark_end() noexcept -> void {
        marked_     = point();
        has_marked_ = true;
    }

    auto location() const noexcept -> SourceLocation { return point().location; }
    auto begin() const noexcept -> SourceLocation { return begin_.location; }
    auto is_eof() const noexcept -> bool { return cursor_.is_eof(); }

    auto lexeme() const noexcept -> slice<u8> {
        return cursor_.view({ begin_.location.offset, selected_end().location.offset });
    }

private:
    ScannerCursor(rstd::parse::PositionedCursor& cursor, SourceId source) noexcept
        : cursor_(cursor), source_(source), start_(point()), begin_(start_), marked_(start_) {}

    auto point() const noexcept -> Point {
        auto position = cursor_.source_position();
        return { { source_, cursor_.position(), position.line, position.column },
                 cursor_.checkpoint() };
    }

    auto selected_end() const noexcept -> Point { return has_marked_ ? marked_ : point(); }
    void commit() noexcept {
        cursor_.rewind(selected_end().checkpoint);
        committed_ = true;
    }

    rstd::parse::PositionedCursor& cursor_;
    SourceId                       source_;
    Point                          start_;
    Point                          begin_;
    Point                          marked_;
    bool                           has_marked_ {};
    bool                           committed_ {};

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
        requires Impled<LanguageType, Language>
    static auto make(SourceFile source) -> ScannerSession {
        return ScannerSession(rstd::move(source), LanguageId::of<LanguageType>());
    }

    auto position() const noexcept -> SourceLocation { return committed_; }
    auto is_eof() const noexcept -> bool { return committed_.offset >= source_.contents().len(); }

    template<typename ScannerType>
        requires Impled<ScannerType, Scanner>
    auto scan(ScannerType& scanner, slice<SymbolId> valid_symbol_slice)
        -> Result<Option<ScannedToken>> {
        auto valid_symbols = ValidSymbols { valid_symbol_slice };
        if (! valid_symbols.belongs_to(language_)) {
            return Err(Error::at(String::make("valid symbol belongs to another language"_str),
                                 position()));
        }

        auto cursor  = ScannerCursor { cursor_, source_.id };
        auto scanned = as<Scanner>(scanner).scan(cursor, valid_symbols);
        if (scanned.is_err()) return Err(rstd::move(scanned).unwrap_err());
        if (scanned->is_none()) return Ok(None());

        auto symbol = **scanned;
        if (symbol.language() != language_) {
            return Err(
                Error::at(String::make("scanner returned a symbol from another language"_str),
                          cursor.start_.location));
        }
        if (! valid_symbols.contains(symbol)) {
            return Err(Error::at(String::make("scanner returned a symbol that is not valid"_str),
                                 cursor.start_.location));
        }

        auto end = cursor.selected_end();
        if (end.location.offset <= cursor.begin_.location.offset) {
            return Err(
                Error::at(String::make("scanner returned a token without consuming input"_str),
                          cursor.start_.location));
        }

        auto token = ScannedToken {
            .symbol = symbol,
            .begin  = cursor.begin_.location,
            .end    = end.location,
        };
        cursor.commit();
        committed_ = end.location;
        return Ok(Some(token));
    }

private:
    ScannerSession(SourceFile source, LanguageId language)
        : source_(rstd::move(source)),
          language_(language),
          cursor_(rstd::parse::Input<u8>(source_.contents())),
          committed_({ .source = source_.id }) {}

    SourceFile                    source_;
    LanguageId                    language_;
    rstd::parse::PositionedCursor cursor_;
    SourceLocation                committed_;
};

} // namespace lito::frontend::lexical
