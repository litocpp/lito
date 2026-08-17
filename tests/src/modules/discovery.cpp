#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito.driver;
import lito.core;
import lito.test.support;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito_test;
using PathBuf = rstd::path::PathBuf;

class ModuleDiscovery : public ProjectFixture {};

using ModuleProjectFactory = lito::source::SourceTreeResult<lito::source::SourceTree> (*)();

auto cache_long_path_tree() -> lito::source::SourceTreeResult<lito::source::SourceTree> {
    const ProjectFile files[] = {
        { "lito.toml"_str, R"module([package]
name = "fixture-cache-long-path"
version = "0.1.0"

[[bin]]
link-stdlib = false
name = "fixture-cache-long-path"
sources = ["src/segment01_abcdefghijklmnopqrstuvwxyz/segment02_abcdefghijklmnopqrstuvwxyz/segment03_abcdefghijklmnopqrstuvwxyz/segment04_abcdefghijklmnopqrstuvwxyz/segment05_abcdefghijklmnopqrstuvwxyz/segment06_abcdefghijklmnopqrstuvwxyz/segment07_abcdefghijklmnopqrstuvwxyz/segment08_abcdefghijklmnopqrstuvwxyz/unit.cpp"]
)module"_str },
        { "src/segment01_abcdefghijklmnopqrstuvwxyz/segment02_abcdefghijklmnopqrstuvwxyz/segment03_abcdefghijklmnopqrstuvwxyz/segment04_abcdefghijklmnopqrstuvwxyz/segment05_abcdefghijklmnopqrstuvwxyz/segment06_abcdefghijklmnopqrstuvwxyz/segment07_abcdefghijklmnopqrstuvwxyz/segment08_abcdefghijklmnopqrstuvwxyz/unit.cpp"_str,
          R"module(auto main() -> int {
    return 0;
}
)module"_str },
    };
    return source_tree(files);
}

auto modules_discovery_ambiguous_relative_app_tree()
    -> lito::source::SourceTreeResult<lito::source::SourceTree> {
    const ProjectFile files[] = {
        { "alpha/lito.toml"_str, R"module([package]
name = "fixture-ambiguous-alpha"
version = "0.1.0"

[lib]
name = "fixture-ambiguous-alpha"
module = "fixture.alpha"
archive = "fixture.ambiguous.alpha"
)module"_str },
        { "alpha/src/error.cppm"_str, R"module(export module fixture.error;

export auto error_value() -> int {
    return 3;
}
)module"_str },
        { "alpha/src/lib.cppm"_str, R"module(export module fixture.alpha;

export auto alpha_value() -> int {
    return 1;
}
)module"_str },
        { "app/lito.toml"_str, R"module([package]
name = "fixture-ambiguous-app"
version = "0.1.0"

[[bin]]
link-stdlib = false
name = "fixture-ambiguous-app"

[dependencies.fixture-ambiguous-alpha]
path = "../alpha"
visibility = "private"

[dependencies.fixture-ambiguous-beta]
path = "../beta"
visibility = "private"
)module"_str },
        { "app/src/main.cppm"_str, R"module(import fixture.alpha;
import fixture.beta;
import fixture.error;

auto main() -> int {
    return alpha_value() + beta_value() + error_value() == 6 ? 0 : 1;
}
)module"_str },
        { "beta/lito.toml"_str, R"module([package]
name = "fixture-ambiguous-beta"
version = "0.1.0"

[lib]
name = "fixture-ambiguous-beta"
module = "fixture.beta"
archive = "fixture.ambiguous.beta"
)module"_str },
        { "beta/src/error.cppm"_str, R"module(export module fixture.beta.error;

export auto beta_error_value() -> int {
    return 4;
}
)module"_str },
        { "beta/src/lib.cppm"_str, R"module(export module fixture.beta;

export auto beta_value() -> int {
    return 2;
}
)module"_str },
    };
    return source_tree(files);
}

