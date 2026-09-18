module;
#include <rstd/macro.hpp>

export module lito.toolchain.android:certification;

import rstd;
import licrypto;
import lito.core;
import lito.system;
import lito.toolchain.common;
import lito.toolchain.clang;
import :ndk;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito::system;

export namespace lito
{

struct AndroidNdkCertification {
    String compiler_version;
    String linker_version;
    String target;
    String identity;

    auto clone() const -> AndroidNdkCertification {
        return AndroidNdkCertification {
            .compiler_version = compiler_version.clone(),
            .linker_version   = linker_version.clone(),
            .target           = target.clone(),
            .identity         = identity.clone(),
        };
    }
};

auto certify_android_ndk(const AndroidNdkDistribution&     distribution,
                         const ResolvedProcessEnvironment& environment)
    -> ToolchainResult<AndroidNdkCertification>;

} // namespace lito

namespace lito
{

auto certification_command(Vec<String>                       arguments,
                           ref<str>                          input,
                           ref<str>                          description,
                           const ResolvedProcessEnvironment& environment,
                           ref<rstd::path::Path> working_directory) -> ToolchainResult<empty> {
    auto output = run_command_with_input(arguments, input, environment, Some(working_directory));
    if (output.is_err()) return Err(rstd::into<ToolchainError>(rstd::move(output).unwrap_err()));
    if (output->exit_code != i32 {}) {
        return Err(ToolchainError::Execution(description.into(),
                                             output->exit_code,
                                             rstd::move(output->standard_output),
                                             rstd::move(output->standard_error)));
    }
    return Ok(empty {});
}

auto push_android_driver_prefix(Vec<String>&                 arguments,
                                ref<rstd::path::Path>        driver,
                                const ResolvedAndroidTarget& target,
                                ref<rstd::path::Path>        linker) -> ToolchainResult<empty> {
    rstd_try(toolchain::command::push_path(arguments, driver));
    arguments.push(rstd::format("--target={}", target.clang_target.as_str()));
    rstd_try(toolchain::command::push_path_option(
        arguments, "--sysroot="_str, target.sysroot.as_path()));
    rstd_try(push_clang_lld_selection(arguments, linker));
    return Ok(empty {});
}

} // namespace lito

