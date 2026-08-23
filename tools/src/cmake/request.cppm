module;
#include <rstd/enum.hpp>

export module lito.tools.cmake:request;

import rstd;
import lito.core;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;

export namespace lito::tools::cmake
{

using ExternalAssetEntry       = lito::dependency::ExternalAssetEntry;
using ExternalAssetDisposition = lito::dependency::ExternalAssetDisposition;
using ExternalAssetSet         = lito::dependency::ExternalAssetSet;

class Source {
    RSTD_ENUM(Source, (Find), (Directory, (PathBuf root; String identity; bool cacheable;)))
};

struct CacheEntry {
    String name;
    String value;
};

struct TargetRequirement {
    String name;
};

struct HostToolRequirement {
    String name;
    String target;
};

struct Request {
    String                   alias;
    String                   package;
    Vec<String>              components;
    Source                   source;
    Option<PathBuf>          adapter;
    String                   adapter_identity;
    Option<PathBuf>          config_directory;
    Vec<CacheEntry>          cache;
    Vec<TargetRequirement>   targets;
    Vec<HostToolRequirement> host_tools;
};

struct Provider {
    PathBuf      executable;
    String       identity;
    String       generator;
    Vec<PathBuf> search_paths;
};

struct TargetToolchainConfiguration : DefaultInClass<TargetToolchainConfiguration, Clone> {
    PathBuf         file;
    Vec<CacheEntry> cache;
    String          identity;

    auto clone() const -> TargetToolchainConfiguration {
        auto entries = Vec<CacheEntry>::with_capacity(cache.len());
        for (const auto& entry : cache) {
            entries.push(CacheEntry { .name = entry.name.clone(), .value = entry.value.clone() });
        }
        return TargetToolchainConfiguration {
            .file     = file.clone(),
            .cache    = rstd::move(entries),
            .identity = identity.clone(),
        };
    }
};

struct ToolchainConfiguration {
    PathBuf                              cc;
    PathBuf                              cxx;
    PathBuf                              linker;
    String                               linker_identity;
    PathBuf                              archiver;
    Option<TargetToolchainConfiguration> target;
};

struct ProfileConfiguration {
    String cxx_standard;
    String build_type;
    String c_flags;
    String cxx_flags;
    String linker_flags;
    String msvc_runtime;
    bool   neutral_configuration {};
};

enum class EventKind
{
    Configure,
    Build,
    Install,
    Query,
    QueryBuild,
    Snapshot,
    Reuse,
};

struct Event {
    EventKind             kind { EventKind::Configure };
    ref<str>              target;
    ref<rstd::path::Path> path;
    rstd::time::Duration  elapsed;
    bool                  completed { false };
};

struct EventSink {
    void* context {};
    void (*notify)(void*, const Event&) noexcept {};
};

} // namespace lito::tools::cmake
