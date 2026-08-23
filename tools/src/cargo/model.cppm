export module lito.tools.cargo:model;

import rstd;
export import :request;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;

export namespace lito::tools::cargo
{

struct TargetMetadata {
    String      name;
    Vec<String> crate_types;

    auto clone() const -> TargetMetadata {
        return TargetMetadata {
            .name        = name.clone(),
            .crate_types = as<Clone>(crate_types).clone(),
        };
    }
};

struct PackageMetadata {
    String                 id;
    String                 name;
    String                 version;
    Option<TargetMetadata> library;
    Vec<TargetMetadata>    binaries;
    PathBuf                source_root;
    PathBuf                workspace_root;
    PathBuf                manifest;
    PathBuf                lock_file;

    auto clone() const -> PackageMetadata {
        auto result = PackageMetadata {
            .id             = id.clone(),
            .name           = name.clone(),
            .version        = version.clone(),
            .binaries       = Vec<TargetMetadata>::with_capacity(binaries.len()),
            .source_root    = source_root.clone(),
            .workspace_root = workspace_root.clone(),
            .manifest       = manifest.clone(),
            .lock_file      = lock_file.clone(),
        };
        if (library.is_some()) result.library = Some(library->clone());
        for (const auto& binary : binaries) result.binaries.push(binary.clone());
        return result;
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

struct BinaryArtifactSnapshot {
    String  name;
    PathBuf executable;
    String  executable_digest;
    u64     executable_size {};
    String  identity;
    bool    fresh { false };
};

struct BinarySnapshot {
    PackageMetadata             package;
    Vec<BinaryArtifactSnapshot> artifacts;
    rstd::time::Duration        elapsed;
};

} // namespace lito::tools::cargo
