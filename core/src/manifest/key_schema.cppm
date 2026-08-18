module;
#include <rstd/macro.hpp>

module lito.core:manifest.key_schema;

import rstd;
import rstd.toml;
import :manifest.error;
import :manifest.package;
import :manifest.language;
import :package.identity;
import :manifest.primitives;

using namespace rstd::prelude;
using namespace rstd::literals;
using Toml = rstd::toml::Value;
using namespace lito::manifest;

auto manifest_package_key(ref<str> key) -> bool {
    return key == "name"_str || key == "version"_str || key == "source-root"_str ||
           key == "target"_str || key == "license"_str || key == "language"_str ||
           key == "minimum-standard"_str;
}

auto library_key(ref<str> key) -> bool {
    return key == "name"_str || key == "archive"_str || key == "module"_str ||
           key == "sources"_str || key == "source-groups"_str || key == "when"_str;
}

auto runnable_key(ref<str> key) -> bool {
    return key == "name"_str || key == "module"_str || key == "sources"_str ||
           key == "source-groups"_str || key == "when"_str || key == "link-stdlib"_str;
}

auto source_group_key(ref<str> key) -> bool {
    return key == "root"_str || key == "external-source"_str || key == "sources"_str;
}

auto target_when_key(ref<str> key) -> bool {
    return key == "condition"_str || key == "source-groups"_str;
}

auto binary_key(ref<str> key) -> bool {
    return runnable_key(key) || key == "resources"_str;
}

auto runtime_resource_key(ref<str> key) -> bool {
    return key == "name"_str || key == "root"_str || key == "path"_str;
}

auto build_tool_key(ref<str> key) -> bool {
    return key == "version"_str || key == "executable"_str || key == "archives"_str;
}

