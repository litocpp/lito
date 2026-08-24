export module lito.toolchain.clang:preprocessor_model;

import rstd;
import lito.cpp;
import lito.core;
import lito.system;
import lito.frontend;
import lito.toolchain.common;
import :options;

using namespace rstd::prelude;
using namespace lito::system;
using namespace rstd::literals;

namespace lito
{
namespace lexical      = frontend::lexical;
namespace preprocessor = frontend::preprocessor;
} // namespace lito

export namespace lito::toolchain
{

struct IncludeSearchEntry {
    PathBuf directory;
    bool    system { true };
};

struct BuiltinSemanticContext {
    String language_standard;
    bool   rtti { false };
    bool   exceptions { false };
};

enum class PreprocessorLanguage
{
    C,
    Cpp,
};

struct PreprocessorMacroDirective {
    bool   defined { true };
    String value;
};

inline constexpr auto CLANG_STANDARD_LIBRARY_CAPABILITY_ID =
    "lito-clang-standard-library-capabilities-v1"_str;

struct ClangBuiltinEnvironmentSnapshot {
    String                                   key;
    String                                   identity;
    Vec<preprocessor::SharedMacroDefinition> definitions;
    rstd::collections::HashMap<String, i64>  capabilities;
    usize                                    clang_macro_count {};
    usize                                    native_macro_count {};
    usize                                    clang_capability_count {};
    usize                                    native_capability_count {};
    usize                                    macro_output_bytes {};
    usize                                    capability_input_bytes {};
    usize                                    capability_output_bytes {};
};

using SharedClangBuiltinEnvironmentSnapshot = rstd::sync::Arc<ClangBuiltinEnvironmentSnapshot>;

struct PreprocessorEnvironmentKey {
    String  context_id;
    PathBuf working_directory;

    static auto make(ref<str> context_id, ref<rstd::path::Path> working_directory)
        -> PreprocessorEnvironmentKey {
        return PreprocessorEnvironmentKey {
            .context_id        = String::make(context_id),
            .working_directory = PathBuf::from(working_directory),
        };
    }

    auto matches(ref<str> context, ref<rstd::path::Path> working) const noexcept -> bool {
        return context_id.as_str() == context && working_directory.as_path() == working;
    }
};

struct PreprocessorEnvironment {
    PreprocessorEnvironmentKey                  key;
    SharedClangBuiltinEnvironmentSnapshot       builtin_environment;
    Vec<preprocessor::SharedMacroDefinition>    native_definitions;
    Vec<preprocessor::PredefinedMacroOperation> command_line_macros;
    BuiltinSemanticContext                      semantic_context;
    Vec<IncludeSearchEntry>                     include_search;
    Vec<String>                                 query_command;
    String                                      identity;
    String                                      date;
    String                                      time;
};

using SharedPreprocessorEnvironment = rstd::sync::Arc<PreprocessorEnvironment>;

struct PackageMacroEntry {
    String                                      name;
    String                                      dependency_key;
    String                                      value_identity;
    frontend::ExternalMacroState                state { frontend::ExternalMacroState::Undefined };
    Option<String>                              compiler_definition;
    Option<preprocessor::SharedMacroDefinition> definition;
};

class PackageMacroCatalog {
public:
    static auto system() -> PackageMacroCatalog {
        return PackageMacroCatalog(Vec<PackageMacroEntry>::make(),
                                   String::make("lito-system-macro-catalog-v1"_str));
    }

    static auto make(const cpp::PackageCompileMetadata& metadata) -> PackageMacroCatalog {
        auto entries = Vec<PackageMacroEntry>::with_capacity(metadata.features.len() + usize(1));
        auto raw_version        = metadata.version.is_some() ? metadata.version->as_str() : ""_str;
        auto literal            = string_literal(raw_version);
        auto version_definition = String::make("LITO_PKG_VERSION="_str);
        version_definition.push_str(literal.as_str());
        entries.push(PackageMacroEntry {
            .name                = String::make("LITO_PKG_VERSION"_str),
            .dependency_key      = String::make("lito.package.version"_str),
            .value_identity      = value_identity("lito-package-version-v1"_str, raw_version),
            .state               = frontend::ExternalMacroState::Defined,
            .compiler_definition = Some(rstd::move(version_definition)),
            .definition          = Some(object_macro("LITO_PKG_VERSION"_str,
                                                     frontend::lexical::TokenKind::StringLiteral,
                                                     literal.as_str())),
        });
        for (const auto& feature : metadata.features) {
            auto dependency_key = String::make("lito.package.feature:"_str);
            dependency_key.push_str(feature.name.as_str());
            auto identity_value = String::make(feature.name.as_str());
            identity_value.push_ascii('=');
            identity_value.push_str(feature.enabled ? "1"_str : "0"_str);
            auto compiler_definition = Option<String> {};
            auto definition          = Option<preprocessor::SharedMacroDefinition> {};
            if (feature.enabled) {
                auto argument = feature.macro_name.clone();
                argument.push_str("=1"_str);
                compiler_definition = Some(rstd::move(argument));
                definition          = Some(object_macro(
                    feature.macro_name.as_str(), frontend::lexical::TokenKind::PpNumber, "1"_str));
            }
            entries.push(PackageMacroEntry {
                .name           = feature.macro_name.clone(),
                .dependency_key = rstd::move(dependency_key),
                .value_identity =
                    value_identity("lito-package-feature-v1"_str, identity_value.as_str()),
                .state               = feature.enabled ? frontend::ExternalMacroState::Defined
                                                       : frontend::ExternalMacroState::Undefined,
                .compiler_definition = rstd::move(compiler_definition),
                .definition          = rstd::move(definition),
            });
        }
        rstd::slice_::sort_unstable_by(
            entries.as_mut_slice().as_mut_ref(),
            [](const PackageMacroEntry& left, const PackageMacroEntry& right) {
                return left.name < right.name.as_str();
            });
        auto schema = String::make("lito-package-macro-catalog-v1\n"_str);
        for (const auto& entry : entries) {
            schema.push_str(
                rstd::format("{}:{}\n", entry.name.len(), entry.name.as_str()).as_str());
        }
        return PackageMacroCatalog(rstd::move(entries), rstd::move(schema));
    }

