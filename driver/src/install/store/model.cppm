export module lito.driver:install.store.model;

import rstd;
import lito.core;
import :install.destination;
import :install.entry;
import :install.package;
import lito.toolchain.common;

using namespace rstd::prelude;

export namespace lito
{

struct InstallLink {
    lito::package::PackageTargetId target;
    PathBuf                        destination;
    PathBuf                        relative_target;
    InstallAction                  action { InstallAction::Created };
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

struct InstallStripRequest {
    ref<str>                  package;
    const InstallEntryOrigin* origin {};
    lito::artifact::StripMode mode { lito::artifact::StripMode::None };
    ref<rstd::path::Path>     staged;
    ref<rstd::path::Path>     destination;
    ref<rstd::path::Path>     working_directory;
};

struct InstallStripExecutor {
    void* context {};
    ToolchainResult<rstd::time::Duration> (*apply)(void*, const InstallStripRequest&) {};
};

struct InstallStoreRequest {
    InstallDestination           destination;
    Vec<InstallPackageRecord>    packages;
    Option<InstallStripExecutor> strip;
    bool                         force { false };
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
