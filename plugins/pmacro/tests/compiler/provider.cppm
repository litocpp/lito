export module pmacro.compiler_test;

import std;
import pmacro;

[[pmacro::define("compiler-test")]]
auto replace(pmacro::AttributeInput, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    return context.parse("struct Value { long value; Value* next; };");
}

[[pmacro::define("compiler-test")]]
auto after(pmacro::DeriveInput, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    return context.parse(
        "using generated_choice = Choice; constexpr int generated() { return 42; }");
}

[[pmacro::define("compiler-test")]]
auto at_end(pmacro::DeriveInput, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    auto stream = context.parse(
        "using later_type = Later; constexpr int deferred() { return sizeof(Later); }");
    if (! stream || ! context.emit(pmacro::OutputLocation::NamespaceEnd, *stream))
        return std::unexpected(pmacro::Error::Failure);
    return context.stream();
}

[[pmacro::define("compiler-test")]]
auto import_function(pmacro::AttributeInput, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    return context.parse("extern \"C\" int raw_import(int value) { return value+1; } int "
                         "imported(int value) { return raw_import(value); }");
}

[[pmacro::define("compiler-test")]]
auto export_function(pmacro::AttributeInput, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    return context.parse("int exported(int value) { return value+1; } extern \"C\" int wrapper(int "
                         "value) { return exported(value); }");
}

[[pmacro::define("compiler-test")]]
auto inspect(pmacro::AttributeInput input, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    auto first  = context.declaration();
    auto second = context.declaration();
    if (! first || ! second || *first != *second) return std::unexpected(pmacro::Error::Failure);
    const auto* record = std::get_if<pmacro::RecordInfo>(&(*first)->data);
    if (! record || record->fields.size() != 2 || ! record->fields[0].type.type_id)
        return std::unexpected(pmacro::Error::Failure);
    auto type   = context.tokens(*record->fields[0].type.type_id);
    auto output = context.parse("using inspected_type = ");
    auto end    = context.parse(";");
    if (! type || ! output || ! end || ! output->append(*type) || ! output->append(*end) ||
        ! context.emit(pmacro::OutputLocation::AfterItem, *output))
        return std::unexpected(pmacro::Error::Failure);
    return input.item;
}

[[pmacro::define("compiler-test")]]
auto inspect_template(pmacro::AttributeInput input, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    auto value = context.declaration();
    if (! value) return std::unexpected(pmacro::Error::Failure);
    const auto* record = std::get_if<pmacro::RecordInfo>(&(*value)->data);
    if (! record || record->fields.size() != 1 || ! record->dependent ||
        ! record->fields[0].type.dependent)
        return std::unexpected(pmacro::Error::Failure);
    auto once = context.parse("inline constexpr int template_once = 1;");
    if (! once || ! context.emit(pmacro::OutputLocation::AfterItem, *once))
        return std::unexpected(pmacro::Error::Failure);
    return input.item;
}

[[pmacro::define("compiler-test")]]
auto rewrite(pmacro::AttributeInput input, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    if (input.arguments.size() != 1) return std::unexpected(pmacro::Error::InvalidArgument);
    const auto text = input.arguments[0].spelling();
    return context.parse(text.substr(1, text.size() - 2));
}

[[pmacro::define("compiler-test")]]
auto derive_generated(pmacro::AttributeInput, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    return context.parse(
        "struct [[pmacro::derive(\"compiler-test::after\")]] Generated { long value; };");
}

std::shared_ptr<const pmacro::Declaration> saved_snapshot;

[[pmacro::define("compiler-test")]]
auto first(pmacro::DeriveInput, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    auto declaration = context.declaration();
    if (! declaration) return std::unexpected(pmacro::Error::Failure);
    saved_snapshot = *declaration;
    auto after     = context.parse("inline constexpr int second_value = first_value + 1;");
    auto end       = context.parse("inline constexpr int first_end = sizeof(Later);");
    if (! after || ! end || ! context.emit(pmacro::OutputLocation::AfterItem, *after) ||
        ! context.emit(pmacro::OutputLocation::NamespaceEnd, *end))
        return std::unexpected(pmacro::Error::Failure);
    return context.parse("inline constexpr int first_value = 1;");
}

[[pmacro::define("compiler-test")]]
auto second(pmacro::DeriveInput, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    auto declaration = context.declaration();
    if (! declaration || *declaration != saved_snapshot)
        return std::unexpected(pmacro::Error::Failure);
    auto end = context.parse("inline constexpr int second_end = first_end + 1;");
    if (! end || ! context.emit(pmacro::OutputLocation::NamespaceEnd, *end))
        return std::unexpected(pmacro::Error::Failure);
    return context.parse("inline constexpr int third_value = second_value + 1;");
}

[[pmacro::define("compiler-test")]]
auto invalid_input(pmacro::AttributeInput, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    auto declaration = context.declaration();
    auto again       = context.declaration();
    if (declaration || again ||
        declaration.error() != pmacro::DeclarationError::InvalidDeclaration ||
        again.error() != declaration.error())
        return std::unexpected(pmacro::Error::Failure);
    return context.parse("struct Invalid {};");
}

