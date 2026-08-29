export module lito.toolchain.common:target;

import rstd;
import lito.core;
import lito.system;
import :error;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito
{

enum class CompileTargetSource
{
    CompilerDefault,
    Config,
    Sdk,
};

constexpr auto compile_target_source_name(CompileTargetSource source) noexcept -> ref<str> {
    switch (source) {
    case CompileTargetSource::CompilerDefault: return "compiler-default"_str;
    case CompileTargetSource::Config: return "config"_str;
    case CompileTargetSource::Sdk: return "sdk"_str;
    }
    return ""_str;
}

struct CompileTarget {
    lito::system::TargetInfo      info;
    lito::config::StandardLibrary standard_library { lito::config::StandardLibrary::Libstdcxx };
    CompileTargetSource           source { CompileTargetSource::CompilerDefault };

    auto clone() const -> CompileTarget {
        return CompileTarget {
            .info             = info.clone(),
            .standard_library = standard_library,
            .source           = source,
        };
    }
};

auto configured_target_candidate(const lito::config::ToolchainTargetSelection& selection)
    -> ToolchainResult<String> {
    if (! selection.is_Config()) {
        return Err(ToolchainError::Message(
            String::make("configured target candidate requires a typed target selection"_str)));
    }
    const auto& configured = selection.as_Config();
    auto        candidate  = lito::system::encode_target_candidate(
        configured.os.as_str(),
        configured.architecture,
        configured.vendor.is_some() ? Some(configured.vendor->as_str()) : Option<ref<str>> {},
        configured.environment.is_some() ? Some(configured.environment->as_str())
                                         : Option<ref<str>> {});
    if (candidate.is_err()) {
        return Err(ToolchainError::Platform(rstd::move(candidate).unwrap_err()));
    }
    return Ok(rstd::move(candidate).unwrap());
}

auto resolve_standard_library_selection(lito::config::StandardLibrarySelection selection,
                                        const lito::system::TargetInfo&        target)
    -> ToolchainResult<lito::config::StandardLibrary> {
    auto explicit_family = lito::config::explicit_standard_library(selection);
    if (explicit_family.is_some()) return Ok(*explicit_family);
    using lito::system::TargetPlatform;
    switch (target.platform) {
    case TargetPlatform::Linux: return Ok(lito::config::StandardLibrary::Libstdcxx);
    case TargetPlatform::Windows:
        return Ok(target.is_msvc() ? lito::config::StandardLibrary::Msvc
                                   : lito::config::StandardLibrary::Libstdcxx);
    case TargetPlatform::Android:
    case TargetPlatform::Macos:
    case TargetPlatform::Freebsd:
    case TargetPlatform::Netbsd:
    case TargetPlatform::Openbsd: return Ok(lito::config::StandardLibrary::Libcxx);
    case TargetPlatform::Unknown: break;
    }
    return Err(ToolchainError::Message(rstd::format(
        "cannot automatically select a C++ standard library for target '{}'; configure "
        "toolchain.stdlib explicitly",
        target.triple.as_str())));
}

auto validate_standard_library(const lito::system::TargetInfo& target,
                               lito::config::StandardLibrary   standard_library)
    -> ToolchainResult<empty> {
    if (standard_library == lito::config::StandardLibrary::Msvc && ! target.is_msvc()) {
        return Err(ToolchainError::Message(
            rstd::format("standard library 'msvc' requires an MSVC target environment; target is "
                         "'{}'",
                         target.triple.as_str())));
    }
    if ((target.platform == lito::system::TargetPlatform::Android ||
         target.platform == lito::system::TargetPlatform::Macos) &&
        standard_library != lito::config::StandardLibrary::Libcxx) {
        return Err(ToolchainError::Message(rstd::format(
            "target platform '{}' requires standard library 'libc++'", target.platform_name())));
    }
    if (target.is_msvc() && standard_library == lito::config::StandardLibrary::Libstdcxx) {
        return Err(ToolchainError::Message(
            rstd::format("standard library 'libstdc++' is unsupported for MSVC target '{}'",
                         target.triple.as_str())));
    }
    return Ok(empty {});
}

auto resolve_compile_target(lito::system::TargetInfo      target,
                            lito::config::StandardLibrary standard_library,
                            CompileTargetSource source) -> ToolchainResult<CompileTarget> {
    auto valid = validate_standard_library(target, standard_library);
    if (valid.is_err()) return Err(rstd::move(valid).unwrap_err());
    return Ok(CompileTarget {
        .info             = rstd::move(target),
        .standard_library = standard_library,
        .source           = source,
    });
}

auto resolve_sdk_compile_target(const lito::system::TargetInfo& target,
                                lito::config::StandardLibrary   standard_library)
    -> ToolchainResult<CompileTarget> {
    return resolve_compile_target(target.clone(), standard_library, CompileTargetSource::Sdk);
}

} // namespace lito
