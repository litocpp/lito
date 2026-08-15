export module lito.driver:install.store.model;

import rstd;
import lito.core;
import :install.destination;
import :install.entry;
import :install.package;

using namespace rstd::prelude;

export namespace lito
{

struct InstallLink {
    PackageTargetId target;
    PathBuf         destination;
    PathBuf         relative_target;
    InstallAction   action { InstallAction::Created };
};

struct InstallPackageRecord {
    String                        name;
    String                        version;
    String                        profile;
    String                        target;
    Vec<InstallBinary>            binaries;
    Vec<InstallEntry>             entries;
    InstallSourceProvenance       provenance;
    Vec<InstallRuntimeDependency> runtime_dependencies;
};

struct InstallStoreRequest {
    InstallDestination        destination;
    Vec<InstallPackageRecord> packages;
    bool                      force { false };
};

struct InstallStoreSummary {
    InstallDestination    destination;
    Option<InstallLayout> managed_layout;
    Vec<String>           packages;
    Vec<InstallBinary>    binaries;
    Vec<InstallEntry>     entries;
    Vec<InstallLink>      links;
};

} // namespace lito