auto modules_discovery_preprocess_app_tree()
    -> lito::source::SourceTreeResult<lito::source::SourceTree> {
    const ProjectFile files[] = {
        { "app/lito.toml"_str, R"module([package]
name = "fixture-discovery-app"
version = "0.1.0"
[[bin]]
link-stdlib = false
name = "fixture-discovery-app"

[dependencies.fixture-discovery-lib]
path = "../lib"
visibility = "private"
)module"_str },
        { "app/src/main.cppm"_str, R"module(import fixture.discovery.lib;

auto main() -> int {
    return discovery_value() == 17 ? 0 : 1;
}
)module"_str },
        { "lib/lito.toml"_str, R"module([package]
name = "fixture-discovery-lib"
version = "0.1.0"

[lib]
name = "fixture-discovery-lib"
module = "fixture.discovery.lib"
archive = "fixture.discovery.lib"
)module"_str },
        { "lib/src/config.hpp"_str, R"module(#pragma once

#define LITO_DISCOVERY_DETAIL 1
#define LITO_DISCOVERY_MODULE :detail.deeply.nested.component.value
)module"_str },
        { "lib/src/detail/deeply/nested/component/value.cpp"_str,
          R"module(module fixture.discovery.lib;

int discovery_companion_anchor = 0;
)module"_str },
        { "lib/src/detail/deeply/nested/component/value.cppm"_str,
          R"module(export module fixture.discovery.lib:detail.deeply.nested.component.value;

export auto discovery_detail() -> int {
    return 17;
}
)module"_str },
        { "lib/src/lib.cppm"_str, R"module(module;

#include "config.hpp"

export module fixture.discovery.lib;

#if LITO_DISCOVERY_DETAIL
export import LITO_DISCOVERY_MODULE;
#endif

export auto discovery_value() -> int {
    return discovery_detail();
}
)module"_str },
        { "lib/src/unused.cppm"_str, R"module(export module fixture.discovery.lib.unused;
)module"_str },
    };
    return source_tree(files);
}

auto manifest_multiple_primary_modules_tree()
    -> lito::source::SourceTreeResult<lito::source::SourceTree> {
    const ProjectFile files[] = {
        { "lito.toml"_str, R"module([package]
name = "fixture-multiple-primary-modules"
version = "0.1.0"

[[bin]]
link-stdlib = false
name = "fixture-multiple-primary-modules"
sources = [
  "src/alpha.cppm",
  "src/alpha-detail.cppm",
  "src/beta.cppm",
  "src/main.cpp",
]
)module"_str },
        { "src/alpha-detail.cppm"_str, R"module(export module fixture.multiple.alpha:detail;

export constexpr auto alpha_value() -> int {
    return 20;
}
)module"_str },
        { "src/alpha.cppm"_str, R"module(export module fixture.multiple.alpha;

export import :detail;
)module"_str },
        { "src/beta.cppm"_str, R"module(export module fixture.multiple.beta;

export constexpr auto beta_value() -> int {
    return 22;
}
)module"_str },
        { "src/main.cpp"_str, R"module(import fixture.multiple.alpha;
import fixture.multiple.beta;

int main() {
    return alpha_value() + beta_value() == 42 ? 0 : 1;
}
)module"_str },
    };
    return source_tree(files);
}

auto manifest_toml_module_directory_markers_tree()
    -> lito::source::SourceTreeResult<lito::source::SourceTree> {
    const ProjectFile files[] = {
        { "lito.toml"_str, R"module([package]
name = "fixture-convention-markers"
version = "0.1.0"

[lib]
name = "fixture-convention-markers"
module = "fixture.convention.markers"
archive = "fixture.convention.markers"
)module"_str },
        { "src/detail/mod.cppm"_str, R"module(export module fixture.convention.markers:detail;

export auto detail_value() -> int {
    return 7;
}
)module"_str },
        { "src/lib.cppm"_str, R"module(export module fixture.convention.markers;

export import :detail;

export auto marker_value() -> int;
)module"_str },
        { "src/value.cpp"_str, R"module(module fixture.convention.markers;

auto marker_value() -> int {
    return detail_value();
}
)module"_str },
    };
    return source_tree(files);
}