    auto resolve(ref<str> name, frontend::lexical::SourceLocation location)
        -> preprocessor::Result<Option<preprocessor::ExternalMacroResolution>> {
        if (name != "LITO_PKG_VERSION"_str && ! name.starts_with("LITO_FEAT_"_str)) {
            return Ok(None());
        }
        for (const auto& entry : entries_) {
            if (entry.name.as_str() != name) continue;
            return Ok(Some(preprocessor::ExternalMacroResolution {
                .dependency_key      = entry.dependency_key.clone(),
                .value_identity      = entry.value_identity.clone(),
                .state               = entry.state,
                .compiler_definition = as<Clone>(entry.compiler_definition).clone(),
                .definition          = as<Clone>(entry.definition).clone(),
            }));
        }
        if (name.starts_with("LITO_FEAT_"_str)) {
            return Err(frontend::lexical::Error::at(
                rstd::format("unknown package feature macro '{}'", name), location));
        }
        return Ok(None());
    }

    auto schema_identity() const noexcept -> ref<str> { return schema_identity_.as_str(); }

    auto validates(const frontend::ExternalMacroMaterialization& value) const noexcept -> bool {
        for (const auto& entry : entries_) {
            if (entry.name.as_str() != value.name.as_str()) continue;
            if (entry.dependency_key.as_str() != value.dependency_key.as_str() ||
                entry.value_identity.as_str() != value.value_identity.as_str() ||
                entry.state != value.state ||
                entry.compiler_definition.is_some() != value.compiler_definition.is_some()) {
                return false;
            }
            if (entry.compiler_definition.is_some() &&
                entry.compiler_definition->as_str() != value.compiler_definition->as_str()) {
                return false;
            }
            return true;
        }
        return false;
    }

private:
    PackageMacroCatalog(Vec<PackageMacroEntry> entries, String schema_identity)
        : entries_(rstd::move(entries)), schema_identity_(rstd::move(schema_identity)) {}

    static auto string_literal(ref<str> value) -> String {
        auto result = String::make();
        result.push_ascii('"');
        for (auto byte : value.as_bytes()) {
            if (byte == u8('\\') || byte == u8('"')) result.push_ascii('\\');
            result.push_ascii(byte);
        }
        result.push_ascii('"');
        return result;
    }

    static auto value_identity(ref<str> recipe, ref<str> value) -> String {
        return rstd::format("{}\n{}:{}", recipe, value.len(), value);
    }

    static auto object_macro(ref<str> name, frontend::lexical::TokenKind kind, ref<str> replacement)
        -> preprocessor::SharedMacroDefinition {
        auto token  = frontend::lexical::Token {};
        token.kind  = kind;
        token.text  = String::make(replacement);
        auto tokens = Vec<frontend::lexical::Token>::make();
        tokens.push(rstd::move(token));
        auto macro = preprocessor::MacroDefinition {};
        macro.set_name(String::make(name));
        macro.set_replacement(rstd::move(tokens));
        return preprocessor::share_macro_definition(rstd::move(macro));
    }

    Vec<PackageMacroEntry> entries_;
    String                 schema_identity_;
};

using SharedPackageMacroCatalog = rstd::sync::Arc<PackageMacroCatalog>;

struct PreparedScanInput {
    SharedPreprocessorEnvironment environment;
    SharedPackageMacroCatalog     external_macros;
    PreprocessorLanguage          language { PreprocessorLanguage::Cpp };
};

} // namespace lito::toolchain