export namespace lito
{

auto certify_android_ndk(const AndroidNdkDistribution&     distribution,
                         const ResolvedProcessEnvironment& environment)
    -> ToolchainResult<AndroidNdkCertification> {
    const ref<str> required_abis[] = {
        "armeabi-v7a"_str, "arm64-v8a"_str, "x86"_str, "x86_64"_str
    };
    for (const auto required : required_abis) {
        auto found = false;
        for (const auto& abi : distribution.abis()) {
            if (abi.name.as_str() == required && abi.default_supported && ! abi.deprecated) {
                found = true;
                break;
            }
        }
        if (! found) {
            return Err(ToolchainError::Message(
                rstd::format("Android NDK {} does not enable required ABI '{}'",
                             distribution.revision().text.as_str(),
                             required)));
        }
    }

    auto clang = ClangToolchain::create(distribution.toolchain_spec(), environment);
    if (clang.is_err()) return Err(rstd::move(clang).unwrap_err());
    auto api = distribution.platforms().minimum;
    for (const auto& abi : distribution.abis()) {
        if (abi.name == "arm64-v8a"_str && abi.minimum_api > api) api = abi.minimum_api;
    }
    auto target = resolve_android_target(distribution,
                                         lito::config::AndroidTargetRequest {
                                             .abi         = "arm64-v8a"_Str,
                                             .minimum_api = api,
                                         });
    if (target.is_err()) {
        return Err(ToolchainError::Message(
            rstd::format("Android NDK certification target resolution failed: {}",
                         rstd::move(target).unwrap_err())));
    }

    auto directory = rstd::fs::TempDir::make("lito-android-ndk-certify"_str);
    if (directory.is_err()) {
        return Err(ToolchainError::Io("create Android NDK certification directory"_Str,
                                      rstd::env::temp_dir(),
                                      rstd::move(directory).unwrap_err()));
    }
    auto root           = PathBuf::from(directory->path());
    auto module_bmi     = root.join(PathBuf::from("probe.pcm"_str).as_path());
    auto module_obj     = root.join(PathBuf::from("probe.o"_str).as_path());
    auto user_obj       = root.join(PathBuf::from("consumer.o"_str).as_path());
    auto c_obj          = root.join(PathBuf::from("c_probe.o"_str).as_path());
    auto library        = root.join(PathBuf::from("liblito_ndk_probe.so"_str).as_path());
    auto static_library = root.join(PathBuf::from("liblito_ndk_probe_static.so"_str).as_path());

    auto module = Vec<String>::make();
    rstd_try(push_android_driver_prefix(module,
                                        distribution.paths().cxx.as_path(),
                                        *target,
                                        distribution.paths().linker.as_path()));
    module.push("-std=c++23"_Str);
    module.push("-fmodules-reduced-bmi"_Str);
    rstd_try(
        toolchain::command::push_path_option(module, "-fmodule-output="_str, module_bmi.as_path()));
    module.push("-fPIC"_Str);
    module.push("-x"_Str);
    module.push("c++-module"_Str);
    module.push("-c"_Str);
    module.push("-"_Str);
    module.push("-o"_Str);
    rstd_try(toolchain::command::push_path(module, module_obj.as_path()));
    rstd_try(certification_command(rstd::move(module),
                                   "export module lito.ndk.probe; "
                                   "export int value() { auto* value = new int(7); "
                                   "auto result = *value; delete value; return result; }\n"_str,
                                   "Android NDK named module compile"_str,
                                   environment,
                                   root.as_path()));

    auto consumer = Vec<String>::make();
    rstd_try(push_android_driver_prefix(consumer,
                                        distribution.paths().cxx.as_path(),
                                        *target,
                                        distribution.paths().linker.as_path()));
    consumer.push("-std=c++23"_Str);
    consumer.push("-fmodules-reduced-bmi"_Str);
    rstd_try(toolchain::command::push_path_option(
        consumer, "-fmodule-file=lito.ndk.probe="_str, module_bmi.as_path()));
    consumer.push("-fPIC"_Str);
    consumer.push("-x"_Str);
    consumer.push("c++"_Str);
    consumer.push("-c"_Str);
    consumer.push("-"_Str);
    consumer.push("-o"_Str);
    rstd_try(toolchain::command::push_path(consumer, user_obj.as_path()));
    rstd_try(certification_command(
        rstd::move(consumer),
        "import lito.ndk.probe; extern \"C\" int lito_ndk_probe() { return value(); }\n"_str,
        "Android NDK module consumer compile"_str,
        environment,
        root.as_path()));

    auto c_compile = Vec<String>::make();
    rstd_try(push_android_driver_prefix(c_compile,
                                        distribution.paths().cc.as_path(),
                                        *target,
                                        distribution.paths().linker.as_path()));
    c_compile.push("-std=c17"_Str);
    c_compile.push("-fPIC"_Str);
    c_compile.push("-x"_Str);
    c_compile.push("c"_Str);
    c_compile.push("-c"_Str);
    c_compile.push("-"_Str);
    c_compile.push("-o"_Str);
    rstd_try(toolchain::command::push_path(c_compile, c_obj.as_path()));
    rstd_try(certification_command(rstd::move(c_compile),
                                   "int lito_ndk_c_probe(void) { return 3; }\n"_str,
                                   "Android NDK C compile"_str,
                                   environment,
                                   root.as_path()));

    auto link = Vec<String>::make();
    rstd_try(push_android_driver_prefix(
        link, distribution.paths().cxx.as_path(), *target, distribution.paths().linker.as_path()));
    link.push("-shared"_Str);
    link.push("-Wl,-soname,liblito_ndk_probe.so"_Str);
    rstd_try(toolchain::command::push_path(link, module_obj.as_path()));
    rstd_try(toolchain::command::push_path(link, user_obj.as_path()));
    rstd_try(toolchain::command::push_path(link, c_obj.as_path()));
    link.push("-o"_Str);
    rstd_try(toolchain::command::push_path(link, library.as_path()));
    rstd_try(certification_command(rstd::move(link),
                                   ""_str,
                                   "Android NDK shared library link"_str,
                                   environment,
                                   root.as_path()));

    auto readelf = Vec<String>::make();
    rstd_try(toolchain::command::push_path(readelf, distribution.paths().readelf.as_path()));
    readelf.push("-h"_Str);
    readelf.push("-d"_Str);
    rstd_try(toolchain::command::push_path(readelf, library.as_path()));
    auto inspected = toolchain::command::tool_output_raw(
        rstd::move(readelf), "Android NDK llvm-readelf"_str, environment);
    if (inspected.is_err()) return Err(rstd::move(inspected).unwrap_err());
    if (! inspected->as_str().contains("AArch64"_str) ||
        ! inspected->as_str().contains("liblito_ndk_probe.so"_str)) {
        return Err(ToolchainError::Message(
            "Android NDK certification produced an unexpected ELF machine or SONAME"_Str));
    }
    if (! inspected->as_str().contains("libc++_shared.so"_str)) {
        return Err(ToolchainError::Message(
            "Android NDK dynamic runtime certification did not depend on libc++_shared.so"_Str));
    }

    auto static_link = Vec<String>::make();
    rstd_try(push_android_driver_prefix(static_link,
                                        distribution.paths().cxx.as_path(),
                                        *target,
                                        distribution.paths().linker.as_path()));
    static_link.push("-shared"_Str);
    static_link.push("-static-libstdc++"_Str);
    static_link.push("-Wl,-soname,liblito_ndk_probe_static.so"_Str);
    rstd_try(toolchain::command::push_path(static_link, module_obj.as_path()));
    rstd_try(toolchain::command::push_path(static_link, user_obj.as_path()));
    rstd_try(toolchain::command::push_path(static_link, c_obj.as_path()));
    static_link.push("-o"_Str);
    rstd_try(toolchain::command::push_path(static_link, static_library.as_path()));
    rstd_try(certification_command(rstd::move(static_link),
                                   ""_str,
                                   "Android NDK static runtime shared library link"_str,
                                   environment,
                                   root.as_path()));

    auto static_readelf = Vec<String>::make();
    rstd_try(toolchain::command::push_path(static_readelf, distribution.paths().readelf.as_path()));
    static_readelf.push("-h"_Str);
    static_readelf.push("-d"_Str);
    rstd_try(toolchain::command::push_path(static_readelf, static_library.as_path()));
    auto static_inspected = toolchain::command::tool_output_raw(
        rstd::move(static_readelf), "Android NDK static runtime llvm-readelf"_str, environment);
    if (static_inspected.is_err()) return Err(rstd::move(static_inspected).unwrap_err());
    if (! static_inspected->as_str().contains("AArch64"_str) ||
        ! static_inspected->as_str().contains("liblito_ndk_probe_static.so"_str) ||
        static_inspected->as_str().contains("libc++_shared.so"_str)) {
        return Err(ToolchainError::Message(
            "Android NDK static runtime certification produced unexpected ELF metadata"_Str));
    }

    auto linker_command = Vec<String>::make();
    rstd_try(toolchain::command::push_path(linker_command, distribution.paths().linker.as_path()));
    linker_command.push("--version"_Str);
    auto linker_version = toolchain::command::tool_output(
        rstd::move(linker_command), "Android NDK LLD version"_str, environment);
    if (linker_version.is_err()) return Err(rstd::move(linker_version).unwrap_err());
    auto identity =
        licrypto::sha256_hex(rstd::format("android-ndk-certification-v2\n{}\n{}\n{}\n{}",
                                          distribution.identity(),
                                          clang->compiler_identity().version.as_str(),
                                          linker_version->as_str(),
                                          target->clang_target.as_str())
                                 .as_str());
    return Ok(AndroidNdkCertification {
        .compiler_version = clang->compiler_identity().version.clone(),
        .linker_version   = rstd::move(linker_version).unwrap(),
        .target           = target->clang_target.clone(),
        .identity         = rstd::move(identity),
    });
}

} // namespace lito