auto manifest_toml_module_multiple_implementations_tree()
    -> lito::source::SourceTreeResult<lito::source::SourceTree> {
    const ProjectFile files[] = {
        { "lito.toml"_str, R"module([package]
name = "fixture-convention-implementations"
version = "0.1.0"

[lib]
name = "fixture-convention-implementations"
module = "fixture.convention.implementations"
archive = "fixture.convention.implementations"
)module"_str },
        { "src/first.cpp"_str, R"module(module fixture.convention.implementations;

auto first_value() -> int {
    return 1;
}
)module"_str },
        { "src/lib.cppm"_str, R"module(export module fixture.convention.implementations;

export auto first_value() -> int;
export auto second_value() -> int;
)module"_str },
        { "src/nested/second.cpp"_str, R"module(module fixture.convention.implementations;

auto second_value() -> int {
    return 2;
}
)module"_str },
    };
    return source_tree(files);
}

auto manifest_toml_module_escaped_directory_tree()
    -> lito::source::SourceTreeResult<lito::source::SourceTree> {
    const ProjectFile files[] = {
        { "lito.toml"_str, R"module([package]
name = "fixture-convention-escaped-directory"
version = "0.1.0"

[lib]
name = "fixture-convention-escaped-directory"
module = "fixture.convention.escaped_directory"
archive = "fixture.convention.escaped_directory"
)module"_str },
        { "src/lib.cppm"_str, R"module(export module fixture.convention.escaped_directory;

export import :template_;
)module"_str },
        { "src/template/configure.cppm"_str,
          R"module(export module fixture.convention.escaped_directory:template_.configure;

export auto configured_value() -> int {
    return 41;
}
)module"_str },
        { "src/template/mod.cpp"_str,
          R"module(module fixture.convention.escaped_directory;

import :template_;

auto template_value() -> int {
    return configured_value() + 1;
}
)module"_str },
        { "src/template/mod.cppm"_str,
          R"module(export module fixture.convention.escaped_directory:template_;

export import :template_.configure;

export auto template_value() -> int;
)module"_str },
    };
    return source_tree(files);
}

auto modules_scanner_module_kinds_tree()
    -> lito::source::SourceTreeResult<lito::source::SourceTree> {
    const ProjectFile files[] = {
        { "lito.toml"_str, R"module([package]
name = "fixture-scanner-module-kinds"
version = "0.1.0"

[lib]
name = "fixture-scanner-module-kinds"
module = "fixture.scanner.kinds"
archive = "fixture.scanner.kinds"
sources = [
  "src/lib.cppm",
  "src/iface.cppm",
  "src/internal.cppm",
  "src/impl.cpp",
]
)module"_str },
        { "src/iface.cppm"_str, R"module(export module fixture.scanner.kinds:iface;

export auto scanner_interface_value() -> int {
    return 2;
}
)module"_str },
        { "src/impl.cpp"_str, R"module(module fixture.scanner.kinds;

auto scanner_value() -> int {
    return 4;
}
)module"_str },
        { "src/internal.cppm"_str, R"module(module fixture.scanner.kinds:internal;

auto scanner_internal_value() -> int {
    return 3;
}
)module"_str },
        { "src/lib.cppm"_str, R"module(export module fixture.scanner.kinds;

export import :iface;
import :internal;

export auto scanner_value() -> int;

module :private;

auto scanner_private_value() -> int {
    return 1;
}
)module"_str },
    };
    return source_tree(files);
}

auto modules_scanner_stdlib_header_tree()
    -> lito::source::SourceTreeResult<lito::source::SourceTree> {
    const ProjectFile files[] = {
        { "lito.toml"_str, R"module([package]
name = "fixture-scanner-stdlib-header"
version = "0.1.0"

[[bin]]
link-stdlib = false
name = "fixture-scanner-stdlib-header"
sources = ["main.cpp"]
)module"_str },
        { "main.cpp"_str, R"module(#include <vector>

auto main() -> int {
    return sizeof(std::vector<int>) > 0 ? 0 : 1;
}
)module"_str },
    };
    return source_tree(files);
}

