#include <rstd/test/gtest.hpp>

import rstd;
import rstd.json;
import rstd.test;
import lito.driver;
import lito.system;
import lito.test.support;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito_test;
using Json      = rstd::json::Value;
using JsonMap   = rstd::json::Map;
using JsonArray = rstd::json::Array;
using PathBuf   = rstd::path::PathBuf;

auto store_json_string(ref<str> value) -> Json {
    return Json::String(String::make(value));
}

auto installed_descriptor(ref<str> version, const lito::system::HostInfo& host) -> String {
    auto host_value = JsonMap::make();
    host_value.insert(String::make("os"_str), store_json_string(host.os.as_str()));
    host_value.insert(String::make("architecture"_str),
                      store_json_string(host.architecture.as_str()));

    auto archive = JsonMap::make();
    archive.insert(String::make("url"_str),
                   store_json_string("https://example.test/llvm.tar.xz"_str));
    archive.insert(
        String::make("sha256"_str),
        store_json_string("0000000000000000000000000000000000000000000000000000000000000000"_str));
    archive.insert(String::make("size"_str), Json::Number(rstd::json::Number::from_u64(u64(1))));

    auto paths = JsonMap::make();
    paths.insert(String::make("cc"_str), store_json_string("bin/clang"_str));
    paths.insert(String::make("cxx"_str), store_json_string("bin/clang++"_str));
    paths.insert(String::make("linker"_str), store_json_string("bin/ld.lld"_str));
    paths.insert(String::make("archiver"_str), store_json_string("bin/llvm-ar"_str));
    paths.insert(String::make("strip"_str), store_json_string("bin/llvm-strip"_str));
    paths.insert(String::make("format"_str), store_json_string("bin/clang-format"_str));
    paths.insert(String::make("llvm-config"_str), store_json_string("bin/llvm-config"_str));
    paths.insert(String::make("cmake"_str), store_json_string("lib/cmake"_str));
    paths.insert(String::make("clang-cpp"_str), store_json_string("lib/libclang-cpp.so"_str));

    auto certification = JsonMap::make();
    certification.insert(String::make("compiler-version"_str), store_json_string(version));
    certification.insert(String::make("standard-library"_str), store_json_string("libstdc++"_str));
    certification.insert(String::make("exceptions"_str), Json::Bool(true));
    certification.insert(String::make("rtti"_str), Json::Bool(true));

    auto runtime = JsonMap::make();
    runtime.insert(String::make("path"_str), store_json_string("lib/libxml2.so.2.13.8"_str));
    runtime.insert(String::make("size"_str), Json::Number(rstd::json::Number::from_u64(u64(1))));
    runtime.insert(
        String::make("sha256"_str),
        store_json_string("0000000000000000000000000000000000000000000000000000000000000000"_str));
    auto license = JsonMap::make();
    license.insert(String::make("path"_str),
                   store_json_string("share/licenses/libxml2/Copyright"_str));
    license.insert(String::make("size"_str), Json::Number(rstd::json::Number::from_u64(u64(1))));
    license.insert(
        String::make("sha256"_str),
        store_json_string("0000000000000000000000000000000000000000000000000000000000000000"_str));
    auto link = JsonMap::make();
    link.insert(String::make("path"_str), store_json_string("lib/libxml2.so.2"_str));
    link.insert(String::make("target"_str), store_json_string("libxml2.so.2.13.8"_str));
    auto links = JsonArray::make();
    links.push(Json::Object(rstd::move(link)));
    auto builder = JsonMap::make();
    builder.insert(String::make("compiler-version"_str),
                   store_json_string("clang version 22.1.8"_str));
    builder.insert(String::make("compiler-target"_str),
                   store_json_string("x86_64-unknown-linux-gnu"_str));
    builder.insert(String::make("linker-family"_str), store_json_string("GNU ld"_str));
    builder.insert(String::make("linker-version"_str),
                   store_json_string("GNU ld (GNU Binutils) 2.47"_str));
    builder.insert(String::make("archiver-version"_str),
                   store_json_string("LLVM version 22.1.8"_str));
    builder.insert(String::make("strip-version"_str), store_json_string("LLVM version 22.1.8"_str));
    builder.insert(
        String::make("link-identity"_str),
        store_json_string("0000000000000000000000000000000000000000000000000000000000000000"_str));
    auto component = JsonMap::make();
    component.insert(String::make("name"_str), store_json_string("libxml2"_str));
    component.insert(String::make("version"_str), store_json_string("2.13.8"_str));
    component.insert(String::make("recipe"_str),
                     store_json_string("libxml2-2.13.8-minimal-elf-v1"_str));
    component.insert(
        String::make("recipe-digest"_str),
        store_json_string("0000000000000000000000000000000000000000000000000000000000000000"_str));
    component.insert(
        String::make("source-identity"_str),
        store_json_string("archive+https://example.test/libxml2.tar.xz#sha256:0000"_str));
    component.insert(String::make("runtime"_str), Json::Object(rstd::move(runtime)));
    component.insert(String::make("links"_str), Json::Array(rstd::move(links)));
    component.insert(String::make("license"_str), Json::Object(rstd::move(license)));
    component.insert(String::make("builder"_str), Json::Object(rstd::move(builder)));
    auto components = JsonArray::make();
    components.push(Json::Object(rstd::move(component)));

    auto root = JsonMap::make();
    root.insert(String::make("schema"_str), Json::Number(rstd::json::Number::from_u64(u64(2))));
    root.insert(String::make("kind"_str), store_json_string("lito-llvm-sdk"_str));
    root.insert(String::make("version"_str), store_json_string(version));
    root.insert(String::make("host"_str), Json::Object(rstd::move(host_value)));
    root.insert(String::make("archive"_str), Json::Object(rstd::move(archive)));
    root.insert(String::make("paths"_str), Json::Object(rstd::move(paths)));
    root.insert(String::make("certification"_str), Json::Object(rstd::move(certification)));
    root.insert(String::make("runtime-components"_str), Json::Array(rstd::move(components)));
    return rstd::json::to_string(Json::Object(rstd::move(root)));
}

