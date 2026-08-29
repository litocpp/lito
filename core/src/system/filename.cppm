export module lito.system:filename;

import rstd;
import :platform;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::system
{

auto plugin_filename(ref<str> stem, const TargetInfo& target) -> String {
    auto result = String::make();
    if (target.family != TargetFamily::Windows) result.push_str("lib"_str);
    result.push_str(stem);
    if (target.family == TargetFamily::Windows) {
        result.push_str(".dll"_str);
    } else if (target.platform == TargetPlatform::Macos) {
        result.push_str(".dylib"_str);
    } else {
        result.push_str(".so"_str);
    }
    return result;
}

} // namespace lito::system
