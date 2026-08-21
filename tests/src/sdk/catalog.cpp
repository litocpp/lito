#include <rstd/test/gtest.hpp>

import rstd;
import rstd.json;
import rstd.test;
import lito.system;
import lito.toolchain;

using namespace rstd::prelude;
using namespace rstd::literals;
using Json = rstd::json::Value;

auto catalog_document() -> Json {
    return rstd::json::from_str(lito::embedded_llvm_sdk_catalog_text()).unwrap();
}

auto rejects_catalog_value(ref<str> pointer, Json value) -> bool {
    auto document = catalog_document();
    auto target   = document.pointer_mut(pointer);
    if (target.is_none()) return false;
    **target  = rstd::move(value);
    auto text = rstd::json::to_string(document);
    return lito::parse_llvm_sdk_catalog(text.as_str()).is_err();
}

auto catalog_json_string(ref<str> value) -> Json {
    return Json::String(String::make(value));
}

auto json_u64(u64 value) -> Json {
    return Json::Number(rstd::json::Number::from_u64(value));
}

auto android_catalog_document() -> Json {
    return rstd::json::from_str(lito::embedded_android_ndk_catalog_text()).unwrap();
}

auto rejects_android_catalog_value(ref<str> pointer, Json value) -> bool {
    auto document = android_catalog_document();
    auto target   = document.pointer_mut(pointer);
    if (target.is_none()) return false;
    **target = rstd::move(value);
    return lito::parse_android_ndk_catalog(rstd::json::to_string(document).as_str()).is_err();
}

TEST(LlvmSdkCatalog, EmbeddedCatalogSelectsTheCertifiedCurrentHostArtifact) {
    auto catalog = lito::load_embedded_llvm_sdk_catalog();
    ASSERT_TRUE(catalog.is_ok());
    ASSERT_EQ(catalog->releases.len(), usize(1));
    EXPECT_EQ(catalog->releases[usize {}].version.text.as_str(), "22.1.8"_str);

    auto host = lito::system::HostInfo {
        .architecture = lito::system::canonical_architecture("x86_64"_str).unwrap(),
        .os           = String::make("linux"_str),
    };
    auto artifact = lito::find_llvm_sdk_artifact(catalog->releases[usize {}], host);
    ASSERT_TRUE(artifact.is_some());
    EXPECT_EQ((**artifact).archive.sha256.to_hex().as_str(),
              "df0e1ecf16caf3489a272a5eea4eec9b0d82878f6477fa309504f918a0006384"_str);
    ASSERT_EQ((**artifact).runtime_components.len(), usize(1));
    EXPECT_EQ((**artifact).runtime_components[usize {}].as_str(), "libxml2"_str);
    auto libxml2 = lito::find_llvm_sdk_runtime_component(
        *catalog, (**artifact).runtime_components[usize {}].as_str());
    ASSERT_TRUE(libxml2.is_some());
    EXPECT_EQ((**libxml2).name.as_str(), "libxml2"_str);
    EXPECT_EQ((**libxml2).version.as_str(), "2.13.8"_str);
    EXPECT_EQ(lito::llvm_sdk_runtime_recipe_name((**libxml2).recipe),
              "libxml2-2.13.8-minimal-elf-v1"_str);
    EXPECT_EQ((**libxml2).file.as_path(),
              rstd::path::PathBuf::from("lib/libxml2.so.2.13.8"_str).as_path());
    EXPECT_EQ((**libxml2).soname.as_str(), "libxml2.so.2"_str);

    host.architecture = lito::system::canonical_architecture("aarch64"_str).unwrap();
    EXPECT_TRUE(lito::find_llvm_sdk_artifact(catalog->releases[usize {}], host).is_none());
}

