export module lito.core:config.toolchain;

import rstd;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;
using namespace rstd::literals;

export namespace lito::config
{

enum class ToolchainEnvironmentVariable
{
    CFlags,
    CxxFlags,
    LdFlags,
};

constexpr auto toolchain_environment_variable_name(ToolchainEnvironmentVariable value) noexcept
    -> ref<str> {
    switch (value) {
    case ToolchainEnvironmentVariable::CFlags: return "CFLAGS"_str;
    case ToolchainEnvironmentVariable::CxxFlags: return "CXXFLAGS"_str;
    case ToolchainEnvironmentVariable::LdFlags: return "LDFLAGS"_str;
    }
    return ""_str;
}

enum class StandardLibrary
{
    Libstdcxx,
    Libcxx,
};

constexpr auto standard_library_name(StandardLibrary value) noexcept -> ref<str> {
    return value == StandardLibrary::Libcxx ? "libc++"_str : "libstdc++"_str;
}

auto parse_standard_library(ref<str> value) noexcept -> Option<StandardLibrary> {
    if (value == standard_library_name(StandardLibrary::Libcxx)) {
        return Some(StandardLibrary::Libcxx);
    }
    if (value == standard_library_name(StandardLibrary::Libstdcxx)) {
        return Some(StandardLibrary::Libstdcxx);
    }
    return None();
}

auto standard_library_names() -> Vec<String> {
    auto values = Vec<String>::with_capacity(usize(2));
    values.push(String::make(standard_library_name(StandardLibrary::Libcxx)));
    values.push(String::make(standard_library_name(StandardLibrary::Libstdcxx)));
    return values;
}

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

} // namespace lito::config
