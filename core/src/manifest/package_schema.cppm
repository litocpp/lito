module;
#include <rstd/macro.hpp>

module lito.core:manifest.package_schema;

import rstd;
import :manifest.wire.common;
import :manifest.error;
import :manifest.package;
import :manifest.workspace;
import :manifest.language;
import :package.identity;
import :manifest.primitives;
import :source.tree;

using namespace rstd::prelude;
using namespace rstd::literals;
using PathBuf = rstd::path::PathBuf;
using namespace lito::manifest;

auto parse_author_list(Vec<String> authors, ref<str> context) -> ManifestSchemaResult<Vec<String>> {
    if (authors.is_empty()) {
        return Err(ManifestSchemaError::Domain(rstd::format("{} must not be empty", context)));
    }
    for (auto index = usize {}; index < authors.len(); ++index) {
        if (authors[index].is_empty()) {
            return Err(
                ManifestSchemaError::Domain(rstd::format("{} entries must not be empty", context)));
        }
        for (auto previous = usize {}; previous < index; ++previous) {
            if (authors[index] == authors[previous]) {
                return Err(ManifestSchemaError::Domain(
                    rstd::format("{} must not contain duplicate entries", context)));
            }
        }
    }
    return Ok(rstd::move(authors));
}

auto parse_package_standard(Option<String> value)
    -> ManifestSchemaResult<Option<PackageStandardRequirement>> {
    if (value.is_none()) return Ok(None());
    auto text = rstd::move(value).unwrap();
    auto c    = parse_c_standard(text.as_str());
    if (c.is_some()) return Ok(Some(PackageStandardRequirement::C(*c)));
    auto cpp = parse_cpp_standard(text.as_str());
    if (cpp.is_some()) return Ok(Some(PackageStandardRequirement::Cpp(*cpp)));
    if (text.as_str().starts_with("c++"_str)) {
        return Err(ManifestSchemaError::Domain(rstd::format(
            "package.standard '{}' is unsupported; C++20 is the minimum supported C++ standard",
            text.as_str())));
    }
    return Err(ManifestSchemaError::Domain(rstd::format(
        "package.standard '{}' is unsupported; expected a C standard or C++20 and later",
        text.as_str())));
}

auto parse_package_version(Option<wire::Inherited<String>> declared, bool optional)
    -> ManifestSchemaResult<PackageVersion> {
    if (declared.is_none()) {
        if (! optional) return Err(ManifestSchemaError::Domain("package is missing 'version'"_Str));
        return Ok(PackageVersion {});
    }
    if (declared->workspace)
        return Ok(PackageVersion { .source = PackageVersionSource::Workspace });
    auto value = rstd::move(declared->value).unwrap();
    if (value.is_empty())
        return Err(ManifestSchemaError::Domain(
            rstd::format("package.{} must not be empty", "version"_str)));
    return Ok(PackageVersion { .source = PackageVersionSource::Explicit,
                               .value  = Some(rstd::move(value)) });
}

auto parse_package_license(Option<wire::Inherited<String>> declared)
    -> ManifestSchemaResult<PackageLicense> {
    if (declared.is_none()) {
        return Ok(PackageLicense {});
    }
    if (declared->workspace)
        return Ok(PackageLicense { .source = PackageLicenseSource::Workspace });
    auto value = rstd::move(declared->value).unwrap();
    if (value.is_empty())
        return Err(ManifestSchemaError::Domain(
            rstd::format("package.{} must not be empty", "license"_str)));
    return Ok(PackageLicense { .source = PackageLicenseSource::Explicit,
                               .value  = Some(rstd::move(value)) });
}

auto parse_package_metadata(Option<wire::Inherited<String>> declared, ref<str> key)
    -> ManifestSchemaResult<PackageMetadata> {
    if (declared.is_none()) {
        return Ok(PackageMetadata {});
    }
    if (declared->workspace)
        return Ok(PackageMetadata { .source = PackageMetadataSource::Workspace });
    auto value = rstd::move(declared->value).unwrap();
    if (value.is_empty())
        return Err(ManifestSchemaError::Domain(rstd::format("package.{} must not be empty", key)));
    return Ok(PackageMetadata { .source = PackageMetadataSource::Explicit,
                                .value  = Some(rstd::move(value)) });
}

auto parse_package_authors(Option<wire::Inherited<Vec<String>>> declared)
    -> ManifestSchemaResult<PackageAuthors> {
    if (declared.is_none()) return Ok(PackageAuthors {});
    if (declared->workspace)
        return Ok(PackageAuthors { .source = PackageAuthorsSource::Workspace });
    return Ok(PackageAuthors { .source = PackageAuthorsSource::Explicit,
                               .values = rstd_try(parse_author_list(
                                   rstd::move(declared->value).unwrap(), "package.authors"_str)) });
}

