module;
#include <rstd/macro.hpp>

export module lito.pack:package;

import rstd;
import lito.core;
import :registry.archive;

using namespace rstd::prelude;
using namespace rstd::literals;
using PathBuf = rstd::path::PathBuf;

export namespace lito
{

struct PackageRegistryContext {
    lito::registry::RegistryId                   owner;
    Vec<lito::manifest::StandaloneRegistryAlias> aliases;
};

struct PackPackageRequest {
    PathBuf                                     root;
    Option<String>                              package;
    Option<PathBuf>                             output;
    bool                                        list {};
    PackageRegistryContext                      registry;
    lito::registry::RegistryExternalInputPolicy external_inputs {
        lito::registry::RegistryExternalInputPolicy::Reject
    };
    Vec<PathBuf> excluded_roots;
};

struct PackPackageSummary {
    lito::registry::RegistryPackageId                package;
    lito::registry::SemanticVersion                  version;
    PathBuf                                          output;
    Vec<String>                                      files;
    Vec<PathBuf>                                     directories;
    Option<lito::registry::InspectedRegistryArchive> artifact;
};

auto pack_package(PackPackageRequest request) -> lito::package::PackageResult<PackPackageSummary>;

} // namespace lito

namespace
{

template<typename T>
auto pack_failure(String message) -> lito::package::PackageResult<T> {
    return Err(lito::package::PackageError::Message(rstd::move(message)));
}

template<typename T>
auto pack_failure(ref<str> message) -> lito::package::PackageResult<T> {
    return pack_failure<T>(String::make(message));
}

auto select_publish_package(lito::workspace::WorkspaceCatalog& catalog,
                            const Option<String>&              requested)
    -> lito::package::PackageResult<lito::manifest::PackageManifest> {
    auto name = Option<String> {};
    if (requested.is_some()) {
        name = Some(requested->clone());
    } else if (catalog.names().len() == usize(1)) {
        name = Some(catalog.names()[usize {}].clone());
    } else {
        return pack_failure<lito::manifest::PackageManifest>(
            "workspace package publishing requires exactly one --package"_str);
    }
    auto package = catalog.take_package(name->as_str());
    if (package.is_none()) {
        return pack_failure<lito::manifest::PackageManifest>(
            rstd::format("project has no publish package named '{}'", name->as_str()));
    }
    return Ok(rstd::move(package).unwrap());
}

} // namespace

auto lito::pack_package(PackPackageRequest request)
    -> lito::package::PackageResult<PackPackageSummary> {
    auto resolved = lito::workspace::resolve_local_project(request.root.as_path());
    if (resolved.is_err()) {
        auto package_error =
            rstd::into<lito::package::PackageError>(rstd::move(resolved).unwrap_err());
        return Err(rstd::move(package_error));
    }
    auto project  = rstd::move(resolved).unwrap();
    auto manifest = rstd_try(select_publish_package(project.primary, request.package));
    auto name     = lito::registry::RegistryPackageName::parse(manifest.name.as_str());
    if (name.is_err()) {
        return pack_failure<PackPackageSummary>(
            rstd::format("package '{}' cannot be published to a Registry: {}",
                         manifest.name.as_str(),
                         rstd::move(name).unwrap_err()));
    }
    if (manifest.version.value.is_none()) {
        return pack_failure<PackPackageSummary>("published package requires a version"_str);
    }
    auto version = lito::registry::SemanticVersion::parse(manifest.version.value->as_str());
    if (version.is_err()) {
        return pack_failure<PackPackageSummary>(
            rstd::format("package version '{}' cannot be published to a Registry: {}",
                         manifest.version.value->as_str(),
                         rstd::move(version).unwrap_err()));
    }
    auto standalone = lito::manifest::serialize_standalone_package_manifest(
        manifest,
        lito::manifest::StandaloneManifestOptions {
            .owner_registry   = request.registry.owner.clone(),
            .registry_aliases = rstd::move(request.registry.aliases),
        });
    if (standalone.is_err()) {
        return Err(rstd::into<lito::package::PackageError>(rstd::move(standalone).unwrap_err()));
    }
    auto package = lito::registry::RegistryPackageId {
        .registry = rstd::move(request.registry.owner),
        .name     = rstd::move(name).unwrap(),
    };
    auto exact = rstd::move(version).unwrap();
    auto output =
        request.output.is_some()
            ? rstd::move(request.output).unwrap()
            : request.root.join(
                  PathBuf::from(rstd::format("{}-{}.tar.zst", package.name.as_str(), exact.text()))
                      .as_path());
    if (output.as_path().is_relative()) output = request.root.join(output.as_path());
    auto files = lito::manifest::PackageFileSetResolver::resolve(
        manifest,
        lito::manifest::PackagePublishPolicy {
            .excluded_roots = rstd::move(request.excluded_roots),
            .archive        = Some(output.clone()),
        });
    if (files.is_err()) {
        return pack_failure<PackPackageSummary>(rstd::move(files).unwrap_err().message);
    }
    auto paths       = files->paths();
    auto directories = as<Clone>(files->directories()).clone();
    if (request.list) {
        return Ok(PackPackageSummary {
            .package     = rstd::move(package),
            .version     = rstd::move(exact),
            .output      = rstd::move(output),
            .files       = rstd::move(paths),
            .directories = rstd::move(directories),
        });
    }
    auto built = lito::registry::PackageArchiveBuilder::build(
        *files, *standalone, package, exact, output.clone(), {}, request.external_inputs);
    if (built.is_err()) {
        return pack_failure<PackPackageSummary>(rstd::move(built).unwrap_err().message);
    }
    return Ok(PackPackageSummary {
        .package     = rstd::move(package),
        .version     = rstd::move(exact),
        .output      = rstd::move(output),
        .files       = rstd::move(paths),
        .directories = rstd::move(directories),
        .artifact    = Some(rstd::move(built).unwrap()),
    });
}
