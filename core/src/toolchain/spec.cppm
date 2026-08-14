export module lito.toolchain.spec;

import rstd;
import lito.error;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito
{

struct ToolchainSpec {
    PathBuf cc { PathBuf::from("clang"_str) };
    PathBuf cxx;
    PathBuf ld { PathBuf::from("ld.lld"_str) };
    PathBuf ar;
    PathBuf strip;
    PathBuf format;

    auto clone() const -> ToolchainSpec {
        return ToolchainSpec {
            .cc     = cc.clone(),
            .cxx    = cxx.clone(),
            .ld     = ld.clone(),
            .ar     = ar.clone(),
            .strip  = strip.clone(),
            .format = format.clone(),
        };
    }
};

struct ToolchainOverride {
    Option<PathBuf> cc;
    Option<PathBuf> cxx;
    Option<PathBuf> ld;
    Option<PathBuf> ar;
    Option<PathBuf> strip;
    Option<PathBuf> format;
};

auto apply_toolchain_override(ToolchainSpec specification, ToolchainOverride values)
    -> ToolchainSpec {
    if (values.cc.is_some()) specification.cc = rstd::move(values.cc).unwrap();
    if (values.cxx.is_some()) specification.cxx = rstd::move(values.cxx).unwrap();
    if (values.ld.is_some()) specification.ld = rstd::move(values.ld).unwrap();
    if (values.ar.is_some()) specification.ar = rstd::move(values.ar).unwrap();
    if (values.strip.is_some()) specification.strip = rstd::move(values.strip).unwrap();
    if (values.format.is_some()) specification.format = rstd::move(values.format).unwrap();
    return specification;
}

} // namespace lito
