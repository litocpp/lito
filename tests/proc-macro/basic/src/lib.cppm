export module pmacro.basic_provider;

import std;
import pmacro;

auto contains_token(pmacro::TokenStream stream, std::string_view spelling) noexcept -> bool {
    for (auto token : stream) {
        if (token.spelling() == spelling) return true;
        if (token.kind() == pmacro::TokenKind::Group && contains_token(token.children(), spelling))
            return true;
    }
    return false;
}

[[pmacro::define()]]
auto identity(pmacro::AttributeInput input, pmacro::Context&) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    return input.item;
}

[[pmacro::define("pmacro-basic")]]
auto scoped_identity(pmacro::AttributeInput input, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    if (context.macro_identity() != "pmacro-basic::scoped_identity" ||
        context.provider_identity() != "pmacro-basic-provider")
        return std::unexpected(pmacro::Error::Failure);
    return input.item;
}

[[pmacro::define("pmacro-basic")]]
auto scoped_derive(pmacro::DeriveInput, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    if (context.macro_identity() != "pmacro-basic::scoped_derive" ||
        context.provider_identity() != "pmacro-basic-provider")
        return std::unexpected(pmacro::Error::Failure);
    return context.parse("inline constexpr bool scoped_derive_valid = true;");
}

[[pmacro::define("pmacro-basic")]]
auto scoped_invoke(pmacro::FunctionInput input, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    auto declaration = context.declaration();
    if (context.macro_identity() != "pmacro-basic::scoped_invoke" ||
        context.provider_identity() != "pmacro-basic-provider" || declaration ||
        declaration.error() != pmacro::DeclarationError::UnsupportedDeclaration ||
        input.scope != pmacro::DeclarationScope::Namespace ||
        input.after_item_export != pmacro::AfterItemExport::Inherited ||
        input.arguments.size() != 3 || input.arguments[0].spelling() != "InvokedValue" ||
        input.arguments[1].spelling() != "," ||
        input.arguments[2].delimiter() != pmacro::Delimiter::Brace ||
        ! contains_token(input.arguments, "missing_type") ||
        ! contains_token(input.arguments, "\"balanced,){}[]\""))
        return std::unexpected(pmacro::Error::Failure);
    return context.parse("struct InvokedValue { long value; }; "
                         "using InvokedAlias = InvokedValue; "
                         "inline constexpr bool scoped_invoke_valid = true;");
}

[[pmacro::define]]
auto invoke_tokens(pmacro::FunctionInput input, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    auto selected = input.arguments.slice(0, input.arguments.size());
    auto output   = context.stream();
    if (! selected || ! output || ! output->append(*selected))
        return std::unexpected(pmacro::Error::Failure);
    return output;
}

[[pmacro::define]]
auto splice_tokens(pmacro::FunctionInput input, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    const auto args = input.arguments;
    if (args.size() < 5 || args[2].delimiter() != pmacro::Delimiter::Brace)
        return std::unexpected(pmacro::Error::InvalidArgument);
    auto scaffold = args.slice(4, args.size());
    if (! scaffold) return std::unexpected(scaffold.error());
    const std::array bindings {
        pmacro::TokenSplice { "slot", args[0] },
        pmacro::TokenSplice { "nested", args[2].children() },
    };
    const std::array duplicate { bindings[0], bindings[0] };
    auto             rejected = context.splice(*scaffold, duplicate);
    if (rejected || rejected.error() != pmacro::Error::InvalidArgument)
        return std::unexpected(pmacro::Error::Failure);
    return context.splice(*scaffold, bindings);
}

