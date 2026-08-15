module;
#include <rstd/macro.hpp>

module lito.driver:install.publication;

import rstd;
import lito.core;
import :install.error;
import :install.destination;
import :install.entry;
import :install.catalog.model;
import :install.store.model;
import :install.identity;
import :install.path;
import :install.source;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace lito
{

struct InstallPublicationLink {
    PackageTargetId target;
    PathBuf         logical_destination;
    PathBuf         physical_destination;
    PathBuf         relative_target;
    InstallAction   action { InstallAction::Created };
};

struct InstallPublicationPackage {
    InstallPackageInfo          info;
    InstallPackageRecord        record;
    Vec<InstallPublicationLink> links;
    PathBuf                     info_destination;
};

struct InstallPublicationPlan {
    InstallDestination             destination;
    Option<InstallLayout>          managed_layout;
    Vec<InstallPublicationPackage> packages;
};

} // namespace lito

namespace lito
{

template<typename T>
auto publication_failure(String message) -> InstallStoreResult<T> {
    return Err(InstallStoreError::Cause(InstallStoreCause::Message(rstd::move(message))));
}

template<typename T>
auto publication_failure(ref<str> message) -> InstallStoreResult<T> {
    return publication_failure<T>(String::make(message));
}

auto origin_text(const InstallEntryOrigin& origin) -> String {
    if (origin.is_PackageFile()) {
        return rstd::format("package-file:{}:{}",
                            origin.as_PackageFile().package.as_str(),
                            origin.as_PackageFile().path.as_path());
    }
    if (origin.is_BuildArtifact()) {
        return rstd::format("build-artifact:{}",
                            package_target_id_text(origin.as_BuildArtifact().target));
    }
    if (origin.is_ExternalAsset()) {
        return rstd::format("external-asset:{}:{}:{}",
                            origin.as_ExternalAsset().dependency.as_str(),
                            origin.as_ExternalAsset().set.as_str(),
                            origin.as_ExternalAsset().path.as_path());
    }
    if (origin.is_Template()) {
        return rstd::format("template:{}", origin.as_Template().input.as_path());
    }
    return String::make("inventory"_str);
}

auto normalize_binary_entries(InstallPackageRecord& package) -> InstallStoreResult<empty> {
    for (auto& binary : package.binaries) {
        auto name = binary.source.as_path().file_name();
        if (name.is_none() || name->to_str().is_none()) {
            return publication_failure<empty>(
                rstd::format("install artifact '{}' has no UTF-8 name", binary.source.as_path()));
        }
        auto destination = PathBuf::from("bin"_str);
        destination.push(PathBuf::from(*name).as_path());
        package.entries.push(InstallEntry {
            .origin               = InstallEntryOrigin::BuildArtifact(binary.target.clone()),
            .payload              = InstallEntryPayload::CopyFile(binary.source.clone()),
            .relative_destination = rstd::move(destination),
        });
    }
    return Ok(empty {});
}

auto managed_layout_for(const InstallPackageRecord& package) -> InstallManagedPackageLayout {
    for (const auto& entry : package.entries) {
        if (! install_path_is_under_bin(entry.relative_destination.as_path())) {
            return InstallManagedPackageLayout::IsolatedPrefix;
        }
    }
    return InstallManagedPackageLayout::DirectBin;
}

auto request_destination_key(ref<rstd::path::Path> path) -> String {
    return path.to_string_lossy();
}

} // namespace lito

