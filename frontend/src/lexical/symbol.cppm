export module lito.frontend.lexical:symbol;

import rstd;

using namespace rstd::prelude;

export namespace lito::frontend::lexical
{

class LanguageId {
public:
    constexpr LanguageId() noexcept = default;

    template<typename LanguageType>
    static constexpr auto of() noexcept -> LanguageId {
        return LanguageId { rstd::any::TypeId::of<LanguageType>() };
    }

    friend constexpr auto operator==(LanguageId, LanguageId) noexcept -> bool = default;

private:
    explicit constexpr LanguageId(rstd::any::TypeId value) noexcept: value_(value) {}

    rstd::any::TypeId value_ { rstd::any::TypeId::of<void>() };
};

class SymbolId {
public:
    constexpr SymbolId() noexcept = default;

    template<typename LanguageType>
    static constexpr auto of(u32 local) noexcept -> SymbolId {
        return SymbolId { LanguageId::of<LanguageType>(), local };
    }

    constexpr auto language() const noexcept -> LanguageId { return language_; }
    constexpr auto local() const noexcept -> u32 { return local_; }

    template<typename LanguageType>
    constexpr auto belongs_to() const noexcept -> bool {
        return language_ == LanguageId::of<LanguageType>();
    }

    friend constexpr auto operator==(SymbolId, SymbolId) noexcept -> bool = default;

private:
    constexpr SymbolId(LanguageId language, u32 local) noexcept
        : language_(language), local_(local) {}

    LanguageId language_;
    u32        local_ {};
};

struct LanguageIdentity {
    ref<str> name;
    ref<str> revision;
};

struct SymbolInfo {
    SymbolId id;
    ref<str> name;
    bool     named { false };
};

struct Language {
    template<typename Self, typename = void>
    struct Api {
        using Trait = Language;

        auto identity() const noexcept -> LanguageIdentity { return rstd::trait_call<0>(this); }

        auto symbol_info(SymbolId symbol) const noexcept -> Option<SymbolInfo> {
            return rstd::trait_call<1>(this, symbol);
        }
    };

    template<typename T>
    using Funcs = TraitFuncs<&T::identity, &T::symbol_info>;
};

template<typename LanguageType>
    requires rstd::Impled<LanguageType, Language>
auto language_identity(const LanguageType& language) noexcept -> LanguageIdentity {
    return rstd::as<Language>(language).identity();
}

template<typename LanguageType>
    requires rstd::Impled<LanguageType, Language>
auto language_symbol_info(const LanguageType& language, SymbolId symbol) noexcept
    -> Option<SymbolInfo> {
    if (! symbol.template belongs_to<LanguageType>()) return None();
    return rstd::as<Language>(language).symbol_info(symbol);
}

} // namespace lito::frontend::lexical
