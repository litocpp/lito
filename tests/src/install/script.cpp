#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito.driver;
import lito.core;
import lito.system;
import lito.tools.cmake;
import lito.toolchain;
import lito.test.support;

using namespace rstd::prelude;
using namespace lito::system;
using namespace rstd::literals;
using namespace lito_test;
using PathBuf = rstd::path::PathBuf;

class InstallScript : public ProjectFixture {};

TEST_F(InstallScript, InstallOnlyManifestOwnsItsConventionalScript) {
    constexpr ProjectFile files[] = {
        { "lito.toml"_str,
          "[package]\nname = \"fixture-install-only\"\nversion = \"1.2.3\"\n"_str },
        { "install.lua"_str, "lito.install({})\n"_str },
    };
    auto project = materialize("install-only-manifest"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto directory = project->root.clone();
    auto loaded    = lito::manifest::load_package_manifest(directory.as_path());
    ASSERT_TRUE(loaded.is_ok());
    ASSERT_TRUE(loaded->install_script.is_some());
    EXPECT_EQ(loaded->install_script->as_path(),
              directory.join(PathBuf::from("install.lua"_str).as_path()).as_path());
    ASSERT_TRUE(loaded->version.value.is_some());
    EXPECT_EQ(loaded->version.value->as_str(), "1.2.3"_str);

    constexpr ProjectFile missing_files[] = {
        { "lito.toml"_str, "[package]\nname = \"fixture-install-only-missing-version\"\n"_str },
        { "install.lua"_str, "lito.install({})\n"_str },
    };
    auto missing_project = materialize("install-only-missing-version"_str, missing_files);
    ASSERT_TRUE(missing_project.is_ok());
    auto missing = lito::manifest::load_package_manifest(missing_project->root.as_path());
    ASSERT_TRUE(missing.is_err());
    EXPECT_TRUE(error_chain_text(missing.unwrap_err()).as_str().contains("version"_str));
}

TEST_F(InstallScript, InstallScriptProducesAnOwnedRecipeOnce) {
    auto environment =
        EnvironmentVariableGuard("LITO_TEST_INSTALL_ENVIRONMENT"_str, "fixture-environment"_str);
    auto empty_environment =
        EnvironmentVariableGuard("LITO_TEST_INSTALL_ENVIRONMENT_EMPTY"_str, ""_str);
    auto unset_environment = EnvironmentVariableGuard("LITO_TEST_INSTALL_ENVIRONMENT_UNSET"_str);
    constexpr ProjectFile recipe_files[] = {
        { "lito.toml"_str,
          "[package]\nname = \"fixture-install-script\"\nversion = \"2.4.6\"\n"_str },
        { "fragment.in"_str, "@NAME@ @VERSION@\n"_str },
        { "manifest.in"_str,
          "@FRAGMENT@@RAW@@PROFILE@ @TARGET@ @ARCH@ @ENVIRONMENT@ @ENVIRONMENT_EMPTY@ @ENVIRONMENT_UNSET@ @ENVIRONMENT_UNSET_COUNT@\n"_str },
        { "resource.txt"_str, "resource @literal@\n"_str },
        { "install.lua"_str, R"lua(local raw = lito.read_file("resource.txt")
local rendered = lito.render_template({
    input = "fragment.in",
    values = { NAME = lito.package_name, VERSION = lito.package_version },
})
lito.install({
    artifacts = {{
        target = { kind = "bin", name = "producer" },
        destination = "bin/producer",
        runtime_search = {{ external_asset = { dependency = "runtime", set = "files" } }},
    }},
    target_runtimes = {{ name = "libc++_shared.so", destination = "lib/libc++_shared.so" }},
    external_assets = {{
        dependency = "runtime", set = "files", destination = "lib/runtime",
        strip = { mode = "symbols", files = { "runtime.so" } },
    }},
    files = {{ source = "resource.txt", destination = "share/fixture/resource.txt" }},
    templates = {{
        input = "manifest.in",
        destination = "share/fixture/manifest.txt",
        values = {
            FRAGMENT = rendered, RAW = raw, PROFILE = lito.profile, TARGET = lito.target,
            ARCH = lito.target_arch,
            ENVIRONMENT = lito.env("LITO_TEST_INSTALL_ENVIRONMENT"),
            ENVIRONMENT_EMPTY = lito.env("LITO_TEST_INSTALL_ENVIRONMENT_EMPTY") == "",
            ENVIRONMENT_UNSET = lito.env("LITO_TEST_INSTALL_ENVIRONMENT_UNSET") == nil,
            ENVIRONMENT_UNSET_COUNT = select("#", lito.env("LITO_TEST_INSTALL_ENVIRONMENT_UNSET")),
        },
    }},
    inventories = {{ destination = "share/fixture/files.txt",
                     relative_to = lito.env("LITO_TEST_INSTALL_ENVIRONMENT_EMPTY") or "" }},
})
)lua"_str },
    };
    auto project = materialize("install-script-recipe"_str, recipe_files);
    ASSERT_TRUE(project.is_ok());
    auto directory = project->root.clone();
    auto manifest  = lito::manifest::load_package_manifest(directory.as_path());
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
    ASSERT_EQ(recipe->artifacts[usize {}].runtime_search.len(), usize(1));
    EXPECT_EQ(recipe->artifacts[usize {}].runtime_search[usize {}].dependency.as_str(),
              "runtime"_str);
    ASSERT_EQ(recipe->target_runtimes.len(), usize(1));
    EXPECT_EQ(recipe->target_runtimes[usize {}].name.as_str(), "libc++_shared.so"_str);
    EXPECT_EQ(recipe->target_runtimes[usize {}].destination.as_path(),
              PathBuf::from("lib/libc++_shared.so"_str).as_path());
    ASSERT_EQ(recipe->external_assets.len(), usize(1));
    ASSERT_TRUE(recipe->external_assets[usize {}].strip.is_some());
    EXPECT_EQ(recipe->external_assets[usize {}].strip->mode, lito::artifact::StripMode::Symbols);
    ASSERT_EQ(recipe->external_assets[usize {}].strip->files.len(), usize(1));
    ASSERT_EQ(recipe->files.len(), usize(1));
    ASSERT_EQ(recipe->templates.len(), usize(1));
    ASSERT_EQ(recipe->inventories.len(), usize(1));
    EXPECT_TRUE(recipe->inventories[usize {}].relative_to.is_empty());
    auto fragment = recipe->templates[usize {}].values.get("FRAGMENT"_str);
    ASSERT_TRUE(fragment.is_some());
    EXPECT_EQ((**fragment).string(), "fixture-install-script 2.4.6\n"_str);
    auto raw = recipe->templates[usize {}].values.get("RAW"_str);
    ASSERT_TRUE(raw.is_some());
    EXPECT_EQ((**raw).string(), "resource @literal@\n"_str);
    auto environment_value = recipe->templates[usize {}].values.get("ENVIRONMENT"_str);
    ASSERT_TRUE(environment_value.is_some());
    EXPECT_EQ((**environment_value).string(), "fixture-environment"_str);
    auto environment_empty = recipe->templates[usize {}].values.get("ENVIRONMENT_EMPTY"_str);
    ASSERT_TRUE(environment_empty.is_some());
#if defined(_WIN32)
    EXPECT_FALSE((**environment_empty).boolean());
#else
    EXPECT_TRUE((**environment_empty).boolean());
#endif
    auto environment_unset = recipe->templates[usize {}].values.get("ENVIRONMENT_UNSET"_str);
    ASSERT_TRUE(environment_unset.is_some());
    EXPECT_TRUE((**environment_unset).boolean());
    auto environment_unset_count =
        recipe->templates[usize {}].values.get("ENVIRONMENT_UNSET_COUNT"_str);
    ASSERT_TRUE(environment_unset_count.is_some());
    EXPECT_EQ((**environment_unset_count).integer(), i64(1));

    struct BindingError {
        ref<str> name;
        ref<str> script;
    };
    constexpr BindingError binding_errors[] = {
        { "cross-package"_str, R"lua(lito.install({ artifacts = {{
    package = "another-package",
    target = { kind = "bin", name = "tool" },
    destination = "bin/tool",
}} })
)lua"_str },
        { "duplicate"_str, "lito.install({})\nlito.install({})\n"_str },
        { "unknown-field"_str, "lito.install({ unsupported = true })\n"_str },
        { "wrong-type"_str, "lito.install({ files = { \"invalid\" } })\n"_str },
        { "empty-runtime-search"_str,
          R"lua(lito.install({ artifacts = {{
    target = { kind = "bin", name = "tool" },
    destination = "bin/tool",
    runtime_search = {},
}} })
)lua"_str },
        { "invalid-strip-mode"_str,
          R"lua(lito.install({ external_assets = {{
    dependency = "runtime", set = "files", destination = "lib/runtime",
    strip = { mode = "none", files = { "runtime.so" } },
}} })
)lua"_str },
        { "unsafe-strip-path"_str,
          R"lua(lito.install({ external_assets = {{
    dependency = "runtime", set = "files", destination = "lib/runtime",
    strip = { mode = "symbols", files = { "../runtime.so" } },
}} })
)lua"_str },
    };
    for (const auto& binding_error : binding_errors) {
        auto manifest_text =
            rstd::format("[package]\nname = \"fixture-install-script-{}\"\nversion = \"1.0.0\"\n",
                         binding_error.name);
        const ProjectFile invalid_files[] = {
            { "lito.toml"_str, manifest_text.as_str() },
            { "install.lua"_str, binding_error.script },
        };
        auto invalid_project = materialize(binding_error.name, invalid_files);
        ASSERT_TRUE(invalid_project.is_ok());
        auto invalid = lito::manifest::load_package_manifest(invalid_project->root.as_path());
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

    constexpr ProjectFile missing_files[] = {
        { "lito.toml"_str,
          "[package]\nname = \"fixture-install-script-missing\"\nversion = \"1.0.0\"\n"_str },
        { "install.lua"_str, "local value = lito.package_name\n"_str },
    };
    auto missing_project = materialize("install-script-missing"_str, missing_files);
    ASSERT_TRUE(missing_project.is_ok());
    auto missing = lito::manifest::load_package_manifest(missing_project->root.as_path());
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
