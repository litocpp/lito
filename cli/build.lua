local target = lito.target({
  kind = "bin",
  name = "lito",
})
local pack = lito.host_tool(target, "lito-pack", "lito-package-embed")

local function embed(id, source_name)
  local source = lito.external_source(target, source_name)
  return lito.run({
    tool = pack,
    cwd = ".",
    input_roots = { source },
    args = {
      "--root", "@INPUT_ROOT:1@",
      "--registry", "https://registry.litocpp.org/",
      "--archive", "@OUTPUT:1@",
      "--descriptor", "@OUTPUT:2@",
      "--depfile", "@OUTPUT:3@",
    },
    inputs = { source.file("lito.toml") },
    outputs = {
      "builtin/" .. id .. ".tar.zst",
      "builtin/" .. id .. ".json",
      "builtin/" .. id .. ".d",
    },
    depfile = { output = 3 },
  })
end

local pmacro = embed("pmacro", "builtin-pmacro")
local qt = embed("qt", "builtin-qt")
local libxml2 = embed("libxml2", "builtin-libxml2")

local inputs = {
  pmacro.outputs[1], pmacro.outputs[2],
  qt.outputs[1], qt.outputs[2],
  libxml2.outputs[1], libxml2.outputs[2],
}
local generated = lito.write({
  output = "builtin/packages.cpp",
  inputs = inputs,
  content = [[module lito.executable;

import rstd;
import lito.core;
import :builtin_packages;

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

static constexpr unsigned char LITO_BUILTIN_PMACRO_ARCHIVE[] = {
#embed "@INPUT:1@"
};
static constexpr unsigned char LITO_BUILTIN_PMACRO_DESCRIPTOR[] = {
#embed "@INPUT:2@"
};
static constexpr unsigned char LITO_BUILTIN_QT_ARCHIVE[] = {
#embed "@INPUT:3@"
};
static constexpr unsigned char LITO_BUILTIN_QT_DESCRIPTOR[] = {
#embed "@INPUT:4@"
};
static constexpr unsigned char LITO_BUILTIN_LIBXML2_ARCHIVE[] = {
#embed "@INPUT:5@"
};
static constexpr unsigned char LITO_BUILTIN_LIBXML2_DESCRIPTOR[] = {
#embed "@INPUT:6@"
};

const PackageInput PACKAGES[] = {
    package_input("pmacro"_str, LITO_BUILTIN_PMACRO_ARCHIVE, LITO_BUILTIN_PMACRO_DESCRIPTOR),
    package_input("qt"_str, LITO_BUILTIN_QT_ARCHIVE, LITO_BUILTIN_QT_DESCRIPTOR),
    package_input("libxml2"_str, LITO_BUILTIN_LIBXML2_ARCHIVE, LITO_BUILTIN_LIBXML2_DESCRIPTOR),
};

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

auto lito_embedded_packages() noexcept -> lito::registry::EmbeddedPackageProvider {
    return lito::registry::EmbeddedPackageProvider { .resolve = resolve_package };
}
]],
})

lito.target_add_generated_source(target, generated.output)
