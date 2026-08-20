module;
#include <rstd/macro.hpp>

export module lito.toolchain.clang:sdk;

import rstd;
import lito.cpp;
import lito.core;
import lito.toolchain.common;
import lito.system;
import :sdk_catalog;
import :toolchain;
import :format;

using namespace rstd::prelude;
using namespace lito::system;
using namespace rstd::literals;

namespace lito
{

template<typename T>
auto sdk_failure(String message) -> ToolchainResult<T> {
    return Err(ToolchainError::Message(rstd::move(message)));
}

template<typename T>
auto sdk_failure(ref<str> message) -> ToolchainResult<T> {
    return sdk_failure<T>(String::make(message));
}

auto require_sdk_file(ref<rstd::path::Path> path, ref<str> description) -> ToolchainResult<empty> {
    auto metadata = rstd::fs::metadata(path);
    if (metadata.is_err()) {
        return Err(ToolchainError::Io(rstd::format("inspect {}", description),
                                      PathBuf::from(path),
                                      rstd::move(metadata).unwrap_err()));
    }
    if (! metadata->is_file()) {
        return sdk_failure<empty>(rstd::format("{} '{}' is not a file", description, path));
    }
    return Ok(empty {});
}

auto require_contained_sdk_file(ref<rstd::path::Path> prefix,
                                ref<rstd::path::Path> path,
                                ref<str>              description) -> ToolchainResult<empty> {
    rstd_try(require_sdk_file(path, description));
    auto canonical = rstd::fs::canonicalize(path);
    if (canonical.is_err()) {
        return Err(ToolchainError::Io(rstd::format("resolve {}", description),
                                      PathBuf::from(path),
                                      rstd::move(canonical).unwrap_err()));
    }
    if (! canonical->as_path().starts_with(prefix)) {
        return sdk_failure<empty>(rstd::format(
            "{} '{}' escapes LLVM SDK prefix '{}'", description, canonical->as_path(), prefix));
    }
    return Ok(empty {});
}

} // namespace lito