[[pmacro::define]]
auto token_model(pmacro::AttributeInput input, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    auto       call_site   = context.call_site();
    auto       joined      = context.join(input.attribute_span, input.item_span);
    auto       prefix      = context.subspan(input.item_span, 0, 1);
    auto       identifiers = 0;
    auto       groups      = 0;
    auto       literals    = 0;
    auto       punctuation = 0;
    const auto count       = input.item.size();
    const auto empty       = input.item.slice(count, count);
    if (! empty || ! empty->valid() || empty->size() != 0 || input.item.slice(0, count + 1) ||
        input.item.slice(1, 0) || pmacro::TokenStream {}.slice(0, 0))
        return std::unexpected(pmacro::Error::Failure);
    for (std::size_t index = 0; index < count; ++index) {
        auto token = input.item[index];
        if (token.kind() == pmacro::TokenKind::Ident) ++identifiers;
        if (token.kind() != pmacro::TokenKind::Group) continue;
        ++groups;
        if (token.delimiter() != pmacro::Delimiter::Brace) continue;
        auto selected = input.item.slice(index, index + 1);
        if (! selected || selected->size() != 1) return std::unexpected(pmacro::Error::Failure);
        auto copied = (*selected)[0];
        if (copied.delimiter() != token.delimiter() || copied.origin() != token.origin() ||
            copied.span().source() != token.span().source() ||
            copied.span().begin() != token.span().begin() ||
            copied.span().end() != token.span().end() ||
            copied.children().size() != token.children().size() ||
            ! selected->append(input.item[0]) || input.item.size() != count)
            return std::unexpected(pmacro::Error::Failure);
        for (auto child : token.children()) {
            if (child.kind() == pmacro::TokenKind::Literal) ++literals;
            if (child.kind() == pmacro::TokenKind::Punct) ++punctuation;
        }
    }
    if (call_site && joined && prefix &&
        context.macro_identity().compare("pmacro-basic-provider::token_model") == 0 &&
        context.provider_identity().compare("pmacro-basic-provider") == 0 &&
        ! context.target_triple().empty() && identifiers >= 2 && groups == 1 && literals == 1 &&
        punctuation >= 2)
        return input.item;
    context.diagnostic(
        pmacro::DiagnosticLevel::Error, input.item_span, "invalid pmacro token model");
    return std::unexpected(pmacro::Error::Failure);
}

[[pmacro::define]]
auto arguments(pmacro::AttributeInput input, pmacro::Context&) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    auto literals = 0;
    auto commas   = 0;
    for (auto token : input.arguments) {
        if (token.kind() == pmacro::TokenKind::Literal) ++literals;
        if (token.kind() == pmacro::TokenKind::Punct && token.spelling().size() == 1 &&
            token.spelling()[0] == ',')
            ++commas;
    }
    if (literals == 2 && commas == 1) return input.item;
    return std::unexpected(pmacro::Error::Failure);
}

[[pmacro::define]]
auto derive_equal(pmacro::DeriveInput input, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    if (! contains_token(input.item, "helper") || ! contains_token(input.item, "\"value\""))
        return std::unexpected(pmacro::Error::Failure);
    return context.parse("constexpr auto operator==(const Equal& left, const Equal& right) "
                         "noexcept -> bool { return left.value == right.value; }");
}

[[pmacro::define]]
auto derive_marker(pmacro::DeriveInput, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    return context.parse("inline constexpr bool equal_has_marker = true;");
}

[[pmacro::define]]
auto replace(pmacro::AttributeInput, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    return context.parse("struct ReplaceSeed { int replacement; };");
}

[[pmacro::define]]
auto remove(pmacro::AttributeInput, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    return context.stream();
}

[[pmacro::define]]
auto emit_import(pmacro::AttributeInput, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    return context.parse("import lito.test.pmacro.generated;\n"
                         "struct ImportSeed { Generated value; };");
}

[[pmacro::define]]
auto recursive(pmacro::AttributeInput, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    return context.parse("struct [[pmacro::attr(\"pmacro-basic-provider::identity\")]] "
                         "RecursiveSeed { int value; };");
}

[[pmacro::define]]
auto recursive_limit(pmacro::AttributeInput, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    return context.parse("struct [[pmacro::attr(\"pmacro-basic-provider::recursive_limit\")]] "
                         "RecursiveLimitSeed {};");
}

[[pmacro::define]]
auto diagnostic(pmacro::AttributeInput input, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    if (! context.diagnostic(
            pmacro::DiagnosticLevel::Warning, input.item_span, "pmacro diagnostic fixture"))
        return std::unexpected(pmacro::Error::Failure);
    return input.item;
}
