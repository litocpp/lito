module;
#include <rstd/enum.hpp>
#include <rstd/macro.hpp>

export module lito.core:registry.canonical;

import rstd;
import rstd.json;
import :registry.error;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::registry
{

auto canonical_signed_json(const rstd::json::Value& value) -> RegistryValueResult<String>;

} // namespace lito::registry

namespace
{

struct JcsObjectKey {
    ref<String> key;
    Vec<u16>    utf16;
};

auto utf16_units(ref<str> text) -> Vec<u16> {
    auto result = Vec<u16>::make();
    auto chars  = text.chars();
    for (auto code_point = chars.next(); code_point.is_some(); code_point = chars.next()) {
        auto scalar = *code_point;
        if (scalar <= u32(0xffff)) {
            result.push(as_cast<u16>(scalar));
            continue;
        }
        scalar -= u32(0x10000);
        result.push(u16(0xd800 + (scalar.to_primitive() >> 10u)));
        result.push(u16(0xdc00 + (scalar.to_primitive() & 0x3ffu)));
    }
    return result;
}

auto utf16_less(const Vec<u16>& left, const Vec<u16>& right) -> bool {
    auto count = left.len() < right.len() ? left.len() : right.len();
    for (usize index {}; index < count; ++index) {
        if (left[index] < right[index]) return true;
        if (right[index] < left[index]) return false;
    }
    return left.len() < right.len();
}

auto append_json_string(String& output, ref<str> value) -> void {
    auto encoded = rstd::json::to_string(rstd::json::Value::String(String::make(value)));
    output.push_str(encoded.as_str());
}

auto append_canonical_json(String& output, const rstd::json::Value& value)
    -> lito::registry::RegistryValueResult<empty> {
    RSTD_MATCH(value) {
        RSTD_CASE(Null) {
            output.push_str("null"_str);
            return Ok(empty {});
        }
        RSTD_CASE(Bool, boolean) {
            output.push_str(boolean ? "true"_str : "false"_str);
            return Ok(empty {});
        }
        RSTD_CASE(Number, number) {
            (void)number;
            return lito::registry::registry_value_failure<empty>(
                "Registry signed JSON represents protocol integers as canonical decimal strings"_str);
        }
        RSTD_CASE(String, string) {
            append_json_string(output, string.as_str());
            return Ok(empty {});
        }
        RSTD_CASE(Array, array) {
            output.push_ascii('[');
            for (usize index {}; index < array.len(); ++index) {
                if (index != usize {}) output.push_ascii(',');
                rstd_try(append_canonical_json(output, array[index]));
            }
            output.push_ascii(']');
            return Ok(empty {});
        }
        RSTD_CASE(Object, object) {
            auto keys = Vec<JcsObjectKey>::with_capacity(object.len());
            auto iter = object.iter();
            for (auto item = iter.next(); item.is_some(); item = iter.next()) {
                auto key = (*item).template get<0>();
                keys.push(JcsObjectKey {
                    .key   = key,
                    .utf16 = utf16_units(key->as_str()),
                });
            }
            rstd::slice_::sort_unstable_by(keys.as_mut_slice().as_mut_ref(),
                                           [](const JcsObjectKey& left, const JcsObjectKey& right) {
                                               return utf16_less(left.utf16, right.utf16);
                                           });
            output.push_ascii('{');
            for (usize index {}; index < keys.len(); ++index) {
                if (index != usize {}) output.push_ascii(',');
                append_json_string(output, keys[index].key->as_str());
                output.push_ascii(':');
                auto member = object.get(keys[index].key->as_str()).unwrap();
                rstd_try(append_canonical_json(output, *member));
            }
            output.push_ascii('}');
            return Ok(empty {});
        }
    }
    rstd::unreachable();
}

} // namespace

auto lito::registry::canonical_signed_json(const rstd::json::Value& value)
    -> RegistryValueResult<String> {
    auto result = String::make();
    rstd_try(append_canonical_json(result, value));
    return Ok(rstd::move(result));
}