auto readme_archive_path(ref<rstd::path::Path> declared, ref<str> context)
    -> ManifestSchemaResult<String> {
    auto portable = lito::source::SourcePath::from_relative_path(declared);
    if (portable.is_ok()) return Ok(String::make(portable->as_str()));
    auto filename = declared.file_name();
    if (filename.is_none() || filename->to_str().is_none()) {
        return Err(
            ManifestSchemaError::Domain(rstd::format("{} must name a portable file", context)));
    }
    auto flattened = lito::source::SourcePath::parse(*filename->to_str());
    if (flattened.is_err()) {
        return Err(ManifestSchemaError::Domain(
            rstd::format("{} must name a portable file: {}", context, flattened.unwrap_err())));
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
        return Err(ManifestSchemaError::Domain(
            rstd::format("cannot inspect inferred package.readme '{}': {}",
                         path.as_path(),
                         exists.unwrap_err())));
    }
    return Ok(*exists);
}

auto parse_package_readme(Option<wire::Inherited<wire::ReadmeValue>> declared,
                          ref<rstd::path::Path>                      root,
                          Option<ref<lito::source::SourceTree>>      embedded_source)
    -> ManifestSchemaResult<PackageReadme> {
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

    if (declared->workspace) return Ok(PackageReadme { .source = PackageReadmeSource::Workspace });
    auto readme        = rstd::move(declared->value).unwrap();
    auto explicit_path = rstd::move(readme.path);
    if (explicit_path.is_some()) {
        if (explicit_path->is_empty()) {
            return Err(ManifestSchemaError::Domain("package.readme must not be empty"_Str));
        }
        auto relative = PathBuf::from(explicit_path->as_str());
        if (relative.as_path().is_absolute() || relative.as_path().has_root()) {
            return Err(ManifestSchemaError::Domain(
                "package.readme must be relative to the package root"_Str));
        }
        if (embedded_source.is_some()) {
            auto portable = lito::source::SourcePath::parse(explicit_path->as_str());
            if (portable.is_err()) {
                return Err(ManifestSchemaError::Domain(
                    rstd::format("standalone package.readme must be a portable archive path: {}",
                                 rstd::move(portable).unwrap_err())));
            }
        }
        auto archive_path = rstd_try(readme_archive_path(relative.as_path(), "package.readme"_str));
        return Ok(PackageReadme {
            .source       = PackageReadmeSource::Explicit,
            .path         = Some(PathBuf::from(root).join(relative.as_path())),
            .archive_path = Some(rstd::move(archive_path)),
        });
    }

    if (! readme.enabled) return Ok(PackageReadme { .source = PackageReadmeSource::Disabled });
    return Ok(PackageReadme {
        .source       = PackageReadmeSource::Explicit,
        .path         = Some(PathBuf::from(root).join(PathBuf::from("README.md"_str).as_path())),
        .archive_path = Some("README.md"_Str),
    });
}

auto parse_workspace_package_readme(Option<wire::ReadmeValue> value, ref<rstd::path::Path> root)
    -> ManifestSchemaResult<Option<WorkspacePackageReadme>> {
    if (value.is_none()) return Ok(None());
    auto path = rstd::move(value->path);
    if (path.is_some()) {
        if (path->is_empty()) {
            return Err(
                ManifestSchemaError::Domain("workspace.package.readme must not be empty"_Str));
        }
        auto relative = PathBuf::from(path->as_str());
        if (relative.as_path().is_absolute() || relative.as_path().has_root()) {
            return Err(ManifestSchemaError::Domain(
                "workspace.package.readme must be relative to the workspace root"_Str));
        }
        return Ok(Some(WorkspacePackageReadme {
            .enabled = true,
            .path    = PathBuf::from(root).join(relative.as_path()),
        }));
    }
    auto enabled = value->enabled;
    return Ok(Some(WorkspacePackageReadme {
        .enabled = enabled,
        .path    = enabled ? PathBuf::from(root).join(PathBuf::from("README.md"_str).as_path())
                           : PathBuf::make(),
    }));
}

auto parse_package_publish(Option<wire::Publish> declared) -> ManifestSchemaResult<PackagePublish> {
    if (declared.is_none()) return Ok(PackagePublish {});
    if (declared->include.is_some() && declared->include->is_empty())
        return Err(ManifestSchemaError::Domain("package.publish.include must not be empty"_Str));
    return Ok(PackagePublish {
        .include = rstd::move(declared->include),
        .exclude = rstd::move(declared->exclude).unwrap_or(Vec<String>::make()),
    });
}