export namespace lito
{

struct ClangSdkLayout {
    PathBuf cmake;
    PathBuf clang_cpp;
    PathBuf llvm_config;
};

struct ClangSdk {
    PathBuf                       prefix;
    PathBuf                       cmake_search_path;
    PathBuf                       clang_cpp;
    PathBuf                       llvm_config;
    lito::config::StandardLibrary standard_library { lito::config::StandardLibrary::Libstdcxx };
    bool                          exceptions { false };
    bool                          rtti { false };
    String                        identity;
};

struct LlvmSdkCertification {
    String                        compiler_version;
    lito::config::StandardLibrary standard_library { lito::config::StandardLibrary::Libstdcxx };
    bool                          exceptions { false };
    bool                          rtti { false };
    String                        identity;
};

auto inspect_clang_sdk(const CompilerIdentity&           compiler,
                       ref<rstd::path::Path>             prefix,
                       const ClangSdkLayout&             layout,
                       const ResolvedProcessEnvironment& environment) -> ToolchainResult<ClangSdk> {
    auto canonical_prefix = rstd::fs::canonicalize(prefix);
    if (canonical_prefix.is_err()) {
        return Err(ToolchainError::Io(String::make("resolve Clang SDK prefix"_str),
                                      PathBuf::from(prefix),
                                      rstd::move(canonical_prefix).unwrap_err()));
    }
    auto cmake_search_path = canonical_prefix->join(layout.cmake.as_path());
    auto clang_config =
        cmake_search_path.join(PathBuf::from("clang/ClangConfig.cmake"_str).as_path());
    auto clang_cpp   = canonical_prefix->join(layout.clang_cpp.as_path());
    auto llvm_config = canonical_prefix->join(layout.llvm_config.as_path());
    rstd_try(require_contained_sdk_file(
        canonical_prefix->as_path(), clang_config.as_path(), "Clang CMake package"_str));
    rstd_try(require_contained_sdk_file(
        canonical_prefix->as_path(), clang_cpp.as_path(), "libclang-cpp"_str));
    rstd_try(require_contained_sdk_file(
        canonical_prefix->as_path(), llvm_config.as_path(), "llvm-config"_str));

    auto flags_command = Vec<String>::make();
    flags_command.push(String::make(llvm_config.as_path().to_str().unwrap()));
    flags_command.push(String::make("--cxxflags"_str));
    auto flags = run_command(flags_command, environment);
    if (flags.is_err()) return Err(rstd::into<ToolchainError>(rstd::move(flags).unwrap_err()));
    if (flags->exit_code != i32 {}) {
        return Err(ToolchainError::Execution(String::make("query Clang SDK C++ flags"_str),
                                             flags->exit_code,
                                             rstd::move(flags->standard_output),
                                             rstd::move(flags->standard_error)));
    }
    auto cxxflags         = trim_ascii(rstd::move(flags->standard_output));
    auto standard_library = cxxflags.as_str().contains("-stdlib=libc++"_str)
                                ? lito::config::StandardLibrary::Libcxx
                                : lito::config::StandardLibrary::Libstdcxx;
    auto exceptions       = ! cxxflags.as_str().contains("-fno-exceptions"_str);
    auto rtti             = ! cxxflags.as_str().contains("-fno-rtti"_str);
    auto library_metadata = rstd::fs::metadata(clang_cpp.as_path());
    if (library_metadata.is_err()) {
        return Err(ToolchainError::Io(String::make("inspect libclang-cpp identity"_str),
                                      clang_cpp.clone(),
                                      rstd::move(library_metadata).unwrap_err()));
    }
    auto identity = rstd::format("clang-sdk-v1\ncompiler={}\nlibrary={}\nsize={}\ncxxflags={}",
                                 compiler.build_identity.as_str(),
                                 clang_cpp.as_path(),
                                 library_metadata->len(),
                                 cxxflags.as_str());
    return Ok(ClangSdk {
        .prefix            = rstd::move(canonical_prefix).unwrap(),
        .cmake_search_path = rstd::move(cmake_search_path),
        .clang_cpp         = rstd::move(clang_cpp),
        .llvm_config       = rstd::move(llvm_config),
        .standard_library  = standard_library,
        .exceptions        = exceptions,
        .rtti              = rtti,
        .identity          = rstd::move(identity),
    });
}

auto resolve_clang_sdk(const CompilerIdentity&           compiler,
                       const ResolvedProcessEnvironment& environment) -> ToolchainResult<ClangSdk> {
    auto binary_directory = compiler.path.as_path().parent();
    if (binary_directory.is_none()) {
        return sdk_failure<ClangSdk>(
            rstd::format("Clang executable '{}' has no parent directory", compiler.path.as_path()));
    }
    auto prefix = (*binary_directory).parent();
    if (prefix.is_none()) {
        return sdk_failure<ClangSdk>(
            rstd::format("Clang executable '{}' has no SDK prefix", compiler.path.as_path()));
    }
    return inspect_clang_sdk(compiler,
                             *prefix,
                             ClangSdkLayout {
                                 .cmake       = PathBuf::from("lib/cmake"_str),
                                 .clang_cpp   = PathBuf::from("lib/libclang-cpp.so"_str),
                                 .llvm_config = PathBuf::from("bin/llvm-config"_str),
                             },
                             environment);
}

auto certify_llvm_sdk(ref<rstd::path::Path>             prefix,
                      ref<str>                          expected_version,
                      const LlvmSdkPaths&               paths,
                      const ResolvedProcessEnvironment& environment)
    -> ToolchainResult<LlvmSdkCertification> {
    auto certification_environment           = environment.without_variable("LD_LIBRARY_PATH"_str);
    constexpr ref<str> removed_environment[] = {
        "LD_PRELOAD"_str,
        "LD_AUDIT"_str,
        "DYLD_LIBRARY_PATH"_str,
        "DYLD_FRAMEWORK_PATH"_str,
        "DYLD_INSERT_LIBRARIES"_str,
    };
    for (const auto key : removed_environment) {
        certification_environment = certification_environment.without_variable(key);
    }
    auto canonical_prefix = rstd::fs::canonicalize(prefix);
    if (canonical_prefix.is_err()) {
        return Err(ToolchainError::Io(String::make("resolve LLVM SDK prefix"_str),
                                      PathBuf::from(prefix),
                                      rstd::move(canonical_prefix).unwrap_err()));
    }
    const auto absolute = [&](ref<rstd::path::Path> relative) {
        return canonical_prefix->join(relative);
    };
    auto cc       = absolute(paths.cc.as_path());
    auto cxx      = absolute(paths.cxx.as_path());
    auto linker   = absolute(paths.linker.as_path());
    auto archiver = absolute(paths.archiver.as_path());
    auto strip    = absolute(paths.strip.as_path());
    auto format   = absolute(paths.format.as_path());
    rstd_try(
        require_contained_sdk_file(canonical_prefix->as_path(), cc.as_path(), "C compiler"_str));
    rstd_try(
        require_contained_sdk_file(canonical_prefix->as_path(), cxx.as_path(), "C++ compiler"_str));
    rstd_try(
        require_contained_sdk_file(canonical_prefix->as_path(), linker.as_path(), "linker"_str));
    rstd_try(require_contained_sdk_file(
        canonical_prefix->as_path(), archiver.as_path(), "archiver"_str));
    rstd_try(
        require_contained_sdk_file(canonical_prefix->as_path(), strip.as_path(), "strip tool"_str));
    rstd_try(require_contained_sdk_file(
        canonical_prefix->as_path(), format.as_path(), "format tool"_str));
    auto toolchain_spec = lito::config::ToolchainSpec {
        .cc  = rstd::move(cc),
        .cxx = rstd::move(cxx),
        .ld  = rstd::move(linker),
        .ar  = rstd::move(archiver),
    };
    auto resolver  = ToolResolver(certification_environment);
    auto toolchain = ClangToolchain::create(toolchain_spec, resolver, certification_environment);
    if (toolchain.is_err()) return Err(rstd::move(toolchain).unwrap_err());
    auto version_marker = rstd::format("clang version {}", expected_version);
    if (! toolchain->compiler_identity().version.as_str().contains(version_marker.as_str())) {
        return sdk_failure<LlvmSdkCertification>(
            rstd::format("LLVM SDK compiler version does not match catalog version '{}': {}",
                         expected_version,
                         toolchain->compiler_identity().version.as_str()));
    }
    auto sdk = inspect_clang_sdk(toolchain->compiler_identity(),
                                 canonical_prefix->as_path(),
                                 ClangSdkLayout {
                                     .cmake       = paths.cmake.clone(),
                                     .clang_cpp   = paths.clang_cpp.clone(),
                                     .llvm_config = paths.llvm_config.clone(),
                                 },
                                 certification_environment);
    if (sdk.is_err()) return Err(rstd::move(sdk).unwrap_err());

    const auto probe_llvm_tool = [&](ref<rstd::path::Path> executable,
                                     ref<str>              description) -> ToolchainResult<String> {
        auto command = Vec<String>::make();
        rstd_try(lito::toolchain::command::push_path(command, executable));
        lito::toolchain::command::push_option(command, "--version"_str);
        return lito::toolchain::command::tool_output(
            rstd::move(command), description, certification_environment);
    };
    auto ar_version = probe_llvm_tool(toolchain_spec.ar.as_path(), "llvm-ar --version"_str);
    if (ar_version.is_err()) return Err(rstd::move(ar_version).unwrap_err());
    if (! ar_version->as_str().contains("LLVM"_str)) {
        return sdk_failure<LlvmSdkCertification>("configured archiver is not llvm-ar"_str);
    }
    auto strip_version = probe_llvm_tool(strip.as_path(), "llvm-strip --version"_str);
    if (strip_version.is_err()) return Err(rstd::move(strip_version).unwrap_err());
    if (! strip_version->as_str().contains("LLVM"_str)) {
        return sdk_failure<LlvmSdkCertification>("configured strip tool is not llvm-strip"_str);
    }
    auto formatter =
        lito::toolchain::ClangFormat::create(format.as_path(), certification_environment);
    if (formatter.is_err()) return Err(rstd::move(formatter).unwrap_err());

    auto llvm_version_command = Vec<String>::make();
    rstd_try(lito::toolchain::command::push_path(llvm_version_command, sdk->llvm_config.as_path()));
    lito::toolchain::command::push_option(llvm_version_command, "--version"_str);
    auto llvm_version = lito::toolchain::command::tool_output(
        rstd::move(llvm_version_command), "llvm-config --version"_str, certification_environment);
    if (llvm_version.is_err()) return Err(rstd::move(llvm_version).unwrap_err());
    if (llvm_version->as_str() != expected_version) {
        return sdk_failure<LlvmSdkCertification>(rstd::format(
            "llvm-config reports '{}', expected '{}'", llvm_version->as_str(), expected_version));
    }
    auto probe_directory = rstd::fs::TempDir::make("lito-llvm-sdk-certify"_str);
    if (probe_directory.is_err()) {
        return Err(ToolchainError::Io(String::make("create LLVM SDK certification directory"_str),
                                      rstd::env::temp_dir(),
                                      rstd::move(probe_directory).unwrap_err()));
    }
    auto probe_output =
        PathBuf::from(probe_directory->path()).join(PathBuf::from("probe"_str).as_path());
    auto probe_command = Vec<String>::make();
    rstd_try(lito::toolchain::command::push_path(probe_command, toolchain->cc_path()));
    rstd_try(lito::toolchain::command::push_path_option(
        probe_command, "-fuse-ld="_str, toolchain->linker_identity().executable.as_path()));
    lito::toolchain::command::push_option(probe_command, "-x"_str);
    lito::toolchain::command::push_option(probe_command, "c"_str);
    lito::toolchain::command::push_option(probe_command, "-"_str);
    lito::toolchain::command::push_option(probe_command, "-o"_str);
    rstd_try(lito::toolchain::command::push_path(probe_command, probe_output.as_path()));
    auto linked = run_command_with_input(probe_command,
                                         "int main(void) { return 0; }\n"_str,
                                         certification_environment,
                                         Some(probe_directory->path()));
    if (linked.is_err()) return Err(rstd::into<ToolchainError>(rstd::move(linked).unwrap_err()));
    if (linked->exit_code != i32 {}) {
        return Err(ToolchainError::Execution(String::make("LLVM SDK host ELF link"_str),
                                             linked->exit_code,
                                             rstd::move(linked->standard_output),
                                             rstd::move(linked->standard_error)));
    }
    auto execute_command = Vec<String>::make();
    rstd_try(lito::toolchain::command::push_path(execute_command, probe_output.as_path()));
    auto executed =
        run_command(execute_command, certification_environment, Some(probe_directory->path()));
    if (executed.is_err()) {
        return Err(rstd::into<ToolchainError>(rstd::move(executed).unwrap_err()));
    }
    if (executed->exit_code != i32 {}) {
        return Err(ToolchainError::Execution(String::make("LLVM SDK host ELF probe"_str),
                                             executed->exit_code,
                                             rstd::move(executed->standard_output),
                                             rstd::move(executed->standard_error)));
    }
    auto probe_path = PathBuf::from(probe_directory->path());
    auto closed     = probe_directory->close();
    if (closed.is_err()) {
        return Err(ToolchainError::Io(String::make("remove LLVM SDK certification directory"_str),
                                      rstd::move(probe_path),
                                      rstd::move(closed).unwrap_err()));
    }
    return Ok(LlvmSdkCertification {
        .compiler_version = rstd::move(llvm_version).unwrap(),
        .standard_library = sdk->standard_library,
        .exceptions       = sdk->exceptions,
        .rtti             = sdk->rtti,
        .identity         = sdk->identity.clone(),
    });
}

} // namespace lito
