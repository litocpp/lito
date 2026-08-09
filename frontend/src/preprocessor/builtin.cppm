export module lito.frontend.preprocessor:builtin;

import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace lito::frontend::preprocessor
{

auto normalize_attribute_component(ref<str> value) -> ref<str> {
    if (value.len() >= usize(4) && value.starts_with("__"_str) && value.ends_with("__"_str)) {
        auto inner = value.get(usize(2), value.len() - usize(2));
        if (inner.is_some()) return *inner;
    }
    return value;
}

} // namespace lito::frontend::preprocessor

export namespace lito::frontend::preprocessor
{

enum class BuiltinQueryArgumentForm
{
    Tokens,
    StringLiteral,
};

struct PlainBuiltinQueryHandler {
    static constexpr auto form = BuiltinQueryArgumentForm::Tokens;

    static auto normalize(ref<str> value) -> String { return String::make(value); }

    static auto render(ref<str> value) -> String { return String::make(value); }
};

struct AttributeBuiltinQueryHandler {
    static constexpr auto form = BuiltinQueryArgumentForm::Tokens;

    static auto normalize(ref<str> value) -> String {
        return String::make(normalize_attribute_component(value));
    }

    static auto render(ref<str> value) -> String { return String::make(value); }
};

struct CppAttributeBuiltinQueryHandler {
    static constexpr auto form = BuiltinQueryArgumentForm::Tokens;

    static auto normalize(ref<str> value) -> String {
        auto separator = value.split_once("::"_str);
        if (separator.is_none()) return String::make(normalize_attribute_component(value));
        auto result = String::make(normalize_attribute_component(separator->template get<0>()));
        result.push_str("::"_str);
        result.push_str(normalize_attribute_component(separator->template get<1>()));
        return result;
    }

    static auto render(ref<str> value) -> String { return String::make(value); }
};

struct TargetBuiltinQueryHandler {
    static constexpr auto form = BuiltinQueryArgumentForm::Tokens;

    static auto normalize(ref<str> value) -> String {
        auto result = String::make();
        for (auto character : value) {
            if (character >= u8('A') && character <= u8('Z')) {
                character =
                    u8(character.to_primitive() + u8('a').to_primitive() - u8('A').to_primitive());
            }
            result.push_ascii(character);
        }
        return result;
    }

    static auto render(ref<str> value) -> String { return String::make(value); }
};

struct WarningBuiltinQueryHandler {
    static constexpr auto form = BuiltinQueryArgumentForm::StringLiteral;

    static auto normalize(ref<str> value) -> String { return String::make(value); }

    static auto render(ref<str> value) -> String { return rstd::format("\"{}\"", value); }
};

struct HasBuiltinQuery {
    using Handler              = PlainBuiltinQueryHandler;
    static constexpr auto name = "__has_builtin"_str;
};

struct HasConstexprBuiltinQuery {
    using Handler              = PlainBuiltinQueryHandler;
    static constexpr auto name = "__has_constexpr_builtin"_str;
};

struct HasFeatureQuery {
    using Handler              = PlainBuiltinQueryHandler;
    static constexpr auto name = "__has_feature"_str;
};

struct HasExtensionQuery {
    using Handler              = PlainBuiltinQueryHandler;
    static constexpr auto name = "__has_extension"_str;
};

struct HasCppAttributeQuery {
    using Handler              = CppAttributeBuiltinQueryHandler;
    static constexpr auto name = "__has_cpp_attribute"_str;
};

struct HasAttributeQuery {
    using Handler              = AttributeBuiltinQueryHandler;
    static constexpr auto name = "__has_attribute"_str;
};

struct HasDeclspecAttributeQuery {
    using Handler              = AttributeBuiltinQueryHandler;
    static constexpr auto name = "__has_declspec_attribute"_str;
};

struct HasWarningQuery {
    using Handler              = WarningBuiltinQueryHandler;
    static constexpr auto name = "__has_warning"_str;
};

struct IsTargetArchQuery {
    using Handler              = TargetBuiltinQueryHandler;
    static constexpr auto name = "__is_target_arch"_str;
};

struct IsTargetVendorQuery {
    using Handler              = TargetBuiltinQueryHandler;
    static constexpr auto name = "__is_target_vendor"_str;
};

struct IsTargetOsQuery {
    using Handler              = TargetBuiltinQueryHandler;
    static constexpr auto name = "__is_target_os"_str;
};

struct IsTargetEnvironmentQuery {
    using Handler              = TargetBuiltinQueryHandler;
    static constexpr auto name = "__is_target_environment"_str;
};

struct IsTargetVariantOsQuery {
    using Handler              = TargetBuiltinQueryHandler;
    static constexpr auto name = "__is_target_variant_os"_str;
};

struct IsTargetVariantEnvironmentQuery {
    using Handler              = TargetBuiltinQueryHandler;
    static constexpr auto name = "__is_target_variant_environment"_str;
};

template<typename... Types>
struct BuiltinTypeSet {
    template<typename Function>
    static constexpr auto for_each(Function&& function) -> void {
        (function(rstd::mtp::type_c<Types>), ...);
    }

