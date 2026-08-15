export module lito.cpp:lexical.scanner;

import rstd;
import lito.frontend.lexical;
import lito.frontend.static_name;
import :lexical.token;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::cpp
{

using frontend::StaticName;

using CppIdentifierSymbolName = StaticName<"identifier">;
using CppModuleKeyword        = StaticName<"module">;
using CppImportKeyword        = StaticName<"import">;

struct CppLexicalLanguage;

inline constexpr auto CPP_IDENTIFIER_SYMBOL =
    frontend::lexical::SymbolId::of<CppLexicalLanguage>(u32(0));
inline constexpr auto CPP_MODULE_SYMBOL =
    frontend::lexical::SymbolId::of<CppLexicalLanguage>(u32(1));
inline constexpr auto CPP_IMPORT_SYMBOL =
    frontend::lexical::SymbolId::of<CppLexicalLanguage>(u32(2));
inline constexpr auto CPP_EXPORT_SYMBOL =
    frontend::lexical::SymbolId::of<CppLexicalLanguage>(u32(3));

struct CppLexicalLanguage {
    auto identity() const noexcept -> frontend::lexical::LanguageIdentity {
        return frontend::lexical::LanguageIdentity {
            .name     = "lito.cpp.lexical"_str,
            .revision = "words-v1"_str,
        };
    }

    auto symbol_info(frontend::lexical::SymbolId symbol) const noexcept
        -> Option<frontend::lexical::SymbolInfo> {
        if (! symbol.belongs_to<CppLexicalLanguage>()) return None();
        switch (symbol.local().to_primitive()) {
        case 0:
            return Some(frontend::lexical::SymbolInfo { .id    = CPP_IDENTIFIER_SYMBOL,
                                                        .name  = CppIdentifierSymbolName::name,
                                                        .named = true });
        case 1:
            return Some(frontend::lexical::SymbolInfo {
                .id = CPP_MODULE_SYMBOL, .name = CppModuleKeyword::name, .named = false });
        case 2:
            return Some(frontend::lexical::SymbolInfo {
                .id = CPP_IMPORT_SYMBOL, .name = CppImportKeyword::name, .named = false });
        case 3:
            return Some(frontend::lexical::SymbolInfo {
                .id = CPP_EXPORT_SYMBOL, .name = CppExportKeyword::name, .named = false });
        default: return None();
        }
    }
};

struct CppWordScanner {
    auto scan(frontend::lexical::ScannerCursor& cursor,
              frontend::lexical::ValidSymbols   valid_symbols)
        -> frontend::lexical::Result<Option<frontend::lexical::SymbolId>> {
        while (auto lookahead = cursor.lookahead()) {
            auto value = *lookahead;
            if (value != u8(' ') && value != u8('\t') && value != u8('\r') && value != u8('\n') &&
                value != u8('\f') && value != u8('\v')) {
                break;
            }
            cursor.advance(true);
        }

        auto first = cursor.lookahead();
        if (first.is_none() || ! frontend::lexical::is_identifier_start(*first)) return Ok(None());
        while (auto lookahead = cursor.lookahead()) {
            if (! frontend::lexical::is_identifier_continue(*lookahead)) break;
            cursor.advance();
        }
        cursor.mark_end();

        auto spelling = cursor.lexeme();
        if (spelling.is_none()) {
            return Err(frontend::lexical::Error::at(String::make("C++ word is not valid UTF-8"_str),
                                                    cursor.begin()));
        }
        auto hash = frontend::comparable_name_hash(*spelling);
        if (hash == CppModuleKeyword::hash && *spelling == CppModuleKeyword::name &&
            valid_symbols.contains(CPP_MODULE_SYMBOL)) {
            return Ok(Some<frontend::lexical::SymbolId>(CPP_MODULE_SYMBOL));
        }
        if (hash == CppImportKeyword::hash && *spelling == CppImportKeyword::name &&
            valid_symbols.contains(CPP_IMPORT_SYMBOL)) {
            return Ok(Some<frontend::lexical::SymbolId>(CPP_IMPORT_SYMBOL));
        }
        if (hash == CppExportKeyword::hash && *spelling == CppExportKeyword::name &&
            valid_symbols.contains(CPP_EXPORT_SYMBOL)) {
            return Ok(Some<frontend::lexical::SymbolId>(CPP_EXPORT_SYMBOL));
        }
        if (valid_symbols.contains(CPP_IDENTIFIER_SYMBOL) &&
            ! CppReservedIdentifierSet::contains(hash, *spelling)) {
            return Ok(Some<frontend::lexical::SymbolId>(CPP_IDENTIFIER_SYMBOL));
        }
        return Ok(None());
    }
};

static_assert(Impled<CppLexicalLanguage, frontend::lexical::Language>);
static_assert(Impled<CppWordScanner, frontend::lexical::Scanner>);

} // namespace lito::cpp
