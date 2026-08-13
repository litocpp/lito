module;
#include <rstd/macro.hpp>

export module lito.manifest:key_schema;

import rstd;
import rstd.toml;
import lito.error;
import lito.manifest.contract;
import lito.package.identity;
import :primitives;

using namespace rstd::prelude;
using namespace rstd::literals;
using Toml = rstd::toml::Value;

namespace lito
{

auto package_key(ref<str> key) -> bool {
    return key == "name"_str || key == "version"_str || key == "source-root"_str ||
           key == "target"_str;
}

auto library_key(ref<str> key) -> bool {
    return key == "name"_str || key == "archive"_str || key == "module"_str ||
           key == "sources"_str || key == "source-groups"_str;
}

auto runnable_key(ref<str> key) -> bool {
    return key == "name"_str || key == "module"_str || key == "sources"_str ||
           key == "source-groups"_str || key == "link-stdlib"_str;
}

auto test_key(ref<str> key) -> bool {
    return runnable_key(key) || key == "attach"_str;
}

auto compile_test_key(ref<str> key) -> bool {
    return key == "cases"_str;
}

auto target_predicate_key(ref<str> key) -> bool {
    return key == "family"_str || key == "os"_str || key == "not-family"_str || key == "not-os"_str;
}

auto source_group_key(ref<str> key) -> bool {
    return key == "sources"_str || key == "target"_str;
}

auto test_attachment_key(ref<str> key) -> bool {
    return key == "package"_str || key == "sources"_str || key == "source-groups"_str;
}

auto compile_test_case_key(ref<str> key) -> bool {
    return key == "name"_str || key == "source"_str || key == "outcome"_str ||
           key == "options"_str || key == "diagnostic-contains"_str ||
           key == "diagnostic-contains-any"_str;
}

auto usage_key(ref<str> key) -> bool {
    return key == "public-include-directories"_str || key == "private-include-directories"_str ||
           key == "public-definitions"_str || key == "private-definitions"_str ||
           key == "public-options"_str || key == "private-options"_str ||
           key == "private-linker-options"_str;
}

auto include_directory_key(ref<str> key) -> bool {
    return key == "path"_str || key == "root"_str;
}

auto dependency_key(ref<str> key) -> bool {
    return key == "path"_str || key == "git"_str || key == "branch"_str || key == "tag"_str ||
           key == "rev"_str || key == "commit"_str || key == "visibility"_str ||
           key == "workspace"_str;
}

auto dev_dependency_key(ref<str> key) -> bool {
    return dependency_key(key) && key != "visibility"_str;
}

auto workspace_dependency_key(ref<str> key) -> bool {
    return key == "path"_str || key == "git"_str || key == "branch"_str || key == "tag"_str ||
           key == "rev"_str || key == "commit"_str;
}

auto workspace_dependency_reference_key(ref<str> key) -> bool {
    return key == "workspace"_str || key == "visibility"_str;
}

auto workspace_dev_dependency_reference_key(ref<str> key) -> bool {
    return key == "workspace"_str;
}

auto runtime_dependency_key(ref<str> key) -> bool {
    return dev_dependency_key(key);
}

auto workspace_runtime_dependency_reference_key(ref<str> key) -> bool {
    return key == "workspace"_str;
}

auto external_dependencies_key(ref<str> key) -> bool {
    return key == "cmake"_str || key == "pkg-config"_str;
}

auto cmake_external_key(ref<str> key) -> bool {
    return key == "find-package"_str || key == "path"_str || key == "git"_str ||
           key == "branch"_str || key == "tag"_str || key == "rev"_str || key == "archive"_str ||
           key == "archives"_str || key == "sha256"_str || key == "integration"_str ||
           key == "adapter"_str || key == "cache"_str || key == "config-directory"_str ||
           key == "targets"_str || key == "workspace"_str;
}

auto cmake_archive_variant_key(ref<str> key) -> bool {
    return key == "archive"_str || key == "sha256"_str;
}

auto workspace_cmake_external_key(ref<str> key) -> bool {
    return cmake_external_key(key) && key != "targets"_str && key != "workspace"_str;
}

auto workspace_cmake_external_reference_key(ref<str> key) -> bool {
    return key == "workspace"_str || key == "targets"_str;
}

auto cmake_target_key(ref<str> key) -> bool {
    return key == "name"_str || key == "visibility"_str;
}

auto pkg_config_external_key(ref<str> key) -> bool {
    return key == "module"_str || key == "version"_str || key == "static"_str ||
           key == "visibility"_str || key == "workspace"_str;
}

auto workspace_pkg_config_external_key(ref<str> key) -> bool {
    return key == "module"_str || key == "version"_str || key == "static"_str;
}

auto workspace_pkg_config_external_reference_key(ref<str> key) -> bool {
    return key == "workspace"_str || key == "visibility"_str;
}

auto workspace_key(ref<str> key) -> bool {
    return key == "name"_str || key == "members"_str || key == "default-members"_str ||
           key == "package"_str || key == "dependencies"_str || key == "external-dependencies"_str;
}

auto package_version_key(ref<str> key) -> bool {
    return key == "workspace"_str;
}

auto workspace_package_key(ref<str> key) -> bool {
    return key == "version"_str;
}

auto parse_package_version(const Toml& package, bool optional) -> ManifestSchemaResult<PackageVersion> {
    auto declared = member(package, "version"_str);
    if (declared.is_none()) {
        if (optional) return Ok(PackageVersion {});
        return failure<PackageVersion>("package is missing 'version'"_str);
    }

    auto explicit_value = (**declared).as_str();
    if (explicit_value.is_some()) {
        if (explicit_value->is_empty()) {
            return failure<PackageVersion>("package.version must not be empty"_str);
        }
        return Ok(PackageVersion {
            .source = PackageVersionSource::Explicit,
            .value  = Some(String::make(*explicit_value)),
        });
    }

    auto inherited = table_value(**declared, "package.version"_str);
    if (inherited.is_err()) return Err(rstd::move(inherited).unwrap_err());
    auto known = reject_unknown(**inherited, "package.version"_str, package_version_key);
    if (known.is_err()) return Err(rstd::move(known).unwrap_err());
    auto workspace = member(**declared, "workspace"_str);
    if (workspace.is_none()) {
        return failure<PackageVersion>("package.version is missing 'workspace'"_str);
    }
    auto enabled = (**workspace).as_bool();
    if (enabled.is_none() || ! *enabled) {
        return failure<PackageVersion>("package.version.workspace must be true"_str);
    }
    return Ok(PackageVersion {
        .source = PackageVersionSource::Workspace,
    });
}

} // namespace lito
