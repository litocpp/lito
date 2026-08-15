export module lito.frontend.lexical:traits;

import rstd;
import lito.frontend.static_name;
import :token;

using namespace rstd::prelude;

export namespace lito::frontend::lexical
{

struct TokenMatcher {
    template<typename Self, typename = void>
    struct Api {
        using Trait = TokenMatcher;

        auto matches(const Token& token) const noexcept -> bool {
            return rstd::trait_call<0>(this, token);
        }
    };

    template<typename T>
    using Funcs = TraitFuncs<&T::matches>;
};

template<typename Matcher>
    requires Impled<Matcher, TokenMatcher>
auto matches_token(const Matcher& matcher, const Token& token) noexcept -> bool {
    return as<TokenMatcher>(matcher).matches(token);
}

struct TokenKindMatcher {
    TokenKind kind { TokenKind::Punctuation };

    auto matches(const Token& token) const noexcept -> bool { return token.kind == kind; }
};

template<typename Name>
struct TokenTextMatcher {
    auto matches(const Token& token) const noexcept -> bool {
        return token.text.template matches<Name>();
    }
};

static_assert(Impled<TokenKindMatcher, TokenMatcher>);
static_assert(Impled<TokenTextMatcher<StaticName<"">>, TokenMatcher>);

} // namespace lito::frontend::lexical
