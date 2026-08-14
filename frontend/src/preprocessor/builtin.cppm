export module lito.frontend.preprocessor:builtin;

import rstd;
import lito.frontend.static_name;

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

using HasBuiltinQuery = StaticNameWithHandler<"__has_builtin", PlainBuiltinQueryHandler>;
using HasConstexprBuiltinQuery =
    StaticNameWithHandler<"__has_constexpr_builtin", PlainBuiltinQueryHandler>;

using HasFeatureQuery   = StaticNameWithHandler<"__has_feature", PlainBuiltinQueryHandler>;
using HasExtensionQuery = StaticNameWithHandler<"__has_extension", PlainBuiltinQueryHandler>;
using HasCppAttributeQuery =
    StaticNameWithHandler<"__has_cpp_attribute", CppAttributeBuiltinQueryHandler>;

using HasAttributeQuery = StaticNameWithHandler<"__has_attribute", AttributeBuiltinQueryHandler>;
using HasDeclspecAttributeQuery =
    StaticNameWithHandler<"__has_declspec_attribute", AttributeBuiltinQueryHandler>;

using HasWarningQuery     = StaticNameWithHandler<"__has_warning", WarningBuiltinQueryHandler>;
using IsTargetArchQuery   = StaticNameWithHandler<"__is_target_arch", TargetBuiltinQueryHandler>;
using IsTargetVendorQuery = StaticNameWithHandler<"__is_target_vendor", TargetBuiltinQueryHandler>;
using IsTargetOsQuery     = StaticNameWithHandler<"__is_target_os", TargetBuiltinQueryHandler>;
using IsTargetEnvironmentQuery =
    StaticNameWithHandler<"__is_target_environment", TargetBuiltinQueryHandler>;

using IsTargetVariantOsQuery =
    StaticNameWithHandler<"__is_target_variant_os", TargetBuiltinQueryHandler>;

using IsTargetVariantEnvironmentQuery =
    StaticNameWithHandler<"__is_target_variant_environment", TargetBuiltinQueryHandler>;

using BuiltinQuerySet = StaticNameSet<HasBuiltinQuery,
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

using LineBuiltin           = StaticName<"__LINE__">;
using FileBuiltin           = StaticName<"__FILE__">;
using BaseFileBuiltin       = StaticName<"__BASE_FILE__">;
using FileNameBuiltin       = StaticName<"__FILE_NAME__">;
using IncludeLevelBuiltin   = StaticName<"__INCLUDE_LEVEL__">;
using CounterBuiltin        = StaticName<"__COUNTER__">;
using DateBuiltin           = StaticName<"__DATE__">;
using TimeBuiltin           = StaticName<"__TIME__">;
using PragmaBuiltin         = StaticName<"_Pragma">;
using HasEmbedBuiltin       = StaticName<"__has_embed">;
using IsIdentifierBuiltin   = StaticName<"__is_identifier">;
using HasIncludeBuiltin     = StaticName<"__has_include">;
using HasIncludeNextBuiltin = StaticName<"__has_include_next">;
using BuildingModuleBuiltin = StaticName<"__building_module">;
using DynamicBuiltinSet     = BuiltinQuerySet::With<LineBuiltin,
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
                                                    HasIncludeNextBuiltin,
                                                    BuildingModuleBuiltin>;

} // namespace lito::frontend::preprocessor
