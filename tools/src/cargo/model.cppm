export module lito.tools.cargo:model;

import rstd;
export import :request;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;

export namespace lito::tools::cargo
{

struct PackageMetadata {
    String  id;
    String  name;
    String  version;
    String  target_name;
    PathBuf source_root;
    PathBuf workspace_root;
    PathBuf manifest;
    PathBuf lock_file;

    auto clone() const -> PackageMetadata {
        return PackageMetadata {
            .id             = id.clone(),
            .name           = name.clone(),
            .version        = version.clone(),
            .target_name    = target_name.clone(),
            .source_root    = source_root.clone(),
            .workspace_root = workspace_root.clone(),
            .manifest       = manifest.clone(),
            .lock_file      = lock_file.clone(),
        };
    }
};

struct StaticLibrarySnapshot {
    PackageMetadata      package;
    PathBuf              archive;
    String               archive_digest;
    u64                  archive_size {};
    Vec<String>          native_link_arguments;
    String               identity;
    bool                 fresh { false };
    rstd::time::Duration elapsed;
};

} // namespace lito::tools::cargo
