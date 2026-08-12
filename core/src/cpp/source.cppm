export module lito.cpp.source;

import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito
{

auto supported_cpp_source(ref<rstd::path::Path> path) noexcept -> bool {
    auto extension = path.extension();
    if (extension.is_none()) return false;
    auto text = (*extension).to_str();
    if (text.is_none()) return false;
    return *text == "cppm"_str || *text == "cpp"_str || *text == "cc"_str || *text == "cxx"_str;
}

auto runnable_cpp_source(ref<rstd::path::Path> path) noexcept -> bool {
    auto extension = path.extension();
    if (extension.is_none()) return false;
    auto text = (*extension).to_str();
    if (text.is_none()) return false;
    return *text == "cpp"_str || *text == "cc"_str || *text == "cxx"_str;
}

} // namespace lito
