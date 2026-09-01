module;
#include <rstd/macro.hpp>

module lito.core:manifest.key_schema;

import rstd;
import rstd.toml;
import :manifest.error;
import :manifest.package;
import :manifest.workspace;
import :manifest.language;
import :package.identity;
import :manifest.primitives;
import :source.tree;

using namespace rstd::prelude;
using namespace rstd::literals;
using Toml    = rstd::toml::Value;
using PathBuf = rstd::path::PathBuf;
using namespace lito::manifest;

auto manifest_package_key(ref<str> key) -> bool {
    return key == "name"_str || key == "version"_str || key == "source-root"_str ||
           key == "target"_str || key == "license"_str || key == "authors"_str ||
           key == "description"_str || key == "readme"_str || key == "repository"_str ||
           key == "documentation"_str || key == "standard"_str || key == "publish"_str;
}

auto package_publish_key(ref<str> key) -> bool {
    return key == "include"_str || key == "exclude"_str;
}

auto library_key(ref<str> key) -> bool {
    return key == "name"_str || key == "kind"_str || key == "archive"_str ||
           key == "artifact"_str || key == "module"_str || key == "sources"_str ||
           key == "source-groups"_str || key == "when"_str || key == "linker-options"_str;
}

auto plugin_key(ref<str> key) -> bool {
    return key == "module"_str || key == "sources"_str || key == "source-groups"_str ||
           key == "when"_str;
}

auto pmacro_key(ref<str> key) -> bool {
    return key == "module"_str || key == "sources"_str || key == "source-groups"_str ||
           key == "when"_str;
}

auto runnable_key(ref<str> key) -> bool {
    return key == "name"_str || key == "module"_str || key == "sources"_str ||
           key == "source-groups"_str || key == "when"_str || key == "link-stdlib"_str;
}

auto binary_key(ref<str> key) -> bool {
    return runnable_key(key) || key == "resources"_str || key == "host-tool"_str;
}

auto build_tool_key(ref<str> key) -> bool {
    return key == "version"_str || key == "executable"_str || key == "archives"_str;
}

auto build_tool_archive_key(ref<str> key) -> bool {
    return key == "url"_str || key == "sha256"_str;
}

