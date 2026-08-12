module;
#include <rstd/enum.hpp>

export module lito.install.contract;

import rstd;
import lito.error;
import lito.build.contract;
import lito.config.contract;
import lito.package.identity;
import lito.workspace;

using namespace rstd::prelude;

export namespace lito
{

enum class InstallSourceStorage
{
    BorrowedLocal,
    ManagedCache,
};

class InstallSourceRequirement {
    RSTD_ENUM(InstallSourceRequirement, (LocalProject, (PathBuf requested_root;)))
};

class InstallSourceProvenance {
    RSTD_ENUM(InstallSourceProvenance, (Local, (PathBuf root;)))

public:
    auto clone() const -> InstallSourceProvenance {
        return InstallSourceProvenance::Local(as_Local().root.clone());
    }
};

struct ResolvedInstallSource {
    ResolvedProjectEntry    project;
    InstallSourceProvenance provenance;
    String                  identity;
    InstallSourceStorage    storage { InstallSourceStorage::BorrowedLocal };
};

struct InstallRoot {
    PathBuf path;
};

struct InstallLayout {
    InstallRoot root;
    PathBuf     bin_directory;
    PathBuf     state_directory;
    PathBuf     metadata;
    PathBuf     lock;
    PathBuf     transactions;
};

enum class InstallAction
{
    Created,
    Replaced,
    Unchanged,
};

struct InstallBinary {
    PackageTargetId target;
    PathBuf         source;
    PathBuf         destination;
    InstallAction   action { InstallAction::Created };
};

struct InstallPackageRecord {
    String             name;
    String             version;
    String             profile;
    String             target;
    Vec<InstallBinary> binaries;
};

struct InstallStoreRequest {
    InstallRoot               root;
    InstallSourceProvenance   provenance;
    Vec<InstallPackageRecord> packages;
    bool                      force { false };
};

struct InstallStoreSummary {
    InstallLayout      layout;
    Vec<String>        packages;
    Vec<InstallBinary> binaries;
};

struct InstallRequest {
    ResolvedInstallSource source;
    BuildRequest          build;
    InstallRoot           root;
    Vec<String>           binaries;
    bool                  force { false };
};

struct InstallSummary {
    BuildSummary       build;
    InstallRoot        root;
    Vec<String>        packages;
    Vec<InstallBinary> binaries;
};

} // namespace lito
