export module lito.frontend.format;

import rstd;
import lito.frontend.lexical;
import :syntax;
import :layout;

using namespace rstd::prelude;

export namespace lito::frontend::format
{

auto format_source(const lexical::SourceFile& source) -> lexical::Result<String> {
    auto syntax = parse_source(source);
    if (syntax.is_err()) return Err(rstd::move(syntax).unwrap_err());
    return layout_source(*syntax);
}

} // namespace lito::frontend::format
