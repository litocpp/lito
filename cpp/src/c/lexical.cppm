export module lito.cpp:c.lexical;

import rstd;
import lito.frontend.lexical;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::c
{

inline constexpr auto C_IDENTIFIER_RULE_ID = "lito-c-identifier-v1"_str;

struct CIdentifierTokenMatcher {
    auto matches(const frontend::lexical::Token& token) const noexcept -> bool {
        return token.kind == frontend::lexical::TokenKind::Identifier;
    }
};

static_assert(Impled<CIdentifierTokenMatcher, frontend::lexical::TokenMatcher>);

} // namespace lito::c