class SdkStore : public ProjectFixture {};

TEST_F(SdkStore, ListMergesCatalogAndVersionDescriptorsWithoutNetworkOrTools) {
    auto directory = cache_root("sdk-list"_str);
    auto data_home = directory.join(PathBuf::from("data"_str).as_path());
    auto data_text = data_home.as_path().to_str();
    ASSERT_TRUE(data_text.is_some());
    EnvironmentVariableGuard xdg_data_home("XDG_DATA_HOME"_str, *data_text);

    auto available = lito::list_llvm_sdks();
    ASSERT_TRUE(available.is_ok());
    ASSERT_EQ(available->entries.len(), usize(1));
    EXPECT_EQ(available->entries[usize {}].version.as_str(), "22.1.8"_str);
    EXPECT_EQ(available->entries[usize {}].status, lito::SdkListStatus::Available);

    auto host = lito::system::detect_host_info();
    ASSERT_TRUE(host.is_ok());
    auto prefix = data_home.join(PathBuf::from("lito/llvm/21.0.0"_str).as_path());
    ASSERT_TRUE(rstd::fs::create_dir_all(prefix.as_path()).is_ok());
    auto descriptor = prefix.join(PathBuf::from("sdk.json"_str).as_path());
    auto text       = installed_descriptor("21.0.0"_str, *host);
    ASSERT_TRUE(rstd::fs::write(descriptor.as_path(), text.as_str().as_bytes()).is_ok());

    auto merged = lito::list_llvm_sdks();
    ASSERT_TRUE(merged.is_ok());
    ASSERT_EQ(merged->entries.len(), usize(2));
    EXPECT_EQ(merged->entries[usize {}].version.as_str(), "22.1.8"_str);
    EXPECT_EQ(merged->entries[usize {}].status, lito::SdkListStatus::Available);
    EXPECT_EQ(merged->entries[usize(1)].version.as_str(), "21.0.0"_str);
    EXPECT_EQ(merged->entries[usize(1)].status, lito::SdkListStatus::InstalledUnavailable);
    EXPECT_FALSE(
        rstd::fs::exists(
            data_home.join(PathBuf::from("lito/llvm/CACHEDIR.TAG"_str).as_path()).as_path())
            .unwrap());
}

TEST_F(SdkStore, ListReportsDescriptorConflictsAsInvalid) {
    auto directory = cache_root("sdk-invalid"_str);
    auto data_home = directory.join(PathBuf::from("data"_str).as_path());
    auto data_text = data_home.as_path().to_str();
    ASSERT_TRUE(data_text.is_some());
    EnvironmentVariableGuard xdg_data_home("XDG_DATA_HOME"_str, *data_text);

    auto prefix = data_home.join(PathBuf::from("lito/llvm/22.1.8"_str).as_path());
    ASSERT_TRUE(rstd::fs::create_dir_all(prefix.as_path()).is_ok());
    auto descriptor = prefix.join(PathBuf::from("sdk.json"_str).as_path());
    ASSERT_TRUE(rstd::fs::write(descriptor.as_path(), "{ invalid"_str.as_bytes()).is_ok());

    auto listed = lito::list_llvm_sdks();
    ASSERT_TRUE(listed.is_ok());
    ASSERT_EQ(listed->entries.len(), usize(1));
    EXPECT_EQ(listed->entries[usize {}].status, lito::SdkListStatus::Invalid);
    ASSERT_TRUE(listed->entries[usize {}].issue.is_some());
}

TEST_F(SdkStore, ListRejectsSchemaOneDescriptors) {
    auto directory = cache_root("sdk-schema-one"_str);
    auto data_home = directory.join(PathBuf::from("data"_str).as_path());
    auto data_text = data_home.as_path().to_str();
    ASSERT_TRUE(data_text.is_some());
    EnvironmentVariableGuard xdg_data_home("XDG_DATA_HOME"_str, *data_text);

    auto host = lito::system::detect_host_info();
    ASSERT_TRUE(host.is_ok());
    auto prefix = data_home.join(PathBuf::from("lito/llvm/21.0.0"_str).as_path());
    ASSERT_TRUE(rstd::fs::create_dir_all(prefix.as_path()).is_ok());
    auto value = rstd::json::from_str(installed_descriptor("21.0.0"_str, *host).as_str());
    ASSERT_TRUE(value.is_ok());
    (*value)["schema"_str] = Json::Number(rstd::json::Number::from_u64(u64(1)));
    auto descriptor        = prefix.join(PathBuf::from("sdk.json"_str).as_path());
    auto text              = rstd::json::to_string(*value);
    ASSERT_TRUE(rstd::fs::write(descriptor.as_path(), text.as_str().as_bytes()).is_ok());

    auto listed = lito::list_llvm_sdks();
    ASSERT_TRUE(listed.is_ok());
    ASSERT_EQ(listed->entries.len(), usize(2));
    EXPECT_EQ(listed->entries[usize(1)].version.as_str(), "21.0.0"_str);
    EXPECT_EQ(listed->entries[usize(1)].status, lito::SdkListStatus::Invalid);
}