    template<typename Function>
    static constexpr auto visit(ref<str> name, Function&& function) -> bool {
        return ((name == Types::name && (function(rstd::mtp::type_c<Types>), true)) || ...);
    }

    static constexpr auto contains(ref<str> name) -> bool { return ((name == Types::name) || ...); }

    template<typename... Additional>
    using With = BuiltinTypeSet<Types..., Additional...>;
};

using BuiltinQuerySet = BuiltinTypeSet<HasBuiltinQuery,
                                       HasConstexprBuiltinQuery,
                                       HasFeatureQuery,
                                       HasExtensionQuery,
                                       HasCppAttributeQuery,
                                       HasAttributeQuery,
                                       HasDeclspecAttributeQuery,
                                       HasWarningQuery,
                                       IsTargetArchQuery,
                                       IsTargetVendorQuery,
                                       IsTargetOsQuery,
                                       IsTargetEnvironmentQuery,
                                       IsTargetVariantOsQuery,
                                       IsTargetVariantEnvironmentQuery>;

using BuiltinQueryTransform = String (*)(ref<str>);

struct BuiltinQueryDefinition {
    ref<str>                 name;
    BuiltinQueryArgumentForm form { BuiltinQueryArgumentForm::Tokens };
    BuiltinQueryTransform    normalize {};
    BuiltinQueryTransform    render {};
};

template<typename Query>
inline constexpr auto BUILTIN_QUERY_DEFINITION = BuiltinQueryDefinition {
    .name      = Query::name,
    .form      = Query::Handler::form,
    .normalize = &Query::Handler::normalize,
    .render    = &Query::Handler::render,
};

class BuiltinQueryKey {
public:
    BuiltinQueryKey() = default;

    template<typename Query>
    static auto make(ref<str> argument) -> BuiltinQueryKey {
        auto result        = BuiltinQueryKey {};
        result.definition_ = rstd::addressof(BUILTIN_QUERY_DEFINITION<Query>);
        result.argument    = Query::Handler::normalize(argument);
        return result;
    }

    template<typename Query>
    auto is() const noexcept -> bool {
        return definition_ == rstd::addressof(BUILTIN_QUERY_DEFINITION<Query>);
    }

    auto name() const noexcept -> ref<str> { return definition_->name; }

    auto form() const noexcept -> BuiltinQueryArgumentForm { return definition_->form; }

    auto render_argument() const -> String { return definition_->render(argument.as_str()); }

    auto same_query(const BuiltinQueryKey& other) const noexcept -> bool {
        return definition_ == other.definition_;
    }

    auto clone() const -> BuiltinQueryKey {
        auto result        = BuiltinQueryKey {};
        result.definition_ = definition_;
        result.argument    = argument.clone();
        return result;
    }

    String argument;

private:
    const BuiltinQueryDefinition* definition_ { rstd::addressof(
        BUILTIN_QUERY_DEFINITION<HasBuiltinQuery>) };
};

struct LineBuiltin {
    static constexpr auto name = "__LINE__"_str;
};

struct FileBuiltin {
    static constexpr auto name = "__FILE__"_str;
};

struct BaseFileBuiltin {
    static constexpr auto name = "__BASE_FILE__"_str;
};

struct FileNameBuiltin {
    static constexpr auto name = "__FILE_NAME__"_str;
};

struct IncludeLevelBuiltin {
    static constexpr auto name = "__INCLUDE_LEVEL__"_str;
};

struct CounterBuiltin {
    static constexpr auto name = "__COUNTER__"_str;
};

struct DateBuiltin {
    static constexpr auto name = "__DATE__"_str;
};

struct TimeBuiltin {
    static constexpr auto name = "__TIME__"_str;
};

struct PragmaBuiltin {
    static constexpr auto name = "_Pragma"_str;
};

struct HasEmbedBuiltin {
    static constexpr auto name = "__has_embed"_str;
};

struct IsIdentifierBuiltin {
    static constexpr auto name = "__is_identifier"_str;
};

struct HasIncludeBuiltin {
    static constexpr auto name = "__has_include"_str;
};

struct HasIncludeNextBuiltin {
    static constexpr auto name = "__has_include_next"_str;
};

using DynamicBuiltinSet = BuiltinQuerySet::With<LineBuiltin,
                                                FileBuiltin,
                                                BaseFileBuiltin,
                                                FileNameBuiltin,
                                                IncludeLevelBuiltin,
                                                CounterBuiltin,
                                                DateBuiltin,
                                                TimeBuiltin,
                                                PragmaBuiltin,
                                                HasEmbedBuiltin,
                                                IsIdentifierBuiltin,
                                                HasIncludeBuiltin,
                                                HasIncludeNextBuiltin>;

} // namespace lito::frontend::preprocessor
