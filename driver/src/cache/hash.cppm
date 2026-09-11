module;

module lito.driver:cache.hash;

import rstd;
import lito.core;

using namespace rstd::prelude;

namespace lito::cache
{

auto text_identity(ref<str> recipe, ref<str> value) -> String {
    auto hash = lito::hash::Fnv1a64 {};
    hash.write_zero_terminated(recipe);
    hash.write_zero_terminated(value);
    return hash.hex64();
}

} // namespace lito::cache