auto script_key(ref<str> key) -> bool {
    return key == "supports"_str;
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

auto usage_key(ref<str> key) -> bool {
    return key == "public-include-directories"_str || key == "private-include-directories"_str ||
           key == "public-definitions"_str || key == "private-definitions"_str ||
           key == "options"_str || key == "linker-options"_str || key == "threads"_str ||
           key == "system-libraries"_str;
}

auto when_key(ref<str> key) -> bool {
    return key == "condition"_str || key == "usage"_str;
}

auto include_directory_key(ref<str> key) -> bool {
    return key == "path"_str || key == "root"_str || key == "external-source"_str;
}

auto dependency_key(ref<str> key) -> bool {
    return key == "path"_str || key == "git"_str || key == "branch"_str || key == "tag"_str ||
           key == "rev"_str || key == "commit"_str || key == "visibility"_str ||
           key == "builtin"_str || key == "workspace"_str || key == "features"_str ||
           key == "default-features"_str || key == "version"_str || key == "registry"_str;
}

auto dev_dependency_key(ref<str> key) -> bool {
    return dependency_key(key) && key != "visibility"_str;
}

auto workspace_dependency_key(ref<str> key) -> bool {
    return key == "path"_str || key == "git"_str || key == "branch"_str || key == "tag"_str ||
           key == "rev"_str || key == "commit"_str || key == "builtin"_str ||
           key == "version"_str || key == "registry"_str;
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
    return key == "cmake"_str || key == "pkg-config"_str || key == "cargo"_str;
}

auto cargo_external_key(ref<str> key) -> bool {
    return key == "source"_str || key == "package"_str || key == "manifest-path"_str ||
           key == "features"_str || key == "default-features"_str || key == "profile"_str ||
           key == "usage"_str || key == "visibility"_str || key == "condition"_str ||
           key == "workspace"_str;
}

auto workspace_cargo_external_key(ref<str> key) -> bool {
    return key == "source"_str || key == "package"_str || key == "manifest-path"_str;
}

auto workspace_cargo_external_reference_key(ref<str> key) -> bool {
    return key == "workspace"_str || key == "features"_str || key == "default-features"_str ||
           key == "profile"_str || key == "usage"_str || key == "visibility"_str ||
           key == "condition"_str;
}

auto cmake_external_key(ref<str> key) -> bool {
    return key == "package"_str || key == "source"_str || key == "adapter"_str ||
           key == "cache"_str || key == "config-directory"_str || key == "targets"_str ||
           key == "components"_str || key == "condition"_str || key == "workspace"_str ||
           key == "host-tools"_str;
}

auto cmake_archive_variant_key(ref<str> key) -> bool {
    return key == "archive"_str || key == "sha256"_str;
}

auto workspace_cmake_external_key(ref<str> key) -> bool {
    return cmake_external_key(key) && key != "targets"_str && key != "condition"_str &&
           key != "workspace"_str;
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
    return key == "workspace"_str || key == "targets"_str || key == "condition"_str;
}

auto pkg_config_external_key(ref<str> key) -> bool {
    return key == "module"_str || key == "version"_str || key == "static"_str ||
           key == "usage"_str || key == "visibility"_str || key == "condition"_str ||
           key == "workspace"_str;
}

auto workspace_pkg_config_external_key(ref<str> key) -> bool {
    return key == "module"_str || key == "version"_str || key == "static"_str;
}

auto workspace_pkg_config_external_reference_key(ref<str> key) -> bool {
    return key == "workspace"_str || key == "usage"_str || key == "visibility"_str ||
           key == "condition"_str;
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
    return key == "version"_str || key == "license"_str || key == "authors"_str ||
           key == "description"_str || key == "readme"_str || key == "repository"_str ||
           key == "documentation"_str;
}

auto package_license_key(ref<str> key) -> bool {
    return key == "workspace"_str;
}

auto package_authors_key(ref<str> key) -> bool {
    return key == "workspace"_str;
}

auto package_metadata_key(ref<str> key) -> bool {
    return key == "workspace"_str;
}

auto parse_author_list(Option<ref<Toml>> value, ref<str> context)
    -> ManifestSchemaResult<Vec<String>> {
    auto authors = rstd_try(string_array(value, context));
    if (authors.is_empty()) {
        return manifest_schema_failure<Vec<String>>(rstd::format("{} must not be empty", context));
    }
    for (auto index = usize {}; index < authors.len(); ++index) {
        if (authors[index].is_empty()) {
            return manifest_schema_failure<Vec<String>>(
                rstd::format("{} entries must not be empty", context));
        }
        for (auto previous = usize {}; previous < index; ++previous) {
            if (authors[index] == authors[previous]) {
                return manifest_schema_failure<Vec<String>>(
                    rstd::format("{} must not contain duplicate entries", context));
            }
        }
    }
    return Ok(rstd::move(authors));
}

auto parse_package_standard(const Toml& package)
    -> ManifestSchemaResult<Option<PackageStandardRequirement>> {
    auto value = member(package, "standard"_str);
    if (value.is_none()) return Ok(None());
    auto text = rstd_try(string_value(**value, "package.standard"_str));
    auto c    = parse_c_standard(text.as_str());
    if (c.is_some()) return Ok(Some(PackageStandardRequirement::C(*c)));
    auto cpp = parse_cpp_standard(text.as_str());
    if (cpp.is_some()) return Ok(Some(PackageStandardRequirement::Cpp(*cpp)));
    if (text.as_str().starts_with("c++"_str)) {
        return manifest_schema_failure<Option<PackageStandardRequirement>>(rstd::format(
            "package.standard '{}' is unsupported; C++20 is the minimum supported C++ standard",
            text.as_str()));
    }
    return manifest_schema_failure<Option<PackageStandardRequirement>>(rstd::format(
        "package.standard '{}' is unsupported; expected a C standard or C++20 and later",
        text.as_str()));
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

auto parse_package_authors(const Toml& package) -> ManifestSchemaResult<PackageAuthors> {
    auto declared = member(package, "authors"_str);
    if (declared.is_none()) return Ok(PackageAuthors {});

    if ((**declared).as_array().is_some()) {
        return Ok(PackageAuthors {
            .source = PackageAuthorsSource::Explicit,
            .values = rstd_try(parse_author_list(declared, "package.authors"_str)),
        });
    }

    auto inherited = rstd_try(table_value(**declared, "package.authors"_str));
    rstd_try(reject_unknown(*inherited, "package.authors"_str, package_authors_key));
    auto workspace = member(**declared, "workspace"_str);
    if (workspace.is_none()) {
        return manifest_schema_failure<PackageAuthors>(
            "package.authors is missing 'workspace'"_str);
    }
    auto enabled = (**workspace).as_bool();
    if (enabled.is_none() || ! *enabled) {
        return manifest_schema_failure<PackageAuthors>(
            "package.authors.workspace must be true"_str);
    }
    return Ok(PackageAuthors {
        .source = PackageAuthorsSource::Workspace,
    });
}

auto parse_package_metadata(const Toml& package, ref<str> key)
    -> ManifestSchemaResult<PackageMetadata> {
    auto declared = member(package, key);
    if (declared.is_none()) return Ok(PackageMetadata {});

    auto explicit_value = (**declared).as_str();
    if (explicit_value.is_some()) {
        if (explicit_value->is_empty()) {
            return manifest_schema_failure<PackageMetadata>(
                rstd::format("package.{} must not be empty", key));
        }
        return Ok(PackageMetadata {
            .source = PackageMetadataSource::Explicit,
            .value  = Some(String::make(*explicit_value)),
        });
    }

    auto context   = rstd::format("package.{}", key);
    auto inherited = rstd_try(table_value(**declared, context.as_str()));
    rstd_try(reject_unknown(*inherited, context.as_str(), package_metadata_key));
    auto workspace = member(**declared, "workspace"_str);
    if (workspace.is_none()) {
        return manifest_schema_failure<PackageMetadata>(
            rstd::format("package.{} is missing 'workspace'", key));
    }
    auto enabled = (**workspace).as_bool();
    if (enabled.is_none() || ! *enabled) {
        return manifest_schema_failure<PackageMetadata>(
            rstd::format("package.{}.workspace must be true", key));
    }
    return Ok(PackageMetadata {
        .source = PackageMetadataSource::Workspace,
    });
}

auto readme_archive_path(ref<rstd::path::Path> declared, ref<str> context)
    -> ManifestSchemaResult<String> {
    auto portable = lito::source::SourcePath::from_relative_path(declared);
    if (portable.is_ok()) return Ok(String::make(portable->as_str()));
    auto filename = declared.file_name();
    if (filename.is_none() || filename->to_str().is_none()) {
        return manifest_schema_failure<String>(
            rstd::format("{} must name a portable file", context));
    }
    auto flattened = lito::source::SourcePath::parse(*filename->to_str());
    if (flattened.is_err()) {
        return manifest_schema_failure<String>(
            rstd::format("{} must name a portable file: {}", context, flattened.unwrap_err()));
    }
    return Ok(String::make(flattened->as_str()));
}

auto inferred_readme_exists(ref<str>                              candidate,
                            ref<rstd::path::Path>                 root,
                            Option<ref<lito::source::SourceTree>> embedded_source)
    -> ManifestSchemaResult<bool> {
    if (embedded_source.is_some()) {
        for (const auto& entry : (**embedded_source).entries()) {
            if (entry.path().as_str() == candidate &&
                entry.kind() == lito::source::SourceEntryKind::File) {
                return Ok(true);
            }
        }
        return Ok(false);
    }
    auto path   = PathBuf::from(root).join(PathBuf::from(candidate).as_path());
    auto exists = rstd::fs::exists(path.as_path());
    if (exists.is_err()) {
        return manifest_schema_failure<bool>(
            rstd::format("cannot inspect inferred package.readme '{}': {}",
                         path.as_path(),
                         exists.unwrap_err()));
    }
    return Ok(*exists);
}

auto parse_package_readme(const Toml&                           package,
                          ref<rstd::path::Path>                 root,
                          Option<ref<lito::source::SourceTree>> embedded_source)
    -> ManifestSchemaResult<PackageReadme> {
    auto declared = member(package, "readme"_str);
    if (declared.is_none()) {
        constexpr ref<str> candidates[] = { "README.md"_str, "README.txt"_str, "README"_str };
        for (auto candidate : candidates) {
            if (! rstd_try(inferred_readme_exists(candidate, root, embedded_source))) continue;
            return Ok(PackageReadme {
                .source       = PackageReadmeSource::Inferred,
                .path         = Some(PathBuf::from(root).join(PathBuf::from(candidate).as_path())),
                .archive_path = Some(String::make(candidate)),
            });
        }
        return Ok(PackageReadme {});
    }

    auto explicit_path = (**declared).as_str();
    if (explicit_path.is_some()) {
        if (explicit_path->is_empty()) {
            return manifest_schema_failure<PackageReadme>("package.readme must not be empty"_str);
        }
        auto relative = PathBuf::from(*explicit_path);
        if (relative.as_path().is_absolute() || relative.as_path().has_root()) {
            return manifest_schema_failure<PackageReadme>(
                "package.readme must be relative to the package root"_str);
        }
        if (embedded_source.is_some()) {
            auto portable = lito::source::SourcePath::parse(*explicit_path);
            if (portable.is_err()) {
                return manifest_schema_failure<PackageReadme>(
                    rstd::format("standalone package.readme must be a portable archive path: {}",
                                 rstd::move(portable).unwrap_err()));
            }
        }
        auto archive_path = rstd_try(readme_archive_path(relative.as_path(), "package.readme"_str));
        return Ok(PackageReadme {
            .source       = PackageReadmeSource::Explicit,
            .path         = Some(PathBuf::from(root).join(relative.as_path())),
            .archive_path = Some(rstd::move(archive_path)),
        });
    }

    auto explicit_enabled = (**declared).as_bool();
    if (explicit_enabled.is_some()) {
        if (! *explicit_enabled) {
            return Ok(PackageReadme { .source = PackageReadmeSource::Disabled });
        }
        return Ok(PackageReadme {
            .source = PackageReadmeSource::Explicit,
            .path   = Some(PathBuf::from(root).join(PathBuf::from("README.md"_str).as_path())),
            .archive_path = Some(String::make("README.md"_str)),
        });
    }

    auto inherited = rstd_try(table_value(**declared, "package.readme"_str));
    rstd_try(reject_unknown(*inherited, "package.readme"_str, package_metadata_key));
    auto workspace = member(**declared, "workspace"_str);
    if (workspace.is_none()) {
        return manifest_schema_failure<PackageReadme>("package.readme is missing 'workspace'"_str);
    }
    auto enabled = (**workspace).as_bool();
    if (enabled.is_none() || ! *enabled) {
        return manifest_schema_failure<PackageReadme>("package.readme.workspace must be true"_str);
    }
    return Ok(PackageReadme { .source = PackageReadmeSource::Workspace });
}

auto parse_workspace_package_readme(Option<ref<Toml>> value, ref<rstd::path::Path> root)
    -> ManifestSchemaResult<Option<WorkspacePackageReadme>> {
    if (value.is_none()) return Ok(None());
    auto path = (**value).as_str();
    if (path.is_some()) {
        if (path->is_empty()) {
            return manifest_schema_failure<Option<WorkspacePackageReadme>>(
                "workspace.package.readme must not be empty"_str);
        }
        auto relative = PathBuf::from(*path);
        if (relative.as_path().is_absolute() || relative.as_path().has_root()) {
            return manifest_schema_failure<Option<WorkspacePackageReadme>>(
                "workspace.package.readme must be relative to the workspace root"_str);
        }
        return Ok(Some(WorkspacePackageReadme {
            .enabled = true,
            .path    = PathBuf::from(root).join(relative.as_path()),
        }));
    }
    auto enabled = (**value).as_bool();
    if (enabled.is_none()) {
        return manifest_schema_failure<Option<WorkspacePackageReadme>>(
            "workspace.package.readme must be a path or boolean"_str);
    }
    return Ok(Some(WorkspacePackageReadme {
        .enabled = *enabled,
        .path    = *enabled ? PathBuf::from(root).join(PathBuf::from("README.md"_str).as_path())
                            : PathBuf::make(),
    }));
}

auto parse_package_publish(const Toml& package) -> ManifestSchemaResult<PackagePublish> {
    auto declared = member(package, "publish"_str);
    if (declared.is_none()) return Ok(PackagePublish {});
    auto table = rstd_try(table_value(**declared, "package.publish"_str));
    rstd_try(reject_unknown(*table, "package.publish"_str, package_publish_key));
    auto include       = Option<Vec<String>> {};
    auto include_value = member(**declared, "include"_str);
    if (include_value.is_some()) {
        auto patterns = rstd_try(string_array(include_value, "package.publish.include"_str));
        if (patterns.is_empty()) {
            return manifest_schema_failure<PackagePublish>(
                "package.publish.include must not be empty"_str);
        }
        include = Some(rstd::move(patterns));
    }
    return Ok(PackagePublish {
        .include = rstd::move(include),
        .exclude = rstd_try(
            string_array(member(**declared, "exclude"_str), "package.publish.exclude"_str)),
    });
}
