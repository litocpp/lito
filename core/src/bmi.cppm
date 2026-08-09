export module lito.bmi;

import rstd;
import lito.cpp;

using namespace rstd::prelude;
using namespace rstd::literals;

using PathBuf = rstd::path::PathBuf;

export namespace lito
{

inline constexpr auto BMI_CONTRACT_VERSION = "lito-bmi-v1"_str;

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

struct CppToolchainCapabilities {
    bool one_phase_bmi { false };
    bool exact_module_mapping { false };
    bool reduced_bmi { false };
    bool full_bmi_precompile { false };
    bool reduced_bmi_precompile { false };
    bool source_embedding { false };
};

struct BmiFormatIdentity : DefaultInClass<BmiFormatIdentity, rstd::clone::Clone> {
    String         family;
    String         compiler_build;
    String         target;
    String         resource_environment;
    Option<String> format_revision;

    auto clone() const -> BmiFormatIdentity;
};

enum class BmiCompatibilityField
{
    Format,
    LanguageStandard,
    StandardLibrary,
    Exceptions,
    Rtti,
    LanguageModes,
    AbiModes,
    Target,
    Sysroot,
    TargetFeatures,
    PublicPreprocessorRequirements,
    VendorSemantics,
};

struct BmiCompatibilityDifference {
    BmiCompatibilityField field { BmiCompatibilityField::Format };
    String                provider;
    String                consumer;
};

struct BmiCompatibilityResult {
    Vec<BmiCompatibilityDifference> differences;

