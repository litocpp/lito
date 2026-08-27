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

struct ToolchainTargetInput {
    lito::system::OperatingSystem os { lito::system::OperatingSystem::Linux };
    lito::system::Architecture    architecture { lito::system::Architecture::Unknown };
    CompileTargetSource           source { CompileTargetSource::CompilerDefault };

    auto clone() const -> ToolchainTargetInput {
        return ToolchainTargetInput {
            .os           = os,
            .architecture = architecture,
            .source       = source,
        };
    }
};

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

auto resolve_toolchain_target_input(const lito::config::ToolchainTargetSelection& selection,
                                    const lito::system::TargetInfo* compiler_default = nullptr)
    -> ToolchainResult<ToolchainTargetInput> {
    if (selection.is_Config()) {
        return Ok(ToolchainTargetInput {
            .os           = selection.as_Config().os,
            .architecture = selection.as_Config().architecture,
            .source       = CompileTargetSource::Config,
        });
    }
    if (compiler_default == nullptr) {
        return Err(
            ToolchainError::Message(String::make("compiler default target was not queried"_str)));
    }
    auto os = lito::system::target_operating_system(*compiler_default);
    if (os.is_err()) return Err(ToolchainError::Platform(rstd::move(os).unwrap_err()));
    return Ok(ToolchainTargetInput {
        .os           = rstd::move(os).unwrap(),
        .architecture = compiler_default->architecture,
        .source       = CompileTargetSource::CompilerDefault,
    });
}

auto resolve_standard_library_selection(lito::config::StandardLibrarySelection selection,
                                        lito::system::OperatingSystem          os)
    -> ToolchainResult<lito::config::StandardLibrary> {
    auto explicit_family = lito::config::explicit_standard_library(selection);
    if (explicit_family.is_some()) return Ok(*explicit_family);
    switch (os) {
    case lito::system::OperatingSystem::Linux: return Ok(lito::config::StandardLibrary::Libstdcxx);
    case lito::system::OperatingSystem::Windows: return Ok(lito::config::StandardLibrary::Msvc);
    case lito::system::OperatingSystem::Android:
    case lito::system::OperatingSystem::Macos:
    case lito::system::OperatingSystem::Freebsd:
    case lito::system::OperatingSystem::Netbsd:
    case lito::system::OperatingSystem::Openbsd: return Ok(lito::config::StandardLibrary::Libcxx);
    }
    return Err(ToolchainError::Message(
        String::make("cannot automatically select a C++ standard library"_str)));
}

auto target_environment(lito::system::OperatingSystem os,
                        lito::config::StandardLibrary standard_library)
    -> ToolchainResult<lito::system::TargetEnvironment> {
    if (os == lito::system::OperatingSystem::Windows) {
        if (standard_library == lito::config::StandardLibrary::Libstdcxx) {
            return Ok(lito::system::TargetEnvironment::Gnu);
        }
        return Ok(lito::system::TargetEnvironment::Msvc);
    }
    if (standard_library == lito::config::StandardLibrary::Msvc) {
        return Err(ToolchainError::Message(
            rstd::format("standard library 'msvc' is unsupported for target operating system '{}'",
                         lito::system::operating_system_name(os))));
    }
    if ((os == lito::system::OperatingSystem::Android ||
         os == lito::system::OperatingSystem::Macos) &&
        standard_library != lito::config::StandardLibrary::Libcxx) {
        return Err(ToolchainError::Message(
            rstd::format("target operating system '{}' requires standard library 'libc++'",
                         lito::system::operating_system_name(os))));
    }
    if (os == lito::system::OperatingSystem::Linux) {
        return Ok(lito::system::TargetEnvironment::Gnu);
    }
    return Ok(lito::system::TargetEnvironment::Unknown);
}

auto resolve_compile_target(const ToolchainTargetInput&   input,
                            lito::config::StandardLibrary standard_library)
    -> ToolchainResult<CompileTarget> {
    auto environment = target_environment(input.os, standard_library);
    if (environment.is_err()) return Err(rstd::move(environment).unwrap_err());
    auto info = lito::system::encode_target_info(input.os, input.architecture, *environment);
    if (info.is_err()) return Err(ToolchainError::Platform(rstd::move(info).unwrap_err()));
    return Ok(CompileTarget {
        .info             = rstd::move(info).unwrap(),
        .standard_library = standard_library,
        .source           = input.source,
    });
}

auto resolve_sdk_compile_target(const lito::system::TargetInfo& target,
                                lito::config::StandardLibrary   standard_library)
    -> ToolchainResult<CompileTarget> {
    auto os = lito::system::target_operating_system(target);
    if (os.is_err()) return Err(ToolchainError::Platform(rstd::move(os).unwrap_err()));
    auto environment = target_environment(*os, standard_library);
    if (environment.is_err()) return Err(rstd::move(environment).unwrap_err());
    if (target.environment != lito::system::TargetEnvironment::Unknown &&
        target.environment != *environment) {
        return Err(ToolchainError::Message(
            rstd::format("SDK target '{}' environment '{}' conflicts with standard library '{}'",
                         target.triple.as_str(),
                         target.environment_name(),
                         lito::config::standard_library_name(standard_library))));
    }
    return Ok(CompileTarget {
        .info             = target.clone(),
        .standard_library = standard_library,
        .source           = CompileTargetSource::Sdk,
    });
}

} // namespace lito
