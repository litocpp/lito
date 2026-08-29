#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito.core;
import lito.system;
import lito.toolchain;
import lito.driver;
import lito.test.support;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito_test;
using PathBuf = rstd::path::PathBuf;

constexpr auto abi_metadata = R"json({
  "armeabi-v7a": {
    "triple": "arm-linux-androideabi",
    "llvm_triple": "armv7-none-linux-androideabi",
    "min_os_version": 21,
    "default": true,
    "deprecated": false
  },
  "arm64-v8a": {
    "triple": "aarch64-linux-android",
    "llvm_triple": "aarch64-none-linux-android",
    "min_os_version": 21,
    "default": true,
    "deprecated": false
  },
  "x86": {
    "triple": "i686-linux-android",
    "llvm_triple": "i686-none-linux-android",
    "min_os_version": 21,
    "default": true,
    "deprecated": false
  },
  "x86_64": {
    "triple": "x86_64-linux-android",
    "llvm_triple": "x86_64-none-linux-android",
    "min_os_version": 21,
    "default": true,
    "deprecated": false
  }
})json"_str;

constexpr auto platform_metadata = R"json({"min": 21, "max": 35})json"_str;

class AndroidNdk : public ProjectFixture {
protected:
    auto distribution(ref<str> name)
        -> lito::source::SourceTreeResult<lito::source::SourceMaterialization> {
        const ProjectFile files[] = {
            { "source.properties"_str,
              "Pkg.Desc = Android NDK\nPkg.Revision = 29.0.14206865\nPkg.ReleaseName = r29\n"_str },
            { "meta/abis.json"_str, abi_metadata },
            { "meta/platforms.json"_str, platform_metadata },
            { "toolchains/llvm/prebuilt/linux-x86_64/bin/clang"_str, ""_str },
            { "toolchains/llvm/prebuilt/linux-x86_64/bin/clang++"_str, ""_str },
            { "toolchains/llvm/prebuilt/linux-x86_64/bin/ld.lld"_str, ""_str },
            { "toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-ar"_str, ""_str },
            { "toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip"_str, ""_str },
            { "toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-readelf"_str, ""_str },
            { "toolchains/llvm/prebuilt/linux-x86_64/sysroot/.keep"_str, ""_str },
            { "toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/arm-linux-androideabi/libc++_shared.so"_str,
              ""_str },
            { "toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so"_str,
              ""_str },
            { "toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/i686-linux-android/libc++_shared.so"_str,
              ""_str },
            { "toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/x86_64-linux-android/libc++_shared.so"_str,
              ""_str },
            { "build/cmake/android.toolchain.cmake"_str, "set(ANDROID TRUE)\n"_str },
        };
        return materialize(name, files);
    }
};

auto linux_x86_64_host() -> lito::system::HostInfo {
    return lito::system::HostInfo {
        .architecture = lito::system::require_architecture("x86_64"_str).unwrap(),
        .os           = String::make("linux"_str),
    };
}

TEST_F(AndroidNdk, InstalledMetadataOwnsAbiApiAndToolPaths) {
    auto installed = distribution("metadata"_str);
    ASSERT_TRUE(installed.is_ok());
    auto opened = lito::open_android_ndk(installed->root.as_path(), linux_x86_64_host());
    ASSERT_TRUE(opened.is_ok());
    EXPECT_EQ(opened->revision().text.as_str(), "29.0.14206865"_str);
    EXPECT_EQ(opened->release_name(), "r29"_str);
    EXPECT_EQ(opened->host_tag(), "linux-x86_64"_str);
    EXPECT_EQ(opened->abis().len(), usize(4));
    EXPECT_EQ(opened->platforms().minimum, u32(21));
    EXPECT_EQ(opened->platforms().maximum, u32(35));
    EXPECT_TRUE(opened->paths().cxx.as_path().starts_with(opened->root()));
    EXPECT_TRUE(opened->paths().sysroot.as_path().starts_with(opened->root()));

    auto platforms = installed->root.join(PathBuf::from("meta/platforms.json"_str).as_path());
    ASSERT_TRUE(
        rstd::fs::write(platforms.as_path(), "{\"min\": 35, \"max\": 21}"_str.as_bytes()).is_ok());
    EXPECT_TRUE(lito::open_android_ndk(installed->root.as_path(), linux_x86_64_host()).is_err());
    ASSERT_TRUE(rstd::fs::write(platforms.as_path(), platform_metadata.as_bytes()).is_ok());

    auto abis = installed->root.join(PathBuf::from("meta/abis.json"_str).as_path());
    ASSERT_TRUE(
        rstd::fs::write(abis.as_path(), "{\"arm64-v8a\": {\"default\": true}}"_str.as_bytes())
            .is_ok());
    EXPECT_TRUE(lito::open_android_ndk(installed->root.as_path(), linux_x86_64_host()).is_err());
}