    auto compatible() const noexcept -> bool { return differences.is_empty(); }
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

auto bmi_compatibility_field_name(BmiCompatibilityField value) noexcept -> ref<str> {
    switch (value) {
    case BmiCompatibilityField::Format: return "compiler/BMI format"_str;
    case BmiCompatibilityField::LanguageStandard: return "C++ language standard"_str;
    case BmiCompatibilityField::StandardLibrary: return "standard library"_str;
    case BmiCompatibilityField::Exceptions: return "exceptions"_str;
    case BmiCompatibilityField::Rtti: return "RTTI"_str;
    case BmiCompatibilityField::LanguageModes: return "C++ language modes"_str;
    case BmiCompatibilityField::AbiModes: return "C++ ABI modes"_str;
    case BmiCompatibilityField::Target: return "target"_str;
    case BmiCompatibilityField::Sysroot: return "sysroot"_str;
    case BmiCompatibilityField::TargetFeatures: return "target features"_str;
    case BmiCompatibilityField::PublicPreprocessorRequirements:
        return "public preprocessor requirements"_str;
    case BmiCompatibilityField::VendorSemantics: return "vendor semantic options"_str;
    }
    return "BMI compatibility"_str;
}

auto bmi_format_identity(const BmiFormatIdentity& format) -> String;

auto bmi_format_key(const BmiFormatIdentity& format) -> String;

auto check_bmi_compatibility(const BmiFormatIdentity&     provider_format,
                             const CppCompileOptions&     provider,
                             const CppPublicRequirements& provider_public_requirements,
                             const BmiFormatIdentity&     consumer_format,
                             const CppCompileOptions&     consumer) -> BmiCompatibilityResult;

auto make_bmi_artifact_key(const BmiRecipe& recipe) -> BmiArtifactKey;

} // namespace lito

namespace lito
{

inline constexpr uint64_t BMI_FNV_OFFSET = 14695981039346656037ull;
inline constexpr uint64_t BMI_FNV_PRIME  = 1099511628211ull;

auto hash_text(uint64_t& hash, ref<str> value) -> void {
    for (auto byte : value) {
        hash ^= byte.to_primitive();
        hash *= BMI_FNV_PRIME;
    }
    hash ^= 0;
    hash *= BMI_FNV_PRIME;
}

auto hash_hex(uint64_t value) -> String {
    static constexpr char digits[] = "0123456789abcdef";
    char                  result[16];
    for (size_t index = 0; index < 16; ++index) {
        result[15 - index] = digits[value & 0xfu];
        value >>= 4u;
    }
    return String::make(
        ref<str>::from_raw_parts_unchecked(reinterpret_cast<const byte*>(result), usize(16)));
}

auto digest(ref<str> recipe, ref<str> value) -> String {
    auto hash = BMI_FNV_OFFSET;
    hash_text(hash, recipe);
    hash_text(hash, value);
    return hash_hex(hash);
}

auto append_value(String& output, ref<str> key, ref<str> value) -> void {
    output.push_str(key);
    output.push_ascii('=');
    output.push_str(rstd::format("{}:{}\n", value.len(), value).as_str());
}

auto bool_text(bool value) -> ref<str> {
    return value ? "true"_str : "false"_str;
}

auto standard_library_text(StandardLibrary value) -> ref<str> {
    return value == StandardLibrary::Libstdcxx ? "libstdc++"_str : "libc++"_str;
}

auto family_identity(ref<str> prefix, const Vec<CppFamilyOption>& options) -> String {
    auto result = String::make();
    for (const auto& option : options) {
        append_value(result,
                     rstd::format("{}:{}", prefix, option.family.as_str()).as_str(),
                     option.value.as_str());
    }
    return result;
}

auto vendor_semantic_identity(const Vec<CppVendorOption>& options) -> String {
    auto result = String::make();
    for (const auto& option : options) {
        auto relevant = option.effect == CppVendorOptionEffect::Preprocessor ||
                        option.effect == CppVendorOptionEffect::Language ||
                        option.effect == CppVendorOptionEffect::Abi ||
                        option.effect == CppVendorOptionEffect::Unknown;
        if (! relevant) continue;
        append_value(result, "vendor"_str, option.value.as_str());
    }
    return result;
}

auto option_text(const Option<String>& value) -> String {
    return value.is_some() ? value->clone() : String::make("<default>"_str);
}

auto add_difference(BmiCompatibilityResult& result,
                    BmiCompatibilityField   field,
                    ref<str>                provider,
                    ref<str>                consumer) -> void {
    if (provider == consumer) return;
    result.differences.push(BmiCompatibilityDifference {
        .field    = field,
        .provider = String::make(provider),
        .consumer = String::make(consumer),
    });
}

} // namespace lito

export namespace lito
{

auto BmiFormatIdentity::clone() const -> BmiFormatIdentity {
    const auto& value  = *this;
    auto        result = BmiFormatIdentity {
        .family               = value.family.clone(),
        .compiler_build       = value.compiler_build.clone(),
        .target               = value.target.clone(),
        .resource_environment = value.resource_environment.clone(),
    };
    if (value.format_revision.is_some()) {
        result.format_revision = Some(value.format_revision->clone());
    }
    return result;
}

auto bmi_format_identity(const BmiFormatIdentity& format) -> String {
    auto result = String::make("lito-bmi-format-v1\n"_str);
    append_value(result, "family"_str, format.family.as_str());
    append_value(result, "compiler-build"_str, format.compiler_build.as_str());
    append_value(result, "target"_str, format.target.as_str());
    append_value(result, "resource-environment"_str, format.resource_environment.as_str());
    append_value(result,
                 "format-revision"_str,
                 format.format_revision.is_some() ? format.format_revision->as_str()
                                                  : "<unreported>"_str);
    return result;
}

auto bmi_format_key(const BmiFormatIdentity& format) -> String {
    auto identity = bmi_format_identity(format);
    return digest("lito-bmi-format-key-v1"_str, identity.as_str());
}

auto check_bmi_compatibility(const BmiFormatIdentity&     provider_format,
                             const CppCompileOptions&     provider,
                             const CppPublicRequirements& provider_public_requirements,
                             const BmiFormatIdentity&     consumer_format,
                             const CppCompileOptions&     consumer) -> BmiCompatibilityResult {
    auto result                   = BmiCompatibilityResult {};
    auto provider_format_identity = bmi_format_identity(provider_format);
    auto consumer_format_identity = bmi_format_identity(consumer_format);
    add_difference(result,
                   BmiCompatibilityField::Format,
                   provider_format_identity.as_str(),
                   consumer_format_identity.as_str());
    add_difference(result,
                   BmiCompatibilityField::LanguageStandard,
                   provider.language.standard.as_str(),
                   consumer.language.standard.as_str());
    add_difference(result,
                   BmiCompatibilityField::StandardLibrary,
                   standard_library_text(provider.abi.standard_library),
                   standard_library_text(consumer.abi.standard_library));
    add_difference(result,
                   BmiCompatibilityField::Exceptions,
                   bool_text(provider.language.exceptions),
                   bool_text(consumer.language.exceptions));
    add_difference(result,
                   BmiCompatibilityField::Rtti,
                   bool_text(provider.language.rtti),
                   bool_text(consumer.language.rtti));
    auto provider_language = family_identity("language"_str, provider.language.modes);
    auto consumer_language = family_identity("language"_str, consumer.language.modes);
    add_difference(result,
                   BmiCompatibilityField::LanguageModes,
                   provider_language.as_str(),
                   consumer_language.as_str());
    auto provider_abi = family_identity("abi"_str, provider.abi.modes);
    auto consumer_abi = family_identity("abi"_str, consumer.abi.modes);
    add_difference(
        result, BmiCompatibilityField::AbiModes, provider_abi.as_str(), consumer_abi.as_str());
    auto provider_target = option_text(provider.target.target);
    auto consumer_target = option_text(consumer.target.target);
    if (provider.target.target.is_none()) provider_target = provider_format.target.clone();
    if (consumer.target.target.is_none()) consumer_target = consumer_format.target.clone();
    add_difference(
        result, BmiCompatibilityField::Target, provider_target.as_str(), consumer_target.as_str());
    auto provider_sysroot = option_text(provider.target.sysroot);
    auto consumer_sysroot = option_text(consumer.target.sysroot);
    add_difference(result,
                   BmiCompatibilityField::Sysroot,
                   provider_sysroot.as_str(),
                   consumer_sysroot.as_str());
    auto provider_features = family_identity("target"_str, provider.target.features);
    auto consumer_features = family_identity("target"_str, consumer.target.features);
    add_difference(result,
                   BmiCompatibilityField::TargetFeatures,
                   provider_features.as_str(),
                   consumer_features.as_str());
    if (! cpp_public_requirements_satisfied(provider_public_requirements, consumer)) {
        auto required  = cpp_public_requirements_identity(provider_public_requirements);
        auto provided  = cpp_public_requirements(consumer);
        auto available = cpp_public_requirements_identity(provided);
        add_difference(result,
                       BmiCompatibilityField::PublicPreprocessorRequirements,
                       required.as_str(),
                       available.as_str());
    }
    auto provider_vendor = vendor_semantic_identity(provider.vendor);
    auto consumer_vendor = vendor_semantic_identity(consumer.vendor);
    add_difference(result,
                   BmiCompatibilityField::VendorSemantics,
                   provider_vendor.as_str(),
                   consumer_vendor.as_str());
    return result;
}

auto make_bmi_artifact_key(const BmiRecipe& recipe) -> BmiArtifactKey {
    auto identity = String::make(BMI_CONTRACT_VERSION);
    identity.push_ascii('\n');
    append_value(identity, "module"_str, recipe.logical_name.as_str());
    append_value(identity, "provider"_str, recipe.provider_identity.as_str());
    append_value(identity, "source"_str, recipe.source_identity.as_str());
    append_value(identity, "source-content"_str, recipe.source_content_identity.as_str());
    append_value(
        identity, "representation"_str, bmi_representation_name(recipe.request.representation));
    append_value(identity,
                 "source-embedding"_str,
                 bmi_source_embedding_name(recipe.request.source_embedding));
    append_value(identity, "cpp-context"_str, recipe.cpp_context_identity.as_str());
    append_value(identity, "public-requirements"_str, recipe.public_requirements_identity.as_str());
    append_value(identity, "format"_str, recipe.format_identity.as_str());
    auto dependencies = rstd::collections::BTreeMap<String, String>::make();
    for (const auto& dependency : recipe.direct_dependencies) {
        dependencies.insert(dependency.logical_name.clone(), dependency.artifact_key.clone());
    }
    auto values = dependencies.into_iter();
    while (auto value = values.next()) {
        auto entry = rstd::move(value).unwrap();
        append_value(identity,
                     rstd::format("dependency:{}", entry.template get<0>().as_str()).as_str(),
                     entry.template get<1>().as_str());
    }
    return BmiArtifactKey {
        .value = digest("lito-bmi-artifact-key-v1"_str, identity.as_str()),
    };
}

} // namespace lito
