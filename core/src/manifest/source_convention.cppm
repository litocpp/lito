export module lito.core:manifest.source_convention;

import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::manifest
{

auto c_manifest_source(ref<rstd::path::Path> path) noexcept -> bool {
    auto extension = path.extension();
    if (extension.is_none()) return false;
    auto text = (*extension).to_str();
    if (text.is_none()) return false;
    return *text == "c"_str;
}

auto cpp_manifest_source(ref<rstd::path::Path> path) noexcept -> bool {
    auto extension = path.extension();
    if (extension.is_none()) return false;
    auto text = (*extension).to_str();
    if (text.is_none()) return false;
    return *text == "cppm"_str || *text == "cpp"_str || *text == "cc"_str || *text == "cxx"_str;
}

auto supported_manifest_source(ref<rstd::path::Path> path) noexcept -> bool {
    return c_manifest_source(path) || cpp_manifest_source(path);
}

auto runnable_manifest_source(ref<rstd::path::Path> path) noexcept -> bool {
    auto extension = path.extension();
    if (extension.is_none()) return false;
    auto text = (*extension).to_str();
    if (text.is_none()) return false;
    return *text == "cpp"_str || *text == "cc"_str || *text == "cxx"_str;
}

} // namespace lito::manifest
