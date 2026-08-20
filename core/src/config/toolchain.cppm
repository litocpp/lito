module;
#include <rstd/enum.hpp>

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
    Msvc,
};

constexpr auto standard_library_name(StandardLibrary value) noexcept -> ref<str> {
    switch (value) {
    case StandardLibrary::Libstdcxx: return "libstdc++"_str;
    case StandardLibrary::Libcxx: return "libc++"_str;
    case StandardLibrary::Msvc: return "msvc"_str;
    }
    return ""_str;
}

auto parse_standard_library(ref<str> value) noexcept -> Option<StandardLibrary> {
    if (value == standard_library_name(StandardLibrary::Libcxx)) {
        return Some(StandardLibrary::Libcxx);
    }
    if (value == standard_library_name(StandardLibrary::Libstdcxx)) {
        return Some(StandardLibrary::Libstdcxx);
    }
    if (value == standard_library_name(StandardLibrary::Msvc)) {
        return Some(StandardLibrary::Msvc);
    }
    return None();
}

auto standard_library_names() -> Vec<String> {
    auto values = Vec<String>::with_capacity(usize(3));
    values.push(String::make(standard_library_name(StandardLibrary::Libcxx)));
    values.push(String::make(standard_library_name(StandardLibrary::Libstdcxx)));
    values.push(String::make(standard_library_name(StandardLibrary::Msvc)));
    return values;
}

enum class StandardLibraryRuntime
{
    Dynamic,
    Static,
};

constexpr auto standard_library_runtime_name(StandardLibraryRuntime value) noexcept -> ref<str> {
    switch (value) {
    case StandardLibraryRuntime::Dynamic: return "dynamic"_str;
    case StandardLibraryRuntime::Static: return "static"_str;
    }
    return ""_str;
}

auto parse_standard_library_runtime(ref<str> value) noexcept -> Option<StandardLibraryRuntime> {
    if (value == standard_library_runtime_name(StandardLibraryRuntime::Dynamic)) {
        return Some(StandardLibraryRuntime::Dynamic);
    }
    if (value == standard_library_runtime_name(StandardLibraryRuntime::Static)) {
        return Some(StandardLibraryRuntime::Static);
    }
    return None();
}

enum class SdkKind
{
    Llvm,
    AndroidNdk,
};

constexpr auto sdk_kind_name(SdkKind value) noexcept -> ref<str> {
    switch (value) {
    case SdkKind::Llvm: return "llvm"_str;
    case SdkKind::AndroidNdk: return "android-ndk"_str;
    }
    return ""_str;
}

auto parse_sdk_kind(ref<str> value) noexcept -> Option<SdkKind> {
    if (value == sdk_kind_name(SdkKind::Llvm)) return Some(SdkKind::Llvm);
    if (value == sdk_kind_name(SdkKind::AndroidNdk)) return Some(SdkKind::AndroidNdk);
    return None();
}

class ToolchainSdkSelection : public DefaultInClass<ToolchainSdkSelection, Clone> {
    RSTD_ENUM(ToolchainSdkSelection,
              (Managed, (SdkKind kind; String version;)),
              (Directory, (SdkKind kind; PathBuf path;)))

public:
    auto clone() const -> ToolchainSdkSelection {
        RSTD_MATCH(*this) {
            RSTD_CASE(Managed, kind, version) {
                return Managed(kind, version.clone());
            }
            RSTD_CASE(Directory, kind, path) {
                return Directory(kind, path.clone());
            }
        }
        rstd::unreachable();
    }

    auto kind() const noexcept -> SdkKind {
        RSTD_MATCH(*this) {
            RSTD_CASE(Managed, kind, version) {
                static_cast<void>(version);
                return kind;
            }
            RSTD_CASE(Directory, kind, path) {
                static_cast<void>(path);
                return kind;
            }
        }
        rstd::unreachable();
    }
};

struct ToolchainSpec {
    PathBuf                       cc { PathBuf::from("clang"_str) };
    PathBuf                       cxx;
    PathBuf                       ld { PathBuf::from("ld.lld"_str) };
    PathBuf                       ar;
    Option<ToolchainSdkSelection> sdk;

    auto clone() const -> ToolchainSpec {
        return ToolchainSpec {
            .cc  = cc.clone(),
            .cxx = cxx.clone(),
            .ld  = ld.clone(),
            .ar  = ar.clone(),
            .sdk = as<Clone>(sdk).clone(),
        };
    }
};

struct ToolchainOverride {
    Option<PathBuf>               cc;
    Option<PathBuf>               cxx;
    Option<PathBuf>               ld;
    Option<PathBuf>               ar;
    Option<ToolchainSdkSelection> sdk;
};

auto apply_toolchain_override(ToolchainSpec specification, ToolchainOverride values)
    -> ToolchainSpec {
    if (values.cc.is_some()) specification.cc = rstd::move(values.cc).unwrap();
    if (values.cxx.is_some()) specification.cxx = rstd::move(values.cxx).unwrap();
    if (values.ld.is_some()) specification.ld = rstd::move(values.ld).unwrap();
    if (values.ar.is_some()) specification.ar = rstd::move(values.ar).unwrap();
    if (values.sdk.is_some()) specification.sdk = Some(rstd::move(values.sdk).unwrap());
    return specification;
}

} // namespace lito::config
