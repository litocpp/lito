export module lito.executable:builtin_packages;

import rstd;
import lito.core;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace
{

struct PackageInput {
    ref<str>                             id;
    lito::registry::EmbeddedPackageInput input;
};

template<rstd::size_t ArchiveSize, rstd::size_t DescriptorSize>
auto package_input(ref<str> id,
                   const unsigned char (&archive)[ArchiveSize],
                   const unsigned char (&descriptor)[DescriptorSize]) -> PackageInput {
    return PackageInput {
        .id = id,
        .input =
            lito::registry::EmbeddedPackageInput {
                .archive    = slice<u8>::from_raw_parts(reinterpret_cast<const byte*>(archive),
                                                        usize(ArchiveSize)),
                .descriptor = slice<u8>::from_raw_parts(reinterpret_cast<const byte*>(descriptor),
                                                        usize(DescriptorSize)),
            },
    };
}

#include "builtin/packages.inc"

auto resolve_package(void*, ref<str> id) noexcept -> Option<lito::registry::EmbeddedPackageInput> {
    for (const auto& package : PACKAGES) {
        if (package.id == id) {
            return Some(lito::registry::EmbeddedPackageInput {
                .archive    = package.input.archive,
                .descriptor = package.input.descriptor,
            });
        }
    }
    return None();
}

} // namespace

export auto lito_embedded_packages() noexcept -> lito::registry::EmbeddedPackageProvider {
    return lito::registry::EmbeddedPackageProvider { .resolve = resolve_package };
}
