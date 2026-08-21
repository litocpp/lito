#include <rstd/test/gtest.hpp>
#include <rstd/macro.hpp>

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

auto installed_descriptor_with_files(ref<str>                      version,
                                     const lito::system::HostInfo& host,
                                     u64                           runtime_size,
                                     ref<str>                      runtime_sha256,
                                     u64                           license_size,
                                     ref<str>                      license_sha256) -> String {
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
    runtime.insert(String::make("size"_str),
                   Json::Number(rstd::json::Number::from_u64(runtime_size)));
    runtime.insert(String::make("sha256"_str), store_json_string(runtime_sha256));
    auto license = JsonMap::make();
    license.insert(String::make("path"_str),
                   store_json_string("share/licenses/libxml2/Copyright"_str));
    license.insert(String::make("size"_str),
                   Json::Number(rstd::json::Number::from_u64(license_size)));
    license.insert(String::make("sha256"_str), store_json_string(license_sha256));
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

auto installed_descriptor(ref<str> version, const lito::system::HostInfo& host) -> String {
    return installed_descriptor_with_files(
        version,
        host,
        u64(1),
        "0000000000000000000000000000000000000000000000000000000000000000"_str,
        u64(1),
        "0000000000000000000000000000000000000000000000000000000000000000"_str);
}

auto installed_descriptor_without_runtime(ref<str> version, const lito::system::HostInfo& host)
    -> String {
    auto value = rstd::json::from_str(installed_descriptor(version, host).as_str()).unwrap();
    value["runtime-components"_str] = Json::Array(JsonArray::make());
    return rstd::json::to_string(value);
}

auto materialize_installed_sdk(ref<rstd::path::Path>         data_home,
                               ref<str>                      version,
                               const lito::system::HostInfo& host) -> rstd::io::Result<PathBuf> {
    auto prefix = PathBuf::from(data_home).join(
        PathBuf::from(rstd::format("lito/llvm/{}", version)).as_path());
    rstd_try(rstd::fs::create_dir_all(prefix.join(PathBuf::from("bin"_str).as_path()).as_path()));
    rstd_try(
        rstd::fs::create_dir_all(prefix.join(PathBuf::from("lib/cmake"_str).as_path()).as_path()));
    rstd_try(rstd::fs::create_dir_all(
        prefix.join(PathBuf::from("share/licenses/libxml2"_str).as_path()).as_path()));
    constexpr ref<str> tools[] = {
        "bin/clang"_str,       "bin/clang++"_str,         "bin/ld.lld"_str,
        "bin/llvm-ar"_str,     "bin/llvm-strip"_str,      "bin/clang-format"_str,
        "bin/llvm-config"_str, "lib/libclang-cpp.so"_str,
    };
    for (const auto tool : tools) {
        auto path = prefix.join(PathBuf::from(tool).as_path());
        rstd_try(rstd::fs::write(path.as_path(), "tool\n"_str.as_bytes()));
    }
    auto runtime = prefix.join(PathBuf::from("lib/libxml2.so.2.13.8"_str).as_path());
    auto license = prefix.join(PathBuf::from("share/licenses/libxml2/Copyright"_str).as_path());
    constexpr auto runtime_contents = "runtime\n"_str;
    constexpr auto license_contents = "license\n"_str;
    rstd_try(rstd::fs::write(runtime.as_path(), runtime_contents.as_bytes()));
    rstd_try(rstd::fs::write(license.as_path(), license_contents.as_bytes()));
    auto link = prefix.join(PathBuf::from("lib/libxml2.so.2"_str).as_path());
    rstd_try(rstd::fs::soft_link(PathBuf::from("libxml2.so.2.13.8"_str).as_path(), link.as_path()));
    auto text =
        installed_descriptor_with_files(version,
                                        host,
                                        u64(runtime_contents.len().to_primitive()),
                                        rstd::crypto::sha256_hex(runtime_contents).as_str(),
                                        u64(license_contents.len().to_primitive()),
                                        rstd::crypto::sha256_hex(license_contents).as_str());
    auto descriptor = prefix.join(PathBuf::from("sdk.json"_str).as_path());
    rstd_try(rstd::fs::write(descriptor.as_path(), text.as_str().as_bytes()));
    return Ok(rstd::move(prefix));
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
    auto text       = installed_descriptor_without_runtime("21.0.0"_str, *host);
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

TEST_F(SdkStore, ActivateProvidesProjectDefaultsAndDeactivateKeepsExistingLeaseAlive) {
    auto directory = cache_root("sdk-activate"_str);
    auto data_home = directory.join(PathBuf::from("data"_str).as_path());
    auto data_text = data_home.as_path().to_str();
    ASSERT_TRUE(data_text.is_some());
    EnvironmentVariableGuard xdg_data_home("XDG_DATA_HOME"_str, *data_text);

    auto host = lito::system::detect_host_info();
    ASSERT_TRUE(host.is_ok());
    auto prefix = materialize_installed_sdk(data_home.as_path(), "21.0.0"_str, *host);
    ASSERT_TRUE(prefix.is_ok());

    auto activated = lito::activate_llvm_sdk(lito::SdkActivateRequest {
        .version = String::make("21.0.0"_str),
    });
    ASSERT_TRUE(activated.is_ok());
    EXPECT_FALSE(activated->unchanged);
    EXPECT_EQ(activated->prefix.as_path(), prefix->as_path());
    auto repeated = lito::activate_llvm_sdk(lito::SdkActivateRequest {
        .version = String::make("21.0.0"_str),
    });
    ASSERT_TRUE(repeated.is_ok());
    EXPECT_TRUE(repeated->unchanged);

    auto acquired = lito::acquire_active_llvm_sdk();
    ASSERT_TRUE(acquired.is_ok());
    ASSERT_TRUE(acquired->is_some());
    auto lease        = rstd::move(acquired).unwrap().unwrap();
    auto defaults     = lease.project_defaults();
    auto version_lock = data_home.join(PathBuf::from("lito/llvm/.locks/21.0.0.lock"_str).as_path());
    auto competing_file =
        rstd::fs::OpenOptions::make().read(true).write(true).open(version_lock.as_path());
    ASSERT_TRUE(competing_file.is_ok());
    auto competing = rstd::fs::FileLock::try_acquire(rstd::move(competing_file).unwrap(),
                                                     rstd::fs::FileLockMode::Exclusive);
    ASSERT_TRUE(competing.is_ok());
    EXPECT_TRUE(competing->is_none());
    EXPECT_EQ(defaults.toolchain.cc.as_path(),
              prefix->join(PathBuf::from("bin/clang"_str).as_path()).as_path());
    EXPECT_EQ(defaults.toolchain.cxx.as_path(),
              prefix->join(PathBuf::from("bin/clang++"_str).as_path()).as_path());
    EXPECT_EQ(defaults.toolchain.ld.as_path(),
              prefix->join(PathBuf::from("bin/ld.lld"_str).as_path()).as_path());
    EXPECT_EQ(defaults.toolchain.ar.as_path(),
              prefix->join(PathBuf::from("bin/llvm-ar"_str).as_path()).as_path());
    EXPECT_EQ(defaults.tools.strip.as_path(),
              prefix->join(PathBuf::from("bin/llvm-strip"_str).as_path()).as_path());
    EXPECT_EQ(defaults.tools.clang_format.as_path(),
              prefix->join(PathBuf::from("bin/clang-format"_str).as_path()).as_path());

    auto listed = lito::list_llvm_sdks();
    ASSERT_TRUE(listed.is_ok());
    auto active = false;
    for (const auto& entry : listed->entries) {
        if (entry.version == "21.0.0"_str) active = entry.active;
    }
    EXPECT_TRUE(active);
    EXPECT_TRUE(listed->active_issue.is_none());

    auto deactivated = lito::deactivate_llvm_sdk();
    ASSERT_TRUE(deactivated.is_ok());
    EXPECT_FALSE(deactivated->unchanged);
    ASSERT_TRUE(deactivated->version.is_some());
    EXPECT_EQ(*deactivated->version, "21.0.0"_str);
    EXPECT_EQ(lease.version(), "21.0.0"_str);
    auto after = lito::acquire_active_llvm_sdk();
    ASSERT_TRUE(after.is_ok());
    EXPECT_TRUE(after->is_none());
    auto repeated_deactivate = lito::deactivate_llvm_sdk();
    ASSERT_TRUE(repeated_deactivate.is_ok());
    EXPECT_TRUE(repeated_deactivate->unchanged);

    ASSERT_TRUE(lito::activate_llvm_sdk(lito::SdkActivateRequest {
                                            .version = String::make("21.0.0"_str),
                                        })
                    .is_ok());
    auto descriptor = prefix->join(PathBuf::from("sdk.json"_str).as_path());
    auto contents   = rstd::fs::read_to_string(descriptor.as_path());
    ASSERT_TRUE(contents.is_ok());
    contents->push_ascii('\n');
    ASSERT_TRUE(rstd::fs::write(descriptor.as_path(), contents->as_str().as_bytes()).is_ok());
    EXPECT_TRUE(lito::acquire_active_llvm_sdk().is_err());
    auto mismatched = lito::list_llvm_sdks();
    ASSERT_TRUE(mismatched.is_ok());
    EXPECT_TRUE(mismatched->active_issue.is_some());
    EXPECT_TRUE(lito::deactivate_llvm_sdk().is_ok());
}

TEST_F(SdkStore, DeactivateRepairsInvalidOrdinaryStateButRejectsSymlinks) {
    auto directory = cache_root("sdk-deactivate-invalid"_str);
    auto data_home = directory.join(PathBuf::from("data"_str).as_path());
    auto data_text = data_home.as_path().to_str();
    ASSERT_TRUE(data_text.is_some());
    EnvironmentVariableGuard xdg_data_home("XDG_DATA_HOME"_str, *data_text);

    auto root = data_home.join(PathBuf::from("lito/llvm"_str).as_path());
    ASSERT_TRUE(rstd::fs::create_dir_all(root.as_path()).is_ok());
    auto active = root.join(PathBuf::from(".active.json"_str).as_path());
    ASSERT_TRUE(rstd::fs::write(active.as_path(), "{ invalid"_str.as_bytes()).is_ok());
    auto listed = lito::list_llvm_sdks();
    ASSERT_TRUE(listed.is_ok());
    ASSERT_TRUE(listed->active_issue.is_some());
    EXPECT_TRUE(lito::acquire_active_llvm_sdk().is_err());

    auto repaired = lito::deactivate_llvm_sdk();
    ASSERT_TRUE(repaired.is_ok());
    EXPECT_TRUE(repaired->invalid_state);
    EXPECT_FALSE(rstd::fs::exists(active.as_path()).unwrap());

    auto target = root.join(PathBuf::from("activation-target.json"_str).as_path());
    ASSERT_TRUE(rstd::fs::write(target.as_path(), "{}"_str.as_bytes()).is_ok());
    ASSERT_TRUE(rstd::fs::soft_link(target.as_path(), active.as_path()).is_ok());
    EXPECT_TRUE(lito::deactivate_llvm_sdk().is_err());
    EXPECT_TRUE(rstd::fs::exists(target.as_path()).unwrap());
}

TEST_F(SdkStore, UninstallClearsActiveStateAndRemovesInvalidOwnedEntries) {
    auto directory = cache_root("sdk-uninstall"_str);
    auto data_home = directory.join(PathBuf::from("data"_str).as_path());
    auto data_text = data_home.as_path().to_str();
    ASSERT_TRUE(data_text.is_some());
    EnvironmentVariableGuard xdg_data_home("XDG_DATA_HOME"_str, *data_text);

    auto host = lito::system::detect_host_info();
    ASSERT_TRUE(host.is_ok());
    auto prefix = materialize_installed_sdk(data_home.as_path(), "21.0.0"_str, *host);
    ASSERT_TRUE(prefix.is_ok());
    ASSERT_TRUE(lito::activate_llvm_sdk(lito::SdkActivateRequest {
                                            .version = String::make("21.0.0"_str),
                                        })
                    .is_ok());
    auto uninstalled = lito::uninstall_llvm_sdk(lito::SdkUninstallRequest {
        .version = String::make("21.0.0"_str),
    });
    ASSERT_TRUE(uninstalled.is_ok());
    EXPECT_TRUE(uninstalled->was_active);
    EXPECT_FALSE(uninstalled->invalid_entry);
    EXPECT_FALSE(rstd::fs::exists(prefix->as_path()).unwrap());
    EXPECT_TRUE(lito::acquire_active_llvm_sdk().unwrap().is_none());

    auto invalid = data_home.join(PathBuf::from("lito/llvm/20.0.0"_str).as_path());
    ASSERT_TRUE(rstd::fs::create_dir_all(invalid.as_path()).is_ok());
    ASSERT_TRUE(rstd::fs::write(invalid.join(PathBuf::from("partial"_str).as_path()).as_path(),
                                "partial"_str.as_bytes())
                    .is_ok());
    auto removed_invalid = lito::uninstall_llvm_sdk(lito::SdkUninstallRequest {
        .version = String::make("20.0.0"_str),
    });
    ASSERT_TRUE(removed_invalid.is_ok());
    EXPECT_TRUE(removed_invalid->invalid_entry);
    EXPECT_FALSE(rstd::fs::exists(invalid.as_path()).unwrap());

    auto target = data_home.join(PathBuf::from("outside-sdk"_str).as_path());
    ASSERT_TRUE(rstd::fs::create_dir_all(target.as_path()).is_ok());
    auto linked = data_home.join(PathBuf::from("lito/llvm/19.0.0"_str).as_path());
    ASSERT_TRUE(rstd::fs::soft_link(target.as_path(), linked.as_path()).is_ok());
    EXPECT_TRUE(lito::uninstall_llvm_sdk(lito::SdkUninstallRequest {
                                             .version = String::make("19.0.0"_str),
                                         })
                    .is_err());
    EXPECT_TRUE(rstd::fs::exists(target.as_path()).unwrap());

    auto tombstone = data_home.join(PathBuf::from("lito/llvm/.removing/18.0.0"_str).as_path());
    ASSERT_TRUE(rstd::fs::create_dir_all(tombstone.as_path()).is_ok());
    ASSERT_TRUE(rstd::fs::write(tombstone.join(PathBuf::from("partial"_str).as_path()).as_path(),
                                "partial"_str.as_bytes())
                    .is_ok());
    auto recovered = lito::uninstall_llvm_sdk(lito::SdkUninstallRequest {
        .version = String::make("18.0.0"_str),
    });
    ASSERT_TRUE(recovered.is_ok());
    EXPECT_TRUE(recovered->recovered);
    EXPECT_FALSE(rstd::fs::exists(tombstone.as_path()).unwrap());
}

TEST_F(SdkStore, AndroidInstallRequiresLicenseAcceptanceBeforeStoreMutation) {
    auto directory = cache_root("android-license"_str);
    auto data_home = directory.join(PathBuf::from("data"_str).as_path());
    auto data_text = data_home.as_path().to_str();
    ASSERT_TRUE(data_text.is_some());
    EnvironmentVariableGuard xdg_data_home("XDG_DATA_HOME"_str, *data_text);

    auto rejected = lito::install_android_ndk(lito::AndroidNdkInstallRequest {
        .version = String::make("29.0.14206865"_str),
    });
    ASSERT_TRUE(rejected.is_err());
    EXPECT_TRUE(
        rstd::format("{}", rejected.unwrap_err()).as_str().contains("--accept-license"_str));
    auto store = data_home.join(PathBuf::from("lito/android-ndk"_str).as_path());
    EXPECT_FALSE(rstd::fs::exists(store.as_path()).unwrap());
}

TEST_F(SdkStore, AndroidUninstallOnlyRemovesOwnedStoreEntries) {
    auto directory = cache_root("android-uninstall"_str);
    auto data_home = directory.join(PathBuf::from("data"_str).as_path());
    auto data_text = data_home.as_path().to_str();
    ASSERT_TRUE(data_text.is_some());
    EnvironmentVariableGuard xdg_data_home("XDG_DATA_HOME"_str, *data_text);

    auto invalid = data_home.join(PathBuf::from("lito/android-ndk/28.0.13004108"_str).as_path());
    ASSERT_TRUE(rstd::fs::create_dir_all(invalid.as_path()).is_ok());
    ASSERT_TRUE(rstd::fs::write(invalid.join(PathBuf::from("partial"_str).as_path()).as_path(),
                                "partial"_str.as_bytes())
                    .is_ok());
    auto removed = lito::uninstall_android_ndk(lito::SdkUninstallRequest {
        .version = String::make("28.0.13004108"_str),
    });
    ASSERT_TRUE(removed.is_ok());
    EXPECT_TRUE(removed->invalid_entry);
    EXPECT_FALSE(rstd::fs::exists(invalid.as_path()).unwrap());

    auto outside = data_home.join(PathBuf::from("outside-android-ndk"_str).as_path());
    ASSERT_TRUE(rstd::fs::create_dir_all(outside.as_path()).is_ok());
    auto linked = data_home.join(PathBuf::from("lito/android-ndk/27.2.12479018"_str).as_path());
    ASSERT_TRUE(rstd::fs::soft_link(outside.as_path(), linked.as_path()).is_ok());
    EXPECT_TRUE(lito::uninstall_android_ndk(lito::SdkUninstallRequest {
                                                .version = String::make("27.2.12479018"_str),
                                            })
                    .is_err());
    EXPECT_TRUE(rstd::fs::exists(outside.as_path()).unwrap());

    auto tombstone =
        data_home.join(PathBuf::from("lito/android-ndk/.removing/26.1.10909125"_str).as_path());
    ASSERT_TRUE(rstd::fs::create_dir_all(tombstone.as_path()).is_ok());
    auto recovered = lito::uninstall_android_ndk(lito::SdkUninstallRequest {
        .version = String::make("26.1.10909125"_str),
    });
    ASSERT_TRUE(recovered.is_ok());
    EXPECT_TRUE(recovered->recovered);
    EXPECT_FALSE(rstd::fs::exists(tombstone.as_path()).unwrap());
}
