module lito.executable;

import rstd;
import lito.core;
import liresto;
import :builtin_packages;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace lito::builtin
{
auto pmacro_resources() -> liresto::ResourceBundle;
auto safety_resources() -> liresto::ResourceBundle;
auto libxml2_resources() -> liresto::ResourceBundle;
} // namespace lito::builtin

namespace
{

struct PackageResources {
    ref<str>                id;
    liresto::ResourceBundle resources;
};

auto resolve_package(void*, ref<str> id) noexcept -> Option<lito::registry::EmbeddedPackageInput> {
    static const PackageResources packages[] = {
        { "pmacro"_str, lito::builtin::pmacro_resources() },
        { "safety"_str, lito::builtin::safety_resources() },
        { "libxml2"_str, lito::builtin::libxml2_resources() },
    };
    for (const auto& package : packages) {
        if (package.id != id) continue;
        auto archive    = package.resources.get("archive"_str);
        auto descriptor = package.resources.get("descriptor"_str);
        if (archive.is_err() || archive->is_none() || descriptor.is_err() || descriptor->is_none())
            return None();
        return Some(lito::registry::EmbeddedPackageInput {
            .archive    = (*archive)->contents(),
            .descriptor = (*descriptor)->contents(),
        });
    }
    return None();
}

} // namespace

auto lito_embedded_packages() noexcept -> lito::registry::EmbeddedPackageProvider {
    return lito::registry::EmbeddedPackageProvider { .resolve = resolve_package };
}
