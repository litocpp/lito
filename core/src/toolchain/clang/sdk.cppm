module;
#include <rstd/macro.hpp>

export module lito.toolchain:clang.sdk;

import rstd;
import lito.cpp;
import lito.error;
import lito.toolchain.contract;
import lito.system.environment;
import lito.system.process;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace lito
{

template<typename T>
auto sdk_failure(String message) -> ToolchainResult<T> {
    return Err(ToolchainError::Message(rstd::move(message)));
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

} // namespace lito

export namespace lito
{

struct ClangSdk {
    PathBuf         prefix;
    PathBuf         cmake_search_path;
    PathBuf         clang_cpp;
    PathBuf         llvm_config;
    StandardLibrary standard_library { StandardLibrary::Libstdcxx };
    bool            exceptions { false };
    bool            rtti { false };
    String          identity;
};

auto resolve_clang_sdk(const CompilerIdentity&           compiler,
                       const ResolvedProcessEnvironment& environment) -> ToolchainResult<ClangSdk> {
    auto binary_directory = compiler.path.as_path().parent();
    if (binary_directory.is_none()) {
        return sdk_failure<ClangSdk>(
            rstd::format("Clang executable '{}' has no parent directory", compiler.path.as_path()));
    }
    auto prefix_path = (*binary_directory).parent();
    if (prefix_path.is_none()) {
        return sdk_failure<ClangSdk>(
            rstd::format("Clang executable '{}' has no SDK prefix", compiler.path.as_path()));
    }
    auto prefix            = PathBuf::from(*prefix_path);
    auto cmake_search_path = prefix.join(PathBuf::from("lib/cmake"_str).as_path());
    auto clang_config =
        cmake_search_path.join(PathBuf::from("clang/ClangConfig.cmake"_str).as_path());
    auto clang_cpp   = prefix.join(PathBuf::from("lib/libclang-cpp.so"_str).as_path());
    auto llvm_config = prefix.join(PathBuf::from("bin/llvm-config"_str).as_path());
    rstd_try(require_sdk_file(clang_config.as_path(), "Clang CMake package"_str));
    rstd_try(require_sdk_file(clang_cpp.as_path(), "libclang-cpp"_str));
    rstd_try(require_sdk_file(llvm_config.as_path(), "llvm-config"_str));

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
                                ? StandardLibrary::Libcxx
                                : StandardLibrary::Libstdcxx;
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
        .prefix            = rstd::move(prefix),
        .cmake_search_path = rstd::move(cmake_search_path),
        .clang_cpp         = rstd::move(clang_cpp),
        .llvm_config       = rstd::move(llvm_config),
        .standard_library  = standard_library,
        .exceptions        = exceptions,
        .rtti              = rtti,
        .identity          = rstd::move(identity),
    });
}

} // namespace lito
