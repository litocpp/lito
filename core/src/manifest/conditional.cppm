export module lito.core:manifest.conditional;

import rstd;
import :condition;
import :dependency.usage;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::manifest
{

struct ConditionalUsage {
    lito::dependency::DeclaredUsageRequirements values;
    bool                                        declares_threads { false };
};

struct ConditionalConfiguration {
    String                      source;
    lito::condition::Expression condition;
    ConditionalUsage            usage;
};

struct DependencyFeature {
    String dependency;
    String feature;
};

struct FeatureDeclaration {
    String                 name;
    String                 macro_name;
    bool                   default_enabled { false };
    Vec<DependencyFeature> enables;
};

auto valid_feature_name(ref<str> value) -> bool {
    if (value.is_empty()) return false;
    const auto bytes = value.as_bytes();
    const auto first = bytes[usize {}];
    if (! ((first >= u8('a') && first <= u8('z')) || (first >= u8('A') && first <= u8('Z')) ||
           first == u8('_'))) {
        return false;
    }
    for (usize index { 1 }; index < bytes.len(); ++index) {
        const auto byte = bytes[index];
        if ((byte >= u8('a') && byte <= u8('z')) || (byte >= u8('A') && byte <= u8('Z')) ||
            (byte >= u8('0') && byte <= u8('9')) || byte == u8('-') || byte == u8('_')) {
            continue;
        }
        return false;
    }
    return true;
}

auto normalized_feature_macro(ref<str> feature) -> String {
    auto result = "LITO_FEAT_"_Str;
    for (auto byte : feature.as_bytes()) {
        if (byte == u8('-')) byte = u8('_');
        if (byte >= u8('a') && byte <= u8('z')) {
            byte = u8(byte.to_primitive() - u8('a').to_primitive() + u8('A').to_primitive());
        }
        result.push_ascii(byte);
    }
    return result;
}

} // namespace lito::manifest