auto build_tool_archive_key(ref<str> key) -> bool {
    return key == "url"_str || key == "sha256"_str;
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

auto test_attachment_key(ref<str> key) -> bool {
    return key == "package"_str || key == "sources"_str;
}

auto compile_test_case_key(ref<str> key) -> bool {
    return key == "name"_str || key == "source"_str || key == "outcome"_str ||
           key == "options"_str || key == "diagnostic-contains"_str ||
           key == "diagnostic-contains-any"_str;
}

auto usage_key(ref<str> key) -> bool {
    return key == "public-include-directories"_str || key == "private-include-directories"_str ||
           key == "public-definitions"_str || key == "private-definitions"_str ||
           key == "options"_str || key == "linker-options"_str || key == "threads"_str ||
           key == "system-libraries"_str;
}

auto when_key(ref<str> key) -> bool {
    return key == "condition"_str || key == "usage"_str;
}

auto feature_key(ref<str> key) -> bool {
    return key == "default"_str;
}

auto include_directory_key(ref<str> key) -> bool {
    return key == "path"_str || key == "root"_str || key == "external-source"_str;
}

auto dependency_key(ref<str> key) -> bool {
    return key == "path"_str || key == "git"_str || key == "branch"_str || key == "tag"_str ||
           key == "rev"_str || key == "commit"_str || key == "visibility"_str ||
           key == "workspace"_str || key == "features"_str || key == "default-features"_str;
}

auto dev_dependency_key(ref<str> key) -> bool {
    return dependency_key(key) && key != "visibility"_str;
}

auto workspace_dependency_key(ref<str> key) -> bool {
    return key == "path"_str || key == "git"_str || key == "branch"_str || key == "tag"_str ||
           key == "rev"_str || key == "commit"_str;
}

auto workspace_dependency_reference_key(ref<str> key) -> bool {
    return key == "workspace"_str || key == "visibility"_str || key == "features"_str ||
           key == "default-features"_str;
}

auto workspace_dev_dependency_reference_key(ref<str> key) -> bool {
    return key == "workspace"_str || key == "features"_str || key == "default-features"_str;
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
    return key == "package"_str || key == "source"_str || key == "adapter"_str ||
           key == "cache"_str || key == "config-directory"_str || key == "targets"_str ||
           key == "workspace"_str;
}

auto cmake_archive_variant_key(ref<str> key) -> bool {
    return key == "archive"_str || key == "sha256"_str;
}

auto workspace_cmake_external_key(ref<str> key) -> bool {
    return cmake_external_key(key) && key != "targets"_str && key != "workspace"_str;
}

auto external_source_key(ref<str> key) -> bool {
    return key == "path"_str || key == "git"_str || key == "branch"_str || key == "tag"_str ||
           key == "rev"_str || key == "commit"_str || key == "archive"_str ||
           key == "archives"_str || key == "sha256"_str || key == "workspace"_str;
}

auto workspace_external_source_key(ref<str> key) -> bool {
    return external_source_key(key) && key != "workspace"_str;
}

auto workspace_external_source_reference_key(ref<str> key) -> bool {
    return key == "workspace"_str;
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
           key == "package"_str || key == "dependencies"_str ||
           key == "external-dependencies"_str || key == "external-sources"_str;
}

auto package_version_key(ref<str> key) -> bool {
    return key == "workspace"_str;
}

auto workspace_package_key(ref<str> key) -> bool {
    return key == "version"_str || key == "license"_str;
}

auto package_license_key(ref<str> key) -> bool {
    return key == "workspace"_str;
}

auto parse_package_language(const Toml& package, bool has_compile_target)
    -> ManifestSchemaResult<Option<PackageLanguageRequirement>> {
    auto language_value = member(package, "language"_str);
    auto standard_value = member(package, "minimum-standard"_str);
    if (language_value.is_none() && standard_value.is_some()) {
        return manifest_schema_failure<Option<PackageLanguageRequirement>>(
            "package.minimum-standard requires package.language"_str);
    }
    if (language_value.is_none()) {
        if (! has_compile_target) return Ok(None());
        return Ok(Some(PackageLanguageRequirement::cpp_language()));
    }

    auto language = rstd_try(string_value(**language_value, "package.language"_str));
    if (language.as_str() != "c"_str && language.as_str() != "cpp"_str) {
        return manifest_schema_failure<Option<PackageLanguageRequirement>>(rstd::format(
            "package.language '{}' is unsupported; expected 'c' or 'cpp'", language.as_str()));
    }
    if (language.as_str() == "c"_str) {
        auto standard = CStandard::C99;
        if (standard_value.is_some()) {
            auto text   = rstd_try(string_value(**standard_value, "package.minimum-standard"_str));
            auto parsed = parse_c_standard(text.as_str());
            if (parsed.is_none()) {
                return manifest_schema_failure<Option<PackageLanguageRequirement>>(rstd::format(
                    "package.minimum-standard '{}' is not a supported C standard", text.as_str()));
            }
            standard = *parsed;
        }
        return Ok(Some(PackageLanguageRequirement::c_language(standard)));
    }

    auto standard = CppStandard::Cpp20;
    if (standard_value.is_some()) {
        auto text   = rstd_try(string_value(**standard_value, "package.minimum-standard"_str));
        auto parsed = parse_cpp_standard(text.as_str());
        if (parsed.is_none()) {
            return manifest_schema_failure<Option<PackageLanguageRequirement>>(rstd::format(
                "package.minimum-standard '{}' is not a supported C++ standard", text.as_str()));
        }
        standard = *parsed;
    }
    return Ok(Some(PackageLanguageRequirement::cpp_language(standard)));
}

auto parse_package_version(const Toml& package, bool optional)
    -> ManifestSchemaResult<PackageVersion> {
    auto declared = member(package, "version"_str);
    if (declared.is_none()) {
        if (optional) return Ok(PackageVersion {});
        return manifest_schema_failure<PackageVersion>("package is missing 'version'"_str);
    }

    auto explicit_value = (**declared).as_str();
    if (explicit_value.is_some()) {
        if (explicit_value->is_empty()) {
            return manifest_schema_failure<PackageVersion>("package.version must not be empty"_str);
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
        return manifest_schema_failure<PackageVersion>(
            "package.version is missing 'workspace'"_str);
    }
    auto enabled = (**workspace).as_bool();
    if (enabled.is_none() || ! *enabled) {
        return manifest_schema_failure<PackageVersion>(
            "package.version.workspace must be true"_str);
    }
    return Ok(PackageVersion {
        .source = PackageVersionSource::Workspace,
    });
}

auto parse_package_license(const Toml& package) -> ManifestSchemaResult<PackageLicense> {
    auto declared = member(package, "license"_str);
    if (declared.is_none()) return Ok(PackageLicense {});

    auto explicit_value = (**declared).as_str();
    if (explicit_value.is_some()) {
        if (explicit_value->is_empty()) {
            return manifest_schema_failure<PackageLicense>("package.license must not be empty"_str);
        }
        return Ok(PackageLicense {
            .source = PackageLicenseSource::Explicit,
            .value  = Some(String::make(*explicit_value)),
        });
    }

    auto inherited = table_value(**declared, "package.license"_str);
    if (inherited.is_err()) return Err(rstd::move(inherited).unwrap_err());
    auto known = reject_unknown(**inherited, "package.license"_str, package_license_key);
    if (known.is_err()) return Err(rstd::move(known).unwrap_err());
    auto workspace = member(**declared, "workspace"_str);
    if (workspace.is_none()) {
        return manifest_schema_failure<PackageLicense>(
            "package.license is missing 'workspace'"_str);
    }
    auto enabled = (**workspace).as_bool();
    if (enabled.is_none() || ! *enabled) {
        return manifest_schema_failure<PackageLicense>(
            "package.license.workspace must be true"_str);
    }
    return Ok(PackageLicense {
        .source = PackageLicenseSource::Workspace,
    });
}
