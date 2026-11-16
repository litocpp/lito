export module lito.frontend.format;

import rstd;
import lito.frontend.lexical;
export import :style;
export import :config;
import :syntax;
import :layout;

using namespace rstd::prelude;

export namespace lito::frontend::format
{

auto format_source(const lexical::SourceFile& source, const FormatStyle& style)
    -> lexical::Result<String> {
    auto syntax = parse_source(source);
    if (syntax.is_err()) return Err(rstd::move(syntax).unwrap_err());
    return layout_source(*syntax, style);
}

auto format_source(const lexical::SourceFile& source) -> lexical::Result<String> {
    return format_source(source, FormatStyle {});
}

} // namespace lito::frontend::format
