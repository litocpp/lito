module;
#include <rstd/macro.hpp>

export module lito.core:lock.wire;

import rstd;
import rstd.serde;
import rstd.toml;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::lock::wire
{

struct External {
    String              name;
    Option<Vec<String>> architectures;
    String              source;
    Option<String>      checksum;
};

struct Package {
    String         name;
    Option<String> version;
    Option<String> source;
    Option<String> checksum;
    Vec<String>    dependencies;
    Vec<String>    runtime_dependencies;
    Vec<External>  externals;
};

struct Document {
    u64          version {};
    Vec<Package> packages;
};

auto encode(const Document& document) -> String;

} // namespace lito::lock::wire

auto append_string(String& output, ref<str> value) -> void {
    auto encoded =
        rstd::toml::to_value_string(rstd::toml::Value::String(String::make(value))).unwrap();
    output.push_str(encoded.as_str());
}

auto append_string_field(String& output, ref<str> name, ref<str> value) -> void {
    output.push_str(name);
    output.push_str(" = "_str);
    append_string(output, value);
    output.push_ascii(u8('\n'));
}

auto append_strings_field(String& output, ref<str> name, const Vec<String>& values) -> void {
    output.push_str(name);
    output.push_str(" = [\n"_str);
    for (const auto& value : values) {
        output.push_str("  "_str);
        append_string(output, value.as_str());
        output.push_str(",\n"_str);
    }
    output.push_str("]\n"_str);
}

auto append_external(String& output, const lito::lock::wire::External& external) -> void {
    output.push_str("\n[[packages.externals]]\n"_str);
    append_string_field(output, "name"_str, external.name.as_str());
    append_string_field(output, "source"_str, external.source.as_str());
    if (external.checksum.is_some()) {
        append_string_field(output, "checksum"_str, external.checksum->as_str());
    }
    if (external.architectures.is_some()) {
        append_strings_field(output, "architectures"_str, *external.architectures);
    }
}

auto append_package(String& output, const lito::lock::wire::Package& package) -> void {
    output.push_str("\n[[packages]]\n"_str);
    append_string_field(output, "name"_str, package.name.as_str());
    if (package.version.is_some()) {
        append_string_field(output, "version"_str, package.version->as_str());
    }
    if (package.source.is_some()) {
        append_string_field(output, "source"_str, package.source->as_str());
    }
    if (package.checksum.is_some()) {
        append_string_field(output, "checksum"_str, package.checksum->as_str());
    }
    if (! package.dependencies.is_empty()) {
        append_strings_field(output, "dependencies"_str, package.dependencies);
    }
    if (! package.runtime_dependencies.is_empty()) {
        append_strings_field(output, "runtime-dependencies"_str, package.runtime_dependencies);
    }
    for (const auto& external : package.externals) append_external(output, external);
}

auto lito::lock::wire::encode(const Document& document) -> String {
    auto output = rstd::format("version = {}\n", document.version);
    for (const auto& package : document.packages) append_package(output, package);
    return output;
}

export namespace rstd
{

template<>
struct Impl<serde::Deserialize, lito::lock::wire::External> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<lito::lock::wire::External, typename Deserializer::error_type> {
        auto name          = serde::RequiredField<String>("name"_str);
        auto architectures = serde::OptionalField<Vec<String>>("architectures"_str);
        auto source        = serde::RequiredField<String>("source"_str);
        auto checksum      = serde::OptionalField<String>("checksum"_str);
        rstd_try(serde::deserialize_record(deserializer,
                                           serde::UnknownFieldPolicy::Reject,
                                           name,
                                           architectures,
                                           source,
                                           checksum));
        return Ok(lito::lock::wire::External {
            .name          = rstd_try(name.take(deserializer)),
            .architectures = architectures.take(),
            .source        = rstd_try(source.take(deserializer)),
            .checksum      = checksum.take(),
        });
    }
};

template<>
struct Impl<serde::Deserialize, lito::lock::wire::Package> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<lito::lock::wire::Package, typename Deserializer::error_type> {
        auto checksum     = serde::OptionalField<String>("checksum"_str);
        auto dependencies = serde::OptionalField<Vec<String>>("dependencies"_str);
        auto externals    = serde::OptionalField<Vec<lito::lock::wire::External>>("externals"_str);
        auto name         = serde::RequiredField<String>("name"_str);
        auto runtime_dependencies = serde::OptionalField<Vec<String>>("runtime-dependencies"_str);
        auto source               = serde::OptionalField<String>("source"_str);
        auto version              = serde::OptionalField<String>("version"_str);
        rstd_try(serde::deserialize_record(deserializer,
                                           serde::UnknownFieldPolicy::Reject,
                                           checksum,
                                           dependencies,
                                           externals,
                                           name,
                                           runtime_dependencies,
                                           source,
                                           version));
        auto dependency_values = dependencies.take();
        auto external_values   = externals.take();
        auto runtime_values    = runtime_dependencies.take();
        return Ok(lito::lock::wire::Package {
            .name         = rstd_try(name.take(deserializer)),
            .version      = version.take(),
            .source       = source.take(),
            .checksum     = checksum.take(),
            .dependencies = dependency_values.is_some() ? rstd::move(dependency_values).unwrap()
                                                        : Vec<String>::make(),
            .runtime_dependencies = runtime_values.is_some() ? rstd::move(runtime_values).unwrap()
                                                             : Vec<String>::make(),
            .externals = external_values.is_some() ? rstd::move(external_values).unwrap()
                                                   : Vec<lito::lock::wire::External>::make(),
        });
    }
};

template<>
struct Impl<serde::Deserialize, lito::lock::wire::Document> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<lito::lock::wire::Document, typename Deserializer::error_type> {
        auto packages = serde::RequiredField<Vec<lito::lock::wire::Package>>("packages"_str);
        auto version  = serde::RequiredField<u64>("version"_str);
        rstd_try(serde::deserialize_record(
            deserializer, serde::UnknownFieldPolicy::Reject, packages, version));
        return Ok(lito::lock::wire::Document {
            .version  = rstd_try(version.take(deserializer)),
            .packages = rstd_try(packages.take(deserializer)),
        });
    }
};

} // namespace rstd