TEST(LlvmSdkCatalog, EmbeddedCatalogSelectsWindowsDevelopmentArchives) {
    auto catalog = lito::load_embedded_llvm_sdk_catalog();
    ASSERT_TRUE(catalog.is_ok());
    auto host = lito::system::HostInfo {
        .architecture = lito::system::canonical_architecture("x86_64"_str).unwrap(),
        .os           = String::make("windows"_str),
    };
    auto x64 = lito::find_llvm_sdk_artifact(catalog->releases[usize {}], host);
    ASSERT_TRUE(x64.is_some());
    EXPECT_EQ((**x64).archive.root.as_str().unwrap(),
              "clang+llvm-22.1.8-x86_64-pc-windows-msvc"_str);
    EXPECT_EQ((**x64).archive.sha256.to_hex().as_str(),
              "d96c2cc1736f4eb7fa43cb9bbdf56d93551a9ae0a9aadb9c99c3c3b2b712a234"_str);
    EXPECT_EQ((**x64).paths.linker.as_path(),
              rstd::path::PathBuf::from("bin/lld-link.exe"_str).as_path());
    EXPECT_EQ((**x64).paths.clang_cpp.as_path(),
              rstd::path::PathBuf::from("bin/clang-cpp.dll"_str).as_path());
    EXPECT_TRUE((**x64).runtime_components.is_empty());

    host.architecture = lito::system::canonical_architecture("aarch64"_str).unwrap();
    auto arm64        = lito::find_llvm_sdk_artifact(catalog->releases[usize {}], host);
    ASSERT_TRUE(arm64.is_some());
    EXPECT_EQ((**arm64).archive.root.as_str().unwrap(),
              "clang+llvm-22.1.8-aarch64-pc-windows-msvc"_str);
    EXPECT_EQ((**arm64).archive.sha256.to_hex().as_str(),
              "de718c58ebbc5f61d58c17b90457fcf42983bc2c4a4aba3e010d108713bfd7f1"_str);
    EXPECT_TRUE((**arm64).runtime_components.is_empty());
}

TEST(LlvmSdkCatalog, VersionsAreCanonicalAndCompareNumerically) {
    auto older = lito::parse_llvm_version("9.10.2"_str);
    auto newer = lito::parse_llvm_version("10.0.0"_str);
    ASSERT_TRUE(older.is_ok());
    ASSERT_TRUE(newer.is_ok());
    EXPECT_TRUE(lito::llvm_version_less(*older, *newer));

    constexpr ref<str> invalid[] = {
        "22"_str,      "22.1"_str,    "22.1.8.1"_str, "022.1.8"_str,
        "22.01.8"_str, "22.1.08"_str, "22.1.x"_str,   "22.1.8-rc1"_str,
    };
    for (const auto value : invalid) EXPECT_TRUE(lito::parse_llvm_version(value).is_err());
}