[[pmacro::define("compiler-test")]]
auto no_namespace(pmacro::AttributeInput input, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    auto output = context.stream();
    if (! output) return std::unexpected(pmacro::Error::Failure);
    auto result = context.emit(pmacro::OutputLocation::NamespaceEnd, *output);
    if (result || result.error() != pmacro::Error::InvalidArgument)
        return std::unexpected(pmacro::Error::Failure);
    return input.item;
}

[[pmacro::define("compiler-test")]]
auto warning(pmacro::AttributeInput input, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    context.diagnostic(
        pmacro::DiagnosticLevel::Warning, input.item_span, "serialized plugin warning");
    return input.item;
}

[[pmacro::define("compiler-test")]]
auto fail_after(pmacro::AttributeInput, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    auto output = context.parse("static_assert(false, \"uncommitted-output\");");
    if (output) (void)context.emit(pmacro::OutputLocation::AfterItem, *output);
    return std::unexpected(pmacro::Error::Failure);
}

[[pmacro::define("compiler-test")]]
auto budget(pmacro::AttributeInput input, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    auto output = context.parse("static_assert(true, \"" + std::string(1024 * 1024, 'x') + "\");");
    if (! output) return std::unexpected(pmacro::Error::Failure);
    for (int i = 0; i < 17; ++i)
        if (! context.emit(pmacro::OutputLocation::AfterItem, *output))
            return std::unexpected(pmacro::Error::Failure);
    return input.item;
}

[[pmacro::define("compiler-test")]]
auto import_at_end(pmacro::DeriveInput, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    auto output = context.parse("namespace nested { import available; }");
    if (! output || ! context.emit(pmacro::OutputLocation::NamespaceEnd, *output))
        return std::unexpected(pmacro::Error::Failure);
    return context.stream();
}

[[pmacro::define("compiler-test")]]
auto function(pmacro::AttributeInput input, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    auto first  = context.declaration();
    auto second = context.declaration();
    auto fail   = [&]() -> std::expected<pmacro::TokenStream, pmacro::Error> {
        context.diagnostic(
            pmacro::DiagnosticLevel::Error, input.item_span, "function model mismatch");
        return std::unexpected(pmacro::Error::Failure);
    };
    if (! first || ! second || *first != *second) return fail();
    const auto* f = std::get_if<pmacro::FunctionInfo>(&(*first)->data);
    if (! f || ! (*first)->name || ! f->default_calling_convention) return fail();
    const auto& name = *(*first)->name;
    if (name == "defaults") {
        if (f->parameters.size() != 2 || f->parameters[0].name ||
            f->parameters[0].default_argument.state != pmacro::DefaultArgumentState::Available ||
            f->parameters[0].default_argument.expression->code->text != "2" ||
            f->exception.state != pmacro::ExceptionState::NonThrowing ||
            f->form != pmacro::FunctionForm::Declaration)
            return fail();
    } else if (name == "defined") {
        if (f->form != pmacro::FunctionForm::Definition || f->parameters[0].name != "value" ||
            f->return_type.type_id->text != "int" || ! f->inline_ || ! f->static_ ||
            f->exception.state != pmacro::ExceptionState::PotentiallyThrowing)
            return fail();
    } else if (name == "deduced") {
        if (f->return_type.type_id ||
            f->return_type.type_id.error() != pmacro::RepresentationError::UndeducedType)
            return fail();
    } else if (name == "adjusted") {
        if (f->parameters.size() != 2 || f->parameters[0].type.type_id->text != "int *" ||
            f->parameters[0].declared_type.type_id->text != "int[3]" ||
            f->parameters[1].declared_type.type_id->text != "int (double)")
            return fail();
    } else if (name == "dependent") {
        if (! f->template_ || ! f->dependent || ! f->parameters[0].type.dependent ||
            f->exception.state != pmacro::ExceptionState::Dependent)
            return fail();
    } else if (name == "linked") {
        if (f->language_linkage != pmacro::LanguageLinkage::C) return fail();
    } else if (name == "pack") {
        if (! f->parameters[0].parameter_pack) return fail();
    } else if (name == "inherited") {
        if (f->parameters[0].default_argument.state != pmacro::DefaultArgumentState::None)
            return fail();
    } else if (name == "zero") {
        if (! f->parameters.empty() || ! f->constexpr_) return fail();
    } else if (name == "complex") {
        if (f->parameters.size() != 2 || ! f->parameters[0].type.type_id || ! f->variadic)
            return fail();
    } else
        return fail();
    if (f->return_type.type_id && ! context.tokens(*f->return_type.type_id)) return fail();
    return input.item;
}

[[pmacro::define("compiler-test")]]
auto unsupported_function(pmacro::AttributeInput input, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    auto first  = context.declaration();
    auto second = context.declaration();
    if (first || second || first.error() != pmacro::DeclarationError::UnsupportedDeclaration ||
        first.error() != second.error())
        return std::unexpected(pmacro::Error::Failure);
    return input.item;
}

[[pmacro::define("compiler-test")]]
auto changed_function(pmacro::AttributeInput input, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    auto first  = context.declaration();
    auto second = context.declaration();
    if (first || second || first.error() != pmacro::DeclarationError::ChangedInput ||
        first.error() != second.error())
        return std::unexpected(pmacro::Error::Failure);
    return input.item;
}
