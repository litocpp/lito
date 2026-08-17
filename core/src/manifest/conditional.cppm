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
    String                     source;
    lito::condition::Expression condition;
    ConditionalUsage           usage;
};

struct FeatureDeclaration {
    String         name;
    bool           default_enabled { false };
    Option<String> macro;
};

auto normalized_feature_macro(ref<str> package, ref<str> feature) -> String {
    auto result = String::make();
    const auto append = [&](ref<str> value) -> void {
        for (auto byte : value.as_bytes()) {
            if (byte == u8('-')) byte = u8('_');
            if (byte >= u8('a') && byte <= u8('z')) {
                byte = u8(byte.to_primitive() - u8('a').to_primitive() + u8('A').to_primitive());
            }
            result.push_ascii(byte);
        }
    };
    append(package);
    result.push_str("_FEATURE_"_str);
    append(feature);
    return result;
}

} // namespace lito::manifest