TEST(LlvmSdkCatalog, StrictSchemaRejectsUntrustedArtifactMetadata) {
    EXPECT_TRUE(rejects_catalog_value("/schema"_str, json_u64(u64(2))));
    EXPECT_TRUE(
        rejects_catalog_value("/kind"_str, catalog_json_string("lito-llvm-sdk-catalog"_str)));
    EXPECT_TRUE(
        rejects_catalog_value("/releases/0/version"_str, catalog_json_string("22.01.8"_str)));
    EXPECT_TRUE(rejects_catalog_value("/releases/0/upstream-tag"_str,
                                      catalog_json_string("llvmorg-22"_str)));
    EXPECT_TRUE(rejects_catalog_value("/releases/0/artifacts/0/archive/url"_str,
                                      catalog_json_string("https://"_str)));
    EXPECT_TRUE(rejects_catalog_value("/releases/0/artifacts/0/archive/url"_str,
                                      catalog_json_string("http://example.test/a"_str)));
    EXPECT_TRUE(rejects_catalog_value(
        "/releases/0/artifacts/0/archive/sha256"_str,
        catalog_json_string(
            "DF0E1ECF16CAF3489A272A5EEA4EEC9B0D82878F6477FA309504F918A0006384"_str)));
    EXPECT_TRUE(
        rejects_catalog_value("/releases/0/artifacts/0/archive/size"_str, json_u64(u64 {})));
    EXPECT_TRUE(rejects_catalog_value("/releases/0/artifacts/0/archive/root"_str,
                                      catalog_json_string("../llvm"_str)));
    EXPECT_TRUE(rejects_catalog_value("/releases/0/artifacts/0/paths/cc"_str,
                                      catalog_json_string("../bin/clang"_str)));
    EXPECT_TRUE(rejects_catalog_value("/releases/0/artifacts/0/paths/cxx"_str,
                                      catalog_json_string("bin/clang"_str)));
    EXPECT_TRUE(rejects_catalog_value("/runtime-components/0/recipe"_str,
                                      catalog_json_string("unknown-recipe"_str)));
    EXPECT_TRUE(rejects_catalog_value("/runtime-components/0/runtime/file"_str,
                                      catalog_json_string("../libxml2.so.2"_str)));
    EXPECT_TRUE(rejects_catalog_value("/runtime-components/0/runtime/soname"_str,
                                      catalog_json_string("lib/libxml2.so.2"_str)));
    EXPECT_TRUE(rejects_catalog_value("/runtime-components/0/runtime/links/0"_str,
                                      catalog_json_string("../libxml2.so.2"_str)));
    EXPECT_TRUE(rejects_catalog_value("/releases/0/artifacts/0/runtime-components/0"_str,
                                      catalog_json_string("missing"_str)));

    auto unknown              = catalog_document();
    unknown["unexpected"_str] = Json::Bool(true);
    EXPECT_TRUE(lito::parse_llvm_sdk_catalog(rstd::json::to_string(unknown).as_str()).is_err());

    auto duplicate_release = catalog_document();
    auto releases          = duplicate_release["releases"_str].as_array_mut();
    ASSERT_TRUE(releases.is_some());
    (**releases).push((**releases)[usize {}].clone());
    EXPECT_TRUE(
        lito::parse_llvm_sdk_catalog(rstd::json::to_string(duplicate_release).as_str()).is_err());

    auto duplicate_host = catalog_document();
    auto artifacts      = duplicate_host.pointer_mut("/releases/0/artifacts"_str);
    ASSERT_TRUE(artifacts.is_some());
    auto values = (**artifacts).as_array_mut();
    ASSERT_TRUE(values.is_some());
    (**values).push((**values)[usize {}].clone());
    EXPECT_TRUE(
        lito::parse_llvm_sdk_catalog(rstd::json::to_string(duplicate_host).as_str()).is_err());

    auto duplicate_component = catalog_document();
    auto components          = duplicate_component.pointer_mut("/runtime-components"_str);
    ASSERT_TRUE(components.is_some());
    auto component_values = (**components).as_array_mut();
    ASSERT_TRUE(component_values.is_some());
    (**component_values).push((**component_values)[usize {}].clone());
    EXPECT_TRUE(
        lito::parse_llvm_sdk_catalog(rstd::json::to_string(duplicate_component).as_str()).is_err());

    auto duplicate_reference = catalog_document();
    auto references =
        duplicate_reference.pointer_mut("/releases/0/artifacts/0/runtime-components"_str);
    ASSERT_TRUE(references.is_some());
    auto reference_values = (**references).as_array_mut();
    ASSERT_TRUE(reference_values.is_some());
    (**reference_values).push((**reference_values)[usize {}].clone());
    EXPECT_TRUE(
        lito::parse_llvm_sdk_catalog(rstd::json::to_string(duplicate_reference).as_str()).is_err());

    auto incompatible_component = catalog_document();
    auto windows_references =
        incompatible_component.pointer_mut("/releases/0/artifacts/1/runtime-components"_str);
    ASSERT_TRUE(windows_references.is_some());
    auto windows_values = (**windows_references).as_array_mut();
    ASSERT_TRUE(windows_values.is_some());
    (**windows_values).push(catalog_json_string("libxml2"_str));
    EXPECT_TRUE(lito::parse_llvm_sdk_catalog(rstd::json::to_string(incompatible_component).as_str())
                    .is_err());
}