namespace lito
{

auto plan_install_publication(InstallDestination        destination,
                              Option<InstallLayout>     managed_layout,
                              Vec<InstallPackageRecord> packages)
    -> InstallStoreResult<InstallPublicationPlan> {
    if (packages.is_empty()) {
        return publication_failure<InstallPublicationPlan>("install request has no packages"_str);
    }
    auto requested_packages = rstd::collections::BTreeMap<String, String>::make();
    auto requested_paths    = rstd::collections::BTreeMap<String, String>::make();
    auto publication        = InstallPublicationPlan {
        .destination    = destination.clone(),
        .managed_layout = rstd::move(managed_layout),
    };

    for (auto& package : packages) {
        if (! valid_package_name(package.name.as_str()) || package.version.is_empty() ||
            package.profile.is_empty() || package.target.is_empty()) {
            return publication_failure<InstallPublicationPlan>(
                "install package identity is invalid"_str);
        }
        rstd_try(normalize_binary_entries(package));
        if (package.entries.is_empty()) {
            return publication_failure<InstallPublicationPlan>(
                rstd::format("install package '{}' has no entries", package.name.as_str()));
        }
        auto identity =
            rstd_try(resolve_install_package_identity(package.name.as_str(), package.provenance));
        if (requested_packages.contains_key(package.name.as_str())) {
            return publication_failure<InstallPublicationPlan>(
                rstd::format("install request repeats package '{}'", package.name.as_str()));
        }
        requested_packages.insert(package.name.clone(), identity.source_identity.clone());

        auto package_layout = publication.destination.is_Managed()
                                  ? managed_layout_for(package)
                                  : InstallManagedPackageLayout::DirectBin;
        auto info           = InstallPackageInfo {
            .identity   = identity.clone(),
            .version    = package.version.clone(),
            .provenance = package.provenance.clone(),
            .profile    = package.profile.clone(),
            .target     = package.target.clone(),
            .layout     = package_layout,
        };
        for (const auto& dependency : package.runtime_dependencies) {
            info.runtime_dependencies.push(InstallStoredRuntimeDependency {
                .package_id = rstd_try(install_package_id(dependency.name.as_str(),
                                                          dependency.source_identity.as_str())),
                .name       = dependency.name.clone(),
                .source_identity = dependency.source_identity.clone(),
            });
        }

        auto links       = Vec<InstallPublicationLink>::make();
        auto owned_links = Vec<InstallOwnedEntry>::make();
        for (auto& entry : package.entries) {
            if (! install_relative_destination_is_valid(entry.relative_destination.as_path())) {
                return publication_failure<InstallPublicationPlan>(rstd::format(
                    "install destination '{}' is unsafe", entry.relative_destination.as_path()));
            }
            auto physical = entry.relative_destination.clone();
            if (publication.destination.is_Managed() &&
                package_layout == InstallManagedPackageLayout::IsolatedPrefix) {
                physical = PathBuf::from("packages"_str);
                physical.push(PathBuf::from(identity.id.as_str()).as_path());
                physical.push(entry.relative_destination.as_path());
            }
            auto physical_key = request_destination_key(physical.as_path());
            auto prior        = requested_paths.get(physical_key.as_str());
            if (prior.is_some()) {
                return publication_failure<InstallPublicationPlan>(rstd::format(
                    "install request contains more than one entry for destination '{}' from "
                    "packages '{}' and '{}'",
                    physical.as_path(),
                    **prior,
                    package.name.as_str()));
            }
            requested_paths.insert(rstd::move(physical_key), package.name.clone());
            info.entries.push(InstallOwnedEntry {
                .logical_destination  = entry.relative_destination.clone(),
                .physical_destination = physical.clone(),
                .kind                 = InstallOwnedEntryKind::File,
                .origin               = origin_text(entry.origin),
            });
            entry.destination =
                PathBuf::from(publication.destination.path()).join(physical.as_path());

            if (! publication.destination.is_Managed() ||
                package_layout != InstallManagedPackageLayout::IsolatedPrefix ||
                ! install_path_is_under_bin(entry.relative_destination.as_path()) ||
                ! entry.origin.is_BuildArtifact()) {
                continue;
            }
            auto public_path = entry.relative_destination.clone();
            auto public_key  = request_destination_key(public_path.as_path());
            prior            = requested_paths.get(public_key.as_str());
            if (prior.is_some()) {
                return publication_failure<InstallPublicationPlan>(rstd::format(
                    "install request contains more than one entry for destination '{}' from "
                    "packages '{}' and '{}'",
                    public_path.as_path(),
                    **prior,
                    package.name.as_str()));
            }
            requested_paths.insert(rstd::move(public_key), package.name.clone());
            auto public_absolute =
                PathBuf::from(publication.destination.path()).join(public_path.as_path());
            auto parent = public_absolute.as_path().parent();
            if (parent.is_none()) {
                return publication_failure<InstallPublicationPlan>(
                    "public install link has no parent"_str);
            }
            auto relative_target =
                rstd::path::lexically_relative(*parent, entry.destination.as_path());
            if (relative_target.is_none()) {
                return publication_failure<InstallPublicationPlan>(
                    rstd::format("cannot form relative install link from '{}' to '{}'",
                                 *parent,
                                 entry.destination.as_path()));
            }
            owned_links.push(InstallOwnedEntry {
                .logical_destination  = entry.relative_destination.clone(),
                .physical_destination = public_path.clone(),
                .kind                 = InstallOwnedEntryKind::SoftLink,
                .origin               = rstd::format(
                    "public-link:{}",
                    package_target_id_text(entry.origin.as_BuildArtifact().target).as_str()),
                .link_target = Some(relative_target->clone()),
            });
            links.push(InstallPublicationLink {
                .target               = entry.origin.as_BuildArtifact().target.clone(),
                .logical_destination  = entry.relative_destination.clone(),
                .physical_destination = rstd::move(public_path),
                .relative_target      = rstd::move(relative_target).unwrap(),
            });
        }
        for (auto& link : owned_links) info.entries.push(rstd::move(link));

        auto info_destination = PathBuf::make();
        if (publication.destination.is_Managed()) {
            info_destination = PathBuf::from("packages"_str);
            info_destination.push(
                PathBuf::from(rstd::format("{}.info", identity.id.as_str())).as_path());
            auto info_key = request_destination_key(info_destination.as_path());
            if (requested_paths.contains_key(info_key.as_str())) {
                return publication_failure<InstallPublicationPlan>(rstd::format(
                    "install package info destination '{}' conflicts with another entry",
                    info_destination.as_path()));
            }
            requested_paths.insert(rstd::move(info_key), package.name.clone());
        }
        publication.packages.push(InstallPublicationPackage {
            .info             = rstd::move(info),
            .record           = rstd::move(package),
            .links            = rstd::move(links),
            .info_destination = rstd::move(info_destination),
        });
    }

    if (! publication.destination.is_Managed()) return Ok(rstd::move(publication));
    for (const auto& package : publication.packages) {
        auto names = rstd::collections::BTreeMap<String, empty>::make();
        for (const auto& dependency : package.info.runtime_dependencies) {
            if (names.contains_key(dependency.name.as_str())) {
                return publication_failure<InstallPublicationPlan>(
                    rstd::format("install package '{}' repeats runtime dependency '{}'",
                                 package.info.identity.name.as_str(),
                                 dependency.name.as_str()));
            }
            names.insert(dependency.name.clone(), empty {});
            auto target = requested_packages.get(dependency.name.as_str());
            if (target.is_some() && **target != dependency.source_identity.as_str()) {
                return publication_failure<InstallPublicationPlan>(rstd::format(
                    "install package '{}' runtime dependency '{}' has source identity mismatch",
                    package.info.identity.name.as_str(),
                    dependency.name.as_str()));
            }
        }
    }
    return Ok(rstd::move(publication));
}

} // namespace lito