auto workspace_shared_source_root_tree()
    -> lito::source::SourceTreeResult<lito::source::SourceTree> {
    const ProjectFile files[] = {
        { "app/main.cpp"_str, R"module(import fixture.shared_source_root;

int main() { return shared_source_root_value() == 19 ? 0 : 1; }
)module"_str },
        { "lito.toml"_str, R"module([workspace]
name = "fixture-shared-source-root"
members = ["packages/library", "packages/app"]
default-members = ["packages/app"]

[workspace.package]
version = "0.1.0"
)module"_str },
        { "packages/app/lito.toml"_str, R"module([package]
name = "fixture-shared-source-root-app"
version.workspace = true
source-root = "../.."

[[bin]]
link-stdlib = false
name = "fixture-shared-source-root-app"
sources = ["app/main.cpp"]

[dependencies.fixture-shared-source-root-library]
path = "../library"
visibility = "private"
)module"_str },
        { "packages/library/lito.toml"_str, R"module([package]
name = "fixture-shared-source-root-library"
version.workspace = true
source-root = "../.."

[lib]
name = "fixture-shared-source-root-library"
module = "fixture.shared_source_root"
archive = "fixture-shared-source-root"
sources = ["src/shared.cppm"]
)module"_str },
        { "src/shared.cppm"_str, R"module(export module fixture.shared_source_root;

export int shared_source_root_value() { return 19; }
)module"_str },
    };
    return source_tree(files);
}

auto modules_discovery_import_cycle_tree()
    -> lito::source::SourceTreeResult<lito::source::SourceTree> {
    const ProjectFile files[] = {
        { "lito.toml"_str, R"module([package]
name = "fixture-discovery-cycle"
version = "0.1.0"

[lib]
name = "fixture-discovery-cycle"
module = "fixture.discovery.cycle"
archive = "fixture.discovery.cycle"
)module"_str },
        { "src/lib.cppm"_str, R"module(export module fixture.discovery.cycle;

import fixture.discovery.cycle.other;
)module"_str },
        { "src/other.cppm"_str, R"module(export module fixture.discovery.cycle.other;

import fixture.discovery.cycle;
)module"_str },
    };
    return source_tree(files);
}

auto manifest_toml_module_logical_mismatch_tree()
    -> lito::source::SourceTreeResult<lito::source::SourceTree> {
    const ProjectFile files[] = {
        { "lito.toml"_str, R"module([package]
name = "fixture-convention-mismatch"
version = "0.1.0"

[lib]
name = "fixture-convention-mismatch"
module = "fixture.convention.mismatch"
archive = "fixture.convention.mismatch"
)module"_str },
        { "src/lib.cppm"_str, R"module(export module fixture.convention.other;
)module"_str },
    };
    return source_tree(files);
}

auto manifest_toml_module_missing_primary_tree()
    -> lito::source::SourceTreeResult<lito::source::SourceTree> {
    const ProjectFile files[] = {
        { "lito.toml"_str, R"module([package]
name = "fixture-convention-missing"
version = "0.1.0"

[lib]
name = "fixture-convention-missing"
module = "fixture.convention.missing"
archive = "fixture.convention.missing"
)module"_str },
        { "src/detail.cppm"_str, R"module(export module fixture.convention.missing:detail;
)module"_str },
    };
    return source_tree(files);
}

auto manifest_toml_module_partition_collision_tree()
    -> lito::source::SourceTreeResult<lito::source::SourceTree> {
    const ProjectFile files[] = {
        { "lito.toml"_str, R"module([package]
name = "fixture-convention-partition"
version = "0.1.0"

[lib]
name = "fixture-convention-partition"
module = "fixture.convention.partition"
archive = "fixture.convention.partition"
)module"_str },
        { "src/detail.cppm"_str, R"module(export module fixture.convention.partition:detail;
)module"_str },
        { "src/detail/mod.cppm"_str, R"module(export module fixture.convention.partition:detail;
)module"_str },
        { "src/lib.cppm"_str, R"module(export module fixture.convention.partition;

export import fixture.convention.partition.detail;
export import :detail;
)module"_str },
    };
    return source_tree(files);
}

auto modules_scanner_header_unit_tree()
    -> lito::source::SourceTreeResult<lito::source::SourceTree> {
    const ProjectFile files[] = {
        { "lito.toml"_str, R"module([package]
name = "fixture-scanner-header-unit"
version = "0.1.0"

[lib]
name = "fixture-scanner-header-unit"
module = "fixture.scanner.header_unit"
archive = "fixture.scanner.header_unit"
sources = ["src/lib.cppm"]
)module"_str },
        { "src/lib.cppm"_str, R"module(export module fixture.scanner.header_unit;

import <vector>;
)module"_str },
    };
    return source_tree(files);
}

