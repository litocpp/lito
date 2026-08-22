export module lito.cpp:bmi.artifact;

import rstd;
import :bmi.compatibility;

using namespace rstd::prelude;
using namespace rstd::literals;

using PathBuf = rstd::path::PathBuf;

namespace lito::cpp
{

inline constexpr uint64_t BMI_FNV_OFFSET = 14695981039346656037ull;
inline constexpr uint64_t BMI_FNV_PRIME  = 1099511628211ull;

auto hash_bmi_identity(uint64_t& hash, ref<str> value) -> void {
    for (auto byte : value) {
        hash ^= byte.to_primitive();
        hash *= BMI_FNV_PRIME;
    }
    hash ^= 0;
    hash *= BMI_FNV_PRIME;
}

auto bmi_identity_hex(uint64_t value) -> String {
    static constexpr char digits[] = "0123456789abcdef";
    char                  result[16];
    for (size_t index = 0; index < 16; ++index) {
        result[15 - index] = digits[value & 0xfu];
        value >>= 4u;
    }
    return String::make(
        ref<str>::from_raw_parts_unchecked(reinterpret_cast<const byte*>(result), usize(16)));
}

auto bmi_identity_digest(ref<str> recipe, ref<str> value) -> String {
    auto hash = BMI_FNV_OFFSET;
    hash_bmi_identity(hash, recipe);
    hash_bmi_identity(hash, value);
    return bmi_identity_hex(hash);
}

auto append_bmi_identity_value(String& output, ref<str> key, ref<str> value) -> void {
    output.push_str(key);
    output.push_ascii('=');
    output.push_str(rstd::format("{}:{}\n", value.len(), value).as_str());
}

} // namespace lito::cpp

export namespace lito::cpp
{

inline constexpr auto BMI_FORMAT_VERSION = "lito-bmi-v1"_str;

enum class BmiRepresentation
{
    Reduced,
    Full,
};

using BmiMode = BmiRepresentation;

enum class BmiSourceEmbeddingPolicy
{
    ExternalSources,
    EmbedAll,
};

enum class BmiUse
{
    Import,
    GenerateObject,
};

struct BmiRequest {
    BmiRepresentation        representation { BmiRepresentation::Reduced };
    BmiSourceEmbeddingPolicy source_embedding { BmiSourceEmbeddingPolicy::ExternalSources };
};

struct BmiRecipeDependency {
    String logical_name;
    String artifact_key;
};

struct BmiRecipe {
    BmiRequest               request;
    String                   logical_name;
    String                   provider_identity;
    String                   source_identity;
    String                   source_content_identity;
    String                   cpp_context_identity;
    String                   public_requirements_identity;
    String                   format_identity;
    Vec<BmiRecipeDependency> direct_dependencies;
};

struct BmiArtifactKey {
    String value;
};

struct ModuleArtifactDependency {
    String         logical_name;
    BmiArtifactKey artifact_key;
    PathBuf        path;
};

struct BmiArtifact {
    String                   logical_name;
    String                   provider_identity;
    BmiArtifactKey           key;
    BmiFormatIdentity        format;
    BmiRequest               request;
    PathBuf                  path;
    Option<String>           compiler_signature;
    Option<String>           content_digest;
    Vec<BmiRecipeDependency> direct_dependencies;
    Option<PathBuf>          paired_object;
};

auto bmi_representation_name(BmiRepresentation value) noexcept -> ref<str> {
    return value == BmiRepresentation::Reduced ? "reduced"_str : "full"_str;
}

auto bmi_source_embedding_name(BmiSourceEmbeddingPolicy value) noexcept -> ref<str> {
    return value == BmiSourceEmbeddingPolicy::EmbedAll ? "embed-all"_str : "external-sources"_str;
}

auto bmi_supports_use(BmiRepresentation representation, BmiUse use) noexcept -> bool {
    return use == BmiUse::Import || representation == BmiRepresentation::Full;
}

auto bmi_format_identity(const BmiFormatIdentity& format) -> String {
    auto result = String::make("lito-bmi-format-v1\n"_str);
    append_bmi_identity_value(result, "family"_str, format.family.as_str());
    append_bmi_identity_value(result, "compiler-build"_str, format.compiler_build.as_str());
    append_bmi_identity_value(result, "target"_str, format.target.as_str());
    append_bmi_identity_value(
        result, "resource-environment"_str, format.resource_environment.as_str());
    append_bmi_identity_value(result,
                              "format-revision"_str,
                              format.format_revision.is_some() ? format.format_revision->as_str()
                                                               : "<unreported>"_str);
    return result;
}

auto bmi_format_key(const BmiFormatIdentity& format) -> String {
    auto identity = bmi_format_identity(format);
    return bmi_identity_digest("lito-bmi-format-key-v1"_str, identity.as_str());
}

auto make_bmi_artifact_key(const BmiRecipe& recipe) -> BmiArtifactKey {
    auto identity = String::make(BMI_FORMAT_VERSION);
    identity.push_ascii('\n');
    append_bmi_identity_value(identity, "module"_str, recipe.logical_name.as_str());
    append_bmi_identity_value(identity, "provider"_str, recipe.provider_identity.as_str());
    append_bmi_identity_value(identity, "source"_str, recipe.source_identity.as_str());
    append_bmi_identity_value(
        identity, "source-content"_str, recipe.source_content_identity.as_str());
    append_bmi_identity_value(
        identity, "representation"_str, bmi_representation_name(recipe.request.representation));
    append_bmi_identity_value(identity,
                              "source-embedding"_str,
                              bmi_source_embedding_name(recipe.request.source_embedding));
    append_bmi_identity_value(identity, "cpp-context"_str, recipe.cpp_context_identity.as_str());
    append_bmi_identity_value(
        identity, "public-requirements"_str, recipe.public_requirements_identity.as_str());
    append_bmi_identity_value(identity, "format"_str, recipe.format_identity.as_str());
    auto dependencies = rstd::collections::BTreeMap<String, String>::make();
    for (const auto& dependency : recipe.direct_dependencies) {
        dependencies.insert(dependency.logical_name.clone(), dependency.artifact_key.clone());
    }
    for (auto entry : rstd::move(dependencies).into_iter()) {
        append_bmi_identity_value(
            identity,
            rstd::format("dependency:{}", entry.template get<0>().as_str()).as_str(),
            entry.template get<1>().as_str());
    }
    return BmiArtifactKey {
        .value = bmi_identity_digest("lito-bmi-artifact-key-v1"_str, identity.as_str()),
    };
}

} // namespace lito::cpp
