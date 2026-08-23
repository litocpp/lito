export module lito.tools.cargo:request;

import rstd;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;

export namespace lito::tools::cargo
{

struct Provider {
    PathBuf executable;
    String  identity;
    String  host_target;

    auto clone() const -> Provider {
        return Provider {
            .executable  = executable.clone(),
            .identity    = identity.clone(),
            .host_target = host_target.clone(),
        };
    }
};

struct MetadataRequest {
    PathBuf source_root;
    PathBuf manifest;
    String  package;
};

struct BuildRequest {
    String      alias;
    PathBuf     source_root;
    PathBuf     manifest;
    String      package;
    Vec<String> features;
    bool        default_features { true };
    String      profile;
    String      target;
    String      request_identity;
    PathBuf     work_root;
    PathBuf     target_directory;
    usize       jobs { usize(1) };
};

enum class EventKind
{
    Metadata,
    Build,
    Reuse,
};

struct Event {
    EventKind             kind { EventKind::Metadata };
    ref<str>              alias;
    ref<rstd::path::Path> path;
    rstd::time::Duration  elapsed;
    bool                  completed { false };
};

struct EventSink {
    void* context {};
    void (*notify)(void*, const Event&) noexcept {};
};

} // namespace lito::tools::cargo
