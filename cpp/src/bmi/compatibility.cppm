export module lito.cpp:bmi.compatibility;

import rstd;
import lito.core;
import :compiler.option;
import :compiler.policy;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace lito::cpp
{

auto append_compatibility_value(String& output, ref<str> key, ref<str> value) -> void {
    output.push_str(key);
    output.push_ascii('=');
    output.push_str(rstd::format("{}:{}\n", value.len(), value).as_str());
}

auto bool_text(bool value) -> ref<str> {
    return value ? "true"_str : "false"_str;
}

auto standard_library_text(lito::config::StandardLibrary value) -> ref<str> {
    return value == lito::config::StandardLibrary::Libstdcxx ? "libstdc++"_str : "libc++"_str;
}

auto family_identity(ref<str> prefix, const Vec<CppFamilyOption>& options) -> String {
    auto result = String::make();
    for (const auto& option : options) {
        append_compatibility_value(result,
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
        append_compatibility_value(result, "vendor"_str, option.value.as_str());
    }
    return result;
}

auto option_text(const Option<String>& value) -> String {
    return value.is_some() ? value->clone() : String::make("<default>"_str);
}

} // namespace lito::cpp

export namespace lito::cpp
{

struct CppToolchainCapabilities {
    bool one_phase_bmi { false };
    bool exact_module_mapping { false };
    bool reduced_bmi { false };
    bool full_bmi_precompile { false };
    bool reduced_bmi_precompile { false };
    bool source_embedding { false };
};

struct BmiFormatIdentity : DefaultInClass<BmiFormatIdentity, Clone> {
    String         family;
    String         compiler_build;
    String         target;
    String         resource_environment;
    Option<String> format_revision;

    auto clone() const -> BmiFormatIdentity {
        auto result = BmiFormatIdentity {
            .family               = family.clone(),
            .compiler_build       = compiler_build.clone(),
            .target               = target.clone(),
            .resource_environment = resource_environment.clone(),
        };
        if (format_revision.is_some()) result.format_revision = Some(format_revision->clone());
        return result;
    }
};

enum class BmiCompatibilityField
{
    Format,
    LanguageStandard,
    StandardLibrary,
    StandardLibraryIdentity,
    StandardLibraryModes,
    Exceptions,
    Rtti,
    SizedDeallocation,
    Threading,
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

auto bmi_format_identity(const BmiFormatIdentity& format) -> String;

auto bmi_format_key(const BmiFormatIdentity& format) -> String;

auto check_bmi_compatibility(const BmiFormatIdentity&     provider_format,
                             const CppCompileOptions&     provider,
                             const CppPublicRequirements& provider_public_requirements,
                             const BmiFormatIdentity&     consumer_format,
                             const CppCompileOptions&     consumer) -> BmiCompatibilityResult {
    auto result                   = BmiCompatibilityResult {};
    auto provider_format_identity = bmi_format_identity(provider_format);
    auto consumer_format_identity = bmi_format_identity(consumer_format);
    auto add_difference =
        [&result](BmiCompatibilityField field, ref<str> provider_value, ref<str> consumer_value) {
            if (provider_value == consumer_value) return;
            result.differences.push(BmiCompatibilityDifference {
                .field    = field,
                .provider = String::make(provider_value),
                .consumer = String::make(consumer_value),
            });
        };
    add_difference(BmiCompatibilityField::Format,
                   provider_format_identity.as_str(),
                   consumer_format_identity.as_str());
    add_difference(BmiCompatibilityField::LanguageStandard,
                   provider.language.standard.as_str(),
                   consumer.language.standard.as_str());
    add_difference(BmiCompatibilityField::StandardLibrary,
                   standard_library_text(provider.abi.standard_library),
                   standard_library_text(consumer.abi.standard_library));
    auto provider_stdlib_identity =
        provider.abi.resolved_standard_library.is_some()
            ? provider.abi.resolved_standard_library->headers_identity.as_str()
            : "<unresolved>"_str;
    auto consumer_stdlib_identity =
        consumer.abi.resolved_standard_library.is_some()
            ? consumer.abi.resolved_standard_library->headers_identity.as_str()
            : "<unresolved>"_str;
    add_difference(BmiCompatibilityField::StandardLibraryIdentity,
                   provider_stdlib_identity,
                   consumer_stdlib_identity);
    auto provider_stdlib_modes = cpp_standard_library_modes_identity(provider);
    auto consumer_stdlib_modes = cpp_standard_library_modes_identity(consumer);
    add_difference(BmiCompatibilityField::StandardLibraryModes,
                   provider_stdlib_modes.as_str(),
                   consumer_stdlib_modes.as_str());
    add_difference(BmiCompatibilityField::Exceptions,
                   bool_text(provider.language.exceptions),
                   bool_text(consumer.language.exceptions));
    add_difference(BmiCompatibilityField::Rtti,
                   bool_text(provider.language.rtti),
                   bool_text(consumer.language.rtti));
    add_difference(BmiCompatibilityField::SizedDeallocation,
                   cpp_sized_deallocation_name(provider.language.sized_deallocation),
                   cpp_sized_deallocation_name(consumer.language.sized_deallocation));
    add_difference(BmiCompatibilityField::Threading,
                   bool_text(provider.threading.posix),
                   bool_text(consumer.threading.posix));
    auto provider_language = family_identity("language"_str, provider.language.modes);
    auto consumer_language = family_identity("language"_str, consumer.language.modes);
    add_difference(BmiCompatibilityField::LanguageModes,
                   provider_language.as_str(),
                   consumer_language.as_str());
    auto provider_abi = family_identity("abi"_str, provider.abi.modes);
    auto consumer_abi = family_identity("abi"_str, consumer.abi.modes);
    add_difference(BmiCompatibilityField::AbiModes, provider_abi.as_str(), consumer_abi.as_str());
    auto provider_target = option_text(provider.target.common.target);
    auto consumer_target = option_text(consumer.target.common.target);
    if (provider.target.common.target.is_none()) provider_target = provider_format.target.clone();
    if (consumer.target.common.target.is_none()) consumer_target = consumer_format.target.clone();
    add_difference(
        BmiCompatibilityField::Target, provider_target.as_str(), consumer_target.as_str());
    auto provider_sysroot = option_text(provider.target.common.sysroot);
    auto consumer_sysroot = option_text(consumer.target.common.sysroot);
    add_difference(
        BmiCompatibilityField::Sysroot, provider_sysroot.as_str(), consumer_sysroot.as_str());
    auto provider_features = family_identity("target"_str, provider.target.features);
    auto consumer_features = family_identity("target"_str, consumer.target.features);
    add_difference(BmiCompatibilityField::TargetFeatures,
                   provider_features.as_str(),
                   consumer_features.as_str());
    if (! cpp_public_requirements_satisfied(provider_public_requirements, consumer)) {
        auto required  = cpp_public_requirements_identity(provider_public_requirements);
        auto provided  = cpp_public_requirements(consumer);
        auto available = cpp_public_requirements_identity(provided);
        add_difference(BmiCompatibilityField::PublicPreprocessorRequirements,
                       required.as_str(),
                       available.as_str());
    }
    auto provider_vendor = vendor_semantic_identity(provider.vendor);
    auto consumer_vendor = vendor_semantic_identity(consumer.vendor);
    add_difference(
        BmiCompatibilityField::VendorSemantics, provider_vendor.as_str(), consumer_vendor.as_str());
    return result;
}

} // namespace lito::cpp

export namespace rstd
{

template<>
struct Impl<fmt::Display, lito::cpp::BmiCompatibilityField>
    : ImplBase<lito::cpp::BmiCompatibilityField> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        auto name = "BMI compatibility"_str;
        switch (this->self()) {
        case lito::cpp::BmiCompatibilityField::Format: name = "compiler/BMI format"_str; break;
        case lito::cpp::BmiCompatibilityField::LanguageStandard:
            name = "C++ language standard"_str;
            break;
        case lito::cpp::BmiCompatibilityField::StandardLibrary:
            name = "standard library"_str;
            break;
        case lito::cpp::BmiCompatibilityField::StandardLibraryIdentity:
            name = "resolved standard library headers"_str;
            break;
        case lito::cpp::BmiCompatibilityField::StandardLibraryModes:
            name = "standard library consistency modes"_str;
            break;
        case lito::cpp::BmiCompatibilityField::Exceptions: name = "exceptions"_str; break;
        case lito::cpp::BmiCompatibilityField::Rtti: name = "RTTI"_str; break;
        case lito::cpp::BmiCompatibilityField::SizedDeallocation:
            name = "sized deallocation"_str;
            break;
        case lito::cpp::BmiCompatibilityField::Threading: name = "threading model"_str; break;
        case lito::cpp::BmiCompatibilityField::LanguageModes:
            name = "C++ language modes"_str;
            break;
        case lito::cpp::BmiCompatibilityField::AbiModes: name = "C++ ABI modes"_str; break;
        case lito::cpp::BmiCompatibilityField::Target: name = "target"_str; break;
        case lito::cpp::BmiCompatibilityField::Sysroot: name = "sysroot"_str; break;
        case lito::cpp::BmiCompatibilityField::TargetFeatures: name = "target features"_str; break;
        case lito::cpp::BmiCompatibilityField::PublicPreprocessorRequirements:
            name = "public preprocessor requirements"_str;
            break;
        case lito::cpp::BmiCompatibilityField::VendorSemantics:
            name = "vendor semantic options"_str;
            break;
        }
        return formatter.write_str(name);
    }
};

} // namespace rstd