TEST(AndroidNdkCatalog, EmbeddedCatalogSelectsReviewedR29Archive) {
    auto catalog = lito::load_embedded_android_ndk_catalog();
    ASSERT_TRUE(catalog.is_ok());
    ASSERT_EQ(catalog->releases.len(), usize(1));
    EXPECT_EQ(catalog->license.id.as_str(), "android-sdk-license"_str);
    EXPECT_EQ(catalog->license.sha256.to_hex().as_str(),
              "efa8d9576e4816922a4676b8b9f8040f05fa22e371fbac5042c4551b08d5a43e"_str);
    EXPECT_EQ(catalog->releases[usize {}].revision.text.as_str(), "29.0.14206865"_str);
    EXPECT_EQ(catalog->releases[usize {}].release_name.as_str(), "r29"_str);

    auto host = lito::system::HostInfo {
        .architecture = lito::system::canonical_architecture("x86_64"_str).unwrap(),
        .os           = String::make("linux"_str),
    };
    auto artifact = lito::find_android_ndk_artifact(catalog->releases[usize {}], host);
    ASSERT_TRUE(artifact.is_some());
    EXPECT_EQ((**artifact).archive.sha256.to_hex().as_str(),
              "4abbbcdc842f3d4879206e9695d52709603e52dd68d3c1fff04b3b5e7a308ecf"_str);
    EXPECT_EQ((**artifact).archive.size, u64(783549481));
    EXPECT_EQ((**artifact).archive.root.as_str().unwrap(), "android-ndk-r29"_str);
}

TEST(AndroidNdkCatalog, RevisionAndRepositorySchemaAreStrict) {
    auto revision = lito::parse_android_ndk_revision("29.0.14206865"_str);
    ASSERT_TRUE(revision.is_ok());
    EXPECT_EQ(revision->major, u64(29));
    EXPECT_EQ(revision->minor, u64 {});
    EXPECT_EQ(revision->build, u64(14206865));
    constexpr ref<str> invalid[] = {
        "r29"_str,     "29"_str,      "29.0"_str,    "29.0.1.0"_str,
        "029.0.1"_str, "29.00.1"_str, "29.0.01"_str, "29.0.x"_str,
    };
    for (const auto value : invalid) EXPECT_TRUE(lito::parse_android_ndk_revision(value).is_err());

    EXPECT_TRUE(rejects_android_catalog_value("/schema"_str, json_u64(u64(2))));
    EXPECT_TRUE(rejects_android_catalog_value("/kind"_str,
                                              catalog_json_string("lito-android-ndk-catalog"_str)));
    EXPECT_TRUE(
        rejects_android_catalog_value("/releases/0/revision"_str, catalog_json_string("r29"_str)));
    EXPECT_TRUE(rejects_android_catalog_value(
        "/releases/0/artifacts/0/archive/url"_str,
        catalog_json_string("https://example.test/android-ndk.zip"_str)));
    EXPECT_TRUE(rejects_android_catalog_value(
        "/releases/0/artifacts/0/archive/sha256"_str,
        catalog_json_string(
            "4ABBBCDC842F3D4879206E9695D52709603E52DD68D3C1FFF04B3B5E7A308ECF"_str)));
    EXPECT_TRUE(rejects_android_catalog_value("/releases/0/artifacts/0/archive/root"_str,
                                              catalog_json_string("../ndk"_str)));
    EXPECT_TRUE(rejects_android_catalog_value(
        "/license/url"_str, catalog_json_string("https://example.test/license"_str)));

    auto duplicate_release = android_catalog_document();
    auto releases          = duplicate_release["releases"_str].as_array_mut();
    ASSERT_TRUE(releases.is_some());
    (**releases).push((**releases)[usize {}].clone());
    EXPECT_TRUE(lito::parse_android_ndk_catalog(rstd::json::to_string(duplicate_release).as_str())
                    .is_err());

    auto duplicate_host = android_catalog_document();
    auto artifacts      = duplicate_host.pointer_mut("/releases/0/artifacts"_str);
    ASSERT_TRUE(artifacts.is_some());
    auto artifact_values = (**artifacts).as_array_mut();
    ASSERT_TRUE(artifact_values.is_some());
    (**artifact_values).push((**artifact_values)[usize {}].clone());
    EXPECT_TRUE(
        lito::parse_android_ndk_catalog(rstd::json::to_string(duplicate_host).as_str()).is_err());
}
