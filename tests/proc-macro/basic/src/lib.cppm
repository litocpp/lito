module;

#include <expected>
#include <string_view>

export module pmacro.basic_provider;

import pmacro;

auto contains_token(pmacro::TokenStream stream, std::string_view spelling) noexcept -> bool {
    for (auto token : stream) {
        if (token.spelling() == spelling) return true;
        if (token.kind() == pmacro::TokenKind::Group && contains_token(token.children(), spelling))
            return true;
    }
    return false;
}

[[pmacro::define]]
auto identity(pmacro::AttributeInput input, pmacro::Context&) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    return input.item;
}

[[pmacro::define]]
auto token_model(pmacro::AttributeInput input, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    auto call_site   = context.call_site();
    auto joined      = context.join(input.attribute_span, input.item_span);
    auto prefix      = context.subspan(input.item_span, 0, 1);
    auto identifiers = 0;
    auto groups      = 0;
    auto literals    = 0;
    auto punctuation = 0;
    for (auto token : input.item) {
        if (token.kind() == pmacro::TokenKind::Ident) ++identifiers;
        if (token.kind() != pmacro::TokenKind::Group) continue;
        ++groups;
        if (token.delimiter() != pmacro::Delimiter::Brace) continue;
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
    return context.parse("struct Replaced { int replacement; };");
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
                         "struct ImportConsumer { Generated value; };");
}

[[pmacro::define]]
auto recursive(pmacro::AttributeInput, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    return context.parse("struct [[pmacro::attr(\"pmacro-basic-provider::identity\")]] "
                         "Recursive { int value; };");
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
