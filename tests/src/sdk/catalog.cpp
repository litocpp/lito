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
    EXPECT_EQ((**artifact).archive.sha256.as_str(),
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
}
