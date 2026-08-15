export module lito.driver:install.catalog.model;

import rstd;
import lito.core;
import :install.package;

using namespace rstd::prelude;

export namespace lito
{

enum class InstallManagedPackageLayout
{
    DirectBin,
    IsolatedPrefix,
};

enum class InstallOwnedEntryKind
{
    File,
    SoftLink,
};

struct InstallOwnedEntry {
    PathBuf               logical_destination;
    PathBuf               physical_destination;
    InstallOwnedEntryKind kind { InstallOwnedEntryKind::File };
    String                origin;
    Option<PathBuf>       link_target;

    auto clone() const -> InstallOwnedEntry {
        return InstallOwnedEntry {
            .logical_destination  = logical_destination.clone(),
            .physical_destination = physical_destination.clone(),
            .kind                 = kind,
            .origin               = origin.clone(),
            .link_target          = link_target.is_some() ? Some(link_target->clone()) : None(),
        };
    }
};

struct InstallStoredRuntimeDependency {
    String package_id;
    String name;
    String source_identity;

    auto clone() const -> InstallStoredRuntimeDependency {
        return InstallStoredRuntimeDependency {
            .package_id      = package_id.clone(),
            .name            = name.clone(),
            .source_identity = source_identity.clone(),
        };
    }
};

struct InstallPackageInfo {
    InstallPackageIdentity              identity;
    String                              version;
    InstallSourceProvenance             provenance;
    String                              profile;
    String                              target;
    InstallManagedPackageLayout         layout { InstallManagedPackageLayout::DirectBin };
    Vec<InstallOwnedEntry>              entries;
    Vec<InstallStoredRuntimeDependency> runtime_dependencies;

    auto clone() const -> InstallPackageInfo {
        auto copied_entries = Vec<InstallOwnedEntry>::with_capacity(entries.len());
        for (const auto& entry : entries) copied_entries.push(entry.clone());
        auto copied_dependencies =
            Vec<InstallStoredRuntimeDependency>::with_capacity(runtime_dependencies.len());
        for (const auto& dependency : runtime_dependencies) {
            copied_dependencies.push(dependency.clone());
        }
        return InstallPackageInfo {
            .identity             = identity.clone(),
            .version              = version.clone(),
            .provenance           = provenance.clone(),
            .profile              = profile.clone(),
            .target               = target.clone(),
            .layout               = layout,
            .entries              = rstd::move(copied_entries),
            .runtime_dependencies = rstd::move(copied_dependencies),
        };
    }
};

struct InstallCatalog {
    Vec<InstallPackageInfo> packages;
};

} // namespace lito