auto explicit_visible_tree() -> lito::source::SourceTreeResult<lito::source::SourceTree> {
    const ProjectFile files[] = {
        { "app/lito.toml"_str, R"module([package]
name = "fixture-convention-consumer"
version = "0.1.0"

[lib]
name = "fixture-convention-consumer"
module = "fixture.convention"
archive = "fixture_convention_consumer"

[dependencies.fixture-explicit-no-src]
path = "../dependency"
visibility = "public"
)module"_str },
        { "app/src/lib.cppm"_str, R"module(export module fixture.convention;

export import fixture.source;
export import :part;
)module"_str },
        { "app/src/part.cppm"_str, R"module(export module fixture.convention:part;

export int convention_value() { return 1; }
)module"_str },
        { "dependency/lib.cppm"_str, R"module(export module fixture.source;

export int explicit_value() { return 1; }
)module"_str },
        { "dependency/lito.toml"_str, R"module([package]
name = "fixture-explicit-no-src"
version = "0.1.0"

[lib]
name = "fixture-explicit-no-src"
module = "fixture.source"
archive = "fixture_explicit_no_src"
sources = ["lib.cppm"]
)module"_str },
    };
    return source_tree(files);
}

struct ModuleBuildCase {
    ref<str>             name;
    ref<str>             request;
    bool                 valid;
    ModuleProjectFactory make;
};

constexpr ModuleBuildCase module_build_cases[] = {
    { "cache_long_path"_str, ""_str, true, &cache_long_path_tree },
    { "modules_discovery_ambiguous_relative_app"_str,
      "app"_str,
      true,
      &modules_discovery_ambiguous_relative_app_tree },
    { "modules_discovery_preprocess_app"_str,
      "app"_str,
      true,
      &modules_discovery_preprocess_app_tree },
    { "manifest_multiple_primary_modules"_str,
      ""_str,
      true,
      &manifest_multiple_primary_modules_tree },
    { "manifest_toml_module_directory_markers"_str,
      ""_str,
      true,
      &manifest_toml_module_directory_markers_tree },
    { "manifest_toml_module_multiple_implementations"_str,
      ""_str,
      true,
      &manifest_toml_module_multiple_implementations_tree },
    { "manifest_toml_module_escaped_directory"_str,
      ""_str,
      true,
      &manifest_toml_module_escaped_directory_tree },
    { "modules_scanner_module_kinds"_str, ""_str, true, &modules_scanner_module_kinds_tree },
    { "modules_scanner_stdlib_header"_str, ""_str, true, &modules_scanner_stdlib_header_tree },
    { "workspace_shared_source_root"_str, ""_str, true, &workspace_shared_source_root_tree },
    { "modules_discovery_import_cycle"_str, ""_str, false, &modules_discovery_import_cycle_tree },
    { "manifest_toml_module_logical_mismatch"_str,
      ""_str,
      false,
      &manifest_toml_module_logical_mismatch_tree },
    { "manifest_toml_module_missing_primary"_str,
      ""_str,
      false,
      &manifest_toml_module_missing_primary_tree },
    { "manifest_toml_module_partition_collision"_str,
      ""_str,
      false,
      &manifest_toml_module_partition_collision_tree },
    { "modules_scanner_header_unit"_str, ""_str, false, &modules_scanner_header_unit_tree },
    { "explicit_visible"_str, "app"_str, true, &explicit_visible_tree },
};

TEST_F(ModuleDiscovery, DiscoveryAndModuleConventionsBuildExpectedCases) {
    for (const auto& item : module_build_cases) {
        SCOPED_TRACE(item.name);
        auto tree = item.make();
        ASSERT_TRUE(tree.is_ok());
        auto project = materialize(item.name, *tree);
        ASSERT_TRUE(project.is_ok());
        auto requested = project->root.clone();
        if (! item.request.is_empty()) {
            requested = requested.join(PathBuf::from(item.request).as_path());
        }
        auto request = project_build_request(item.name, requested.as_path(), Vec<String>::make());
        auto built   = lito::build(request);
        EXPECT_EQ(built.is_ok(), item.valid);
    }
}
