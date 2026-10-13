export module lito.core:manifest.name;
import rstd;
using namespace rstd::prelude;

export namespace lito::manifest
{
auto package_name_is_valid(ref<str> value) -> bool {
    if (value.is_empty()) return false;
    for (auto byte : value) {
        const bool alpha =
            (byte >= u8('a') && byte <= u8('z')) || (byte >= u8('A') && byte <= u8('Z'));
        const bool digit = byte >= u8('0') && byte <= u8('9');
        if (! alpha && ! digit && byte != u8('-') && byte != u8('_')) return false;
    }
    return true;
}
} // namespace lito::manifest
