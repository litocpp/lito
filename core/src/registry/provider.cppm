export module lito.core:registry.provider;

import rstd;

using namespace rstd::prelude;

export namespace lito::registry
{

struct EmbeddedPackageInput {
    slice<u8> archive;
    slice<u8> descriptor;
};

struct EmbeddedPackageProvider {
    void* context {};
    Option<EmbeddedPackageInput> (*resolve)(void*, ref<str>) noexcept {};
};

} // namespace lito::registry