TEST_F(AndroidNdk, FourPublicAbisMapToCanonicalClangTargets) {
    auto installed = distribution("targets"_str);
    ASSERT_TRUE(installed.is_ok());
    auto opened = lito::open_android_ndk(installed->root.as_path(), linux_x86_64_host());
    ASSERT_TRUE(opened.is_ok());
    struct Mapping {
        ref<str> abi;
        ref<str> clang_target;
        ref<str> library_triple;
    };
    constexpr Mapping mappings[] = {
        { "armeabi-v7a"_str, "armv7a-linux-androideabi21"_str, "arm-linux-androideabi"_str },
        { "arm64-v8a"_str, "aarch64-linux-android21"_str, "aarch64-linux-android"_str },
        { "x86"_str, "i686-linux-android21"_str, "i686-linux-android"_str },
        { "x86_64"_str, "x86_64-linux-android21"_str, "x86_64-linux-android"_str },
    };
    for (const auto& mapping : mappings) {
        auto target = lito::resolve_android_target(*opened,
                                                   lito::config::AndroidTargetRequest {
                                                       .abi         = String::make(mapping.abi),
                                                       .minimum_api = u32(21),
                                                   });
        ASSERT_TRUE(target.is_ok());
        EXPECT_EQ(target->clang_target.as_str(), mapping.clang_target);
        EXPECT_EQ(target->library_triple.as_str(), mapping.library_triple);
        EXPECT_EQ(target->target_info.platform, lito::system::TargetPlatform::Android);
        EXPECT_EQ(target->output_key.as_str(), rstd::format("android-{}-api21", mapping.abi));
    }

    auto below = lito::resolve_android_target(
        *opened,
        lito::config::AndroidTargetRequest { .abi         = String::make("arm64-v8a"_str),
                                             .minimum_api = u32(20) });
    auto above = lito::resolve_android_target(
        *opened,
        lito::config::AndroidTargetRequest { .abi         = String::make("arm64-v8a"_str),
                                             .minimum_api = u32(36) });
    auto unsupported = lito::resolve_android_target(
        *opened,
        lito::config::AndroidTargetRequest { .abi         = String::make("riscv64"_str),
                                             .minimum_api = u32(35) });
    EXPECT_TRUE(below.is_err());
    EXPECT_TRUE(above.is_err());
    EXPECT_TRUE(unsupported.is_err());
}

TEST_F(AndroidNdk, RuntimeAndCMakeProjectionShareResolvedTarget) {
    auto installed = distribution("projection"_str);
    ASSERT_TRUE(installed.is_ok());
    auto opened = lito::open_android_ndk(installed->root.as_path(), linux_x86_64_host());
    ASSERT_TRUE(opened.is_ok());
    auto dynamic = lito::resolve_android_toolchain(
        opened->clone(),
        lito::config::AndroidTargetRequest { .abi         = String::make("arm64-v8a"_str),
                                             .minimum_api = u32(24) },
        lito::config::StandardLibraryRuntime::Dynamic);
    ASSERT_TRUE(dynamic.is_ok());
    EXPECT_EQ(dynamic->target.clang_target.as_str(), "aarch64-linux-android24"_str);
    EXPECT_EQ(dynamic->cmake.abi.as_str(), "arm64-v8a"_str);
    EXPECT_EQ(dynamic->cmake.platform.as_str(), "android-24"_str);
    EXPECT_EQ(dynamic->cmake.standard_library.as_str(), "c++_shared"_str);
    ASSERT_TRUE(dynamic->shared_runtime.is_some());
    EXPECT_EQ(dynamic->shared_runtime->name.as_str(), "libc++_shared.so"_str);
    EXPECT_EQ(dynamic->shared_runtime->path.as_path().file_name().unwrap().to_str().unwrap(),
              "libc++_shared.so"_str);
    EXPECT_EQ(dynamic->shared_runtime->identity.len(), usize(64));

    auto static_runtime = lito::resolve_android_toolchain(
        opened->clone(),
        lito::config::AndroidTargetRequest { .abi         = String::make("arm64-v8a"_str),
                                             .minimum_api = u32(24) },
        lito::config::StandardLibraryRuntime::Static);
    ASSERT_TRUE(static_runtime.is_ok());
    EXPECT_EQ(static_runtime->cmake.standard_library.as_str(), "c++_static"_str);
    EXPECT_TRUE(static_runtime->shared_runtime.is_none());
    EXPECT_NE(dynamic->cmake.identity.as_str(), static_runtime->cmake.identity.as_str());
}

TEST(AndroidBuildLayout, TargetKeyIsolatesEveryProfileLocalPath) {
    auto owner = PathBuf::from("/project"_str);
    auto host =
        lito::BuildLayout::resolve(owner.as_path(), PathBuf::make().as_path(), "release"_str);
    auto arm64 = lito::BuildLayout::resolve(
        owner.as_path(), PathBuf::make().as_path(), "release"_str, "android-arm64-v8a-api21"_str);
    auto x86 = lito::BuildLayout::resolve(
        owner.as_path(), PathBuf::make().as_path(), "release"_str, "android-x86_64-api21"_str);
    EXPECT_EQ(host.output(), PathBuf::from("/project/build/release"_str).as_path());
    EXPECT_EQ(
        arm64.output(),
        PathBuf::from("/project/build/release/targets/android-arm64-v8a-api21"_str).as_path());
    EXPECT_NE(arm64.output(), x86.output());
    EXPECT_NE(arm64.generated_root().as_path(), x86.generated_root().as_path());
    EXPECT_NE(arm64.compile_cache_directory().as_path(), x86.compile_cache_directory().as_path());
    EXPECT_NE(arm64.cmake_work_root().as_path(), x86.cmake_work_root().as_path());
}
