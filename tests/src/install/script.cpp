#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito;
import lito.lock;
import lito.package;
import lito.package.graph_contract;
import lito.workspace.contract;
import lito.workspace.resolver;
import lito.platform;
import lito.dependency;
import lito.dependency.cmake;
import lito.source;
import lito.manifest;
import lito.toolchain;
import lito.build.discovery;
import lito.build.layout;
import lito.system.environment;
import lito.system.process;
import lito.system.storage;
import lito.test.support;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito_test;
using PathBuf = rstd::path::PathBuf;

TEST(Install, InstallOnlyManifestOwnsItsConventionalScript) {
    auto directory = fixture_path("install/manifest/install-only"_str);
    auto loaded    = lito::load_package_manifest(directory.as_path());
    ASSERT_TRUE(loaded.is_ok());
    ASSERT_TRUE(loaded->install_script.is_some());
    EXPECT_EQ(loaded->install_script->as_path(),
              directory.join(PathBuf::from("install.lua"_str).as_path()).as_path());
    ASSERT_TRUE(loaded->version.value.is_some());
    EXPECT_EQ(loaded->version.value->as_str(), "1.2.3"_str);

    auto missing = lito::load_package_manifest(
        fixture_path("install/manifest/install-only-missing-version"_str).as_path());
    ASSERT_TRUE(missing.is_err());
    EXPECT_TRUE(error_chain_text(missing.unwrap_err()).as_str().contains("version"_str));
}

TEST(Install, InstallScriptProducesAnOwnedRecipeOnce) {
    auto directory = fixture_path("install/script/recipe"_str);
    auto manifest  = lito::load_package_manifest(directory.as_path());
    ASSERT_TRUE(manifest.is_ok());
    ASSERT_TRUE(manifest->install_script.is_some());
    auto recipe = lito::execute_install_script(install_script_input(*manifest),
                                               lito::InstallScriptContext {
                                                   .profile = String::make("release"_str),
                                                   .target  = String::make("x86_64-test-linux"_str),
                                                   .target_arch = String::make("x86_64"_str),
                                               });
    ASSERT_TRUE(recipe.is_ok());
    EXPECT_EQ(recipe->owner.as_str(), "fixture-install-script"_str);
    ASSERT_EQ(recipe->artifacts.len(), usize(1));
    EXPECT_EQ(recipe->artifacts[usize {}].target.package.as_str(), "fixture-install-script"_str);
    EXPECT_EQ(recipe->artifacts[usize {}].destination.as_path(),
              PathBuf::from("bin/producer"_str).as_path());
    ASSERT_EQ(recipe->external_assets.len(), usize(1));
    ASSERT_EQ(recipe->files.len(), usize(1));
    ASSERT_EQ(recipe->templates.len(), usize(1));
    ASSERT_EQ(recipe->inventories.len(), usize(1));
    auto fragment = recipe->templates[usize {}].values.get("FRAGMENT"_str);
    ASSERT_TRUE(fragment.is_some());
    EXPECT_EQ((**fragment).string(), "fixture-install-script 2.4.6\n"_str);

    constexpr ref<str> binding_errors[] = {
        "install/script/cross-package"_str,
        "install/script/duplicate"_str,
        "install/script/unknown-field"_str,
        "install/script/wrong-type"_str,
    };
    for (auto case_path : binding_errors) {
        auto invalid = lito::load_package_manifest(fixture_path(case_path).as_path());
        ASSERT_TRUE(invalid.is_ok());
        auto executed =
            lito::execute_install_script(install_script_input(*invalid),
                                         lito::InstallScriptContext {
                                             .profile     = String::make("release"_str),
                                             .target      = String::make("x86_64-test-linux"_str),
                                             .target_arch = String::make("x86_64"_str),
                                         });
        ASSERT_TRUE(executed.is_err());
        EXPECT_TRUE(executed.unwrap_err().is_Binding());
    }

    auto missing =
        lito::load_package_manifest(fixture_path("install/script/missing"_str).as_path());
    ASSERT_TRUE(missing.is_ok());
    auto unregistered =
        lito::execute_install_script(install_script_input(*missing),
                                     lito::InstallScriptContext {
                                         .profile     = String::make("release"_str),
                                         .target      = String::make("x86_64-test-linux"_str),
                                         .target_arch = String::make("x86_64"_str),
                                     });
    ASSERT_TRUE(unregistered.is_err());
    EXPECT_TRUE(unregistered.unwrap_err().is_Message());
}
