export module lito.driver:build.event;

import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito
{

enum class BuildEventKind
{
    Toolchain,
    Fetch,
    Scan,
    ScanReuse,
    Compile,
    Reuse,
    Archive,
    Link,
    Strip,
    Configure,
    ConfigureReuse,
    BuildToolFetch,
    BuildToolReuse,
    BuildToolRun,
    BuildToolRunReuse,
    GeneratedResource,
    GeneratedResourceReuse,
    CMakeConfigure,
    CMakeBuild,
    CMakeInstall,
    CMakeQuery,
    CMakeQueryBuild,
    CMakeSnapshot,
    CMakeReuse,
};

struct BuildProgress {
    usize current {};
    usize total {};
};

struct BuildEvent {
    BuildEventKind        kind { BuildEventKind::Scan };
    ref<str>              target;
    ref<rstd::path::Path> path;
    rstd::time::Duration  elapsed;
    bool                  completed { false };
    Option<BuildProgress> progress;
};

struct BuildEventSink {
    void* context {};
    void (*notify)(void*, const BuildEvent&) noexcept {};
};

} // namespace lito

export namespace rstd
{

template<>
struct Impl<fmt::Display, lito::BuildEventKind> : ImplBase<lito::BuildEventKind> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        auto name = "unknown"_str;
        switch (this->self()) {
        case lito::BuildEventKind::Toolchain: name = "toolchain"_str; break;
        case lito::BuildEventKind::Fetch: name = "fetch"_str; break;
        case lito::BuildEventKind::Scan: name = "scan"_str; break;
        case lito::BuildEventKind::ScanReuse: name = "scan-reuse"_str; break;
        case lito::BuildEventKind::Compile: name = "compile"_str; break;
        case lito::BuildEventKind::Reuse: name = "reuse"_str; break;
        case lito::BuildEventKind::Archive: name = "archive"_str; break;
        case lito::BuildEventKind::Link: name = "link"_str; break;
        case lito::BuildEventKind::Strip: name = "strip"_str; break;
        case lito::BuildEventKind::Configure: name = "configure"_str; break;
        case lito::BuildEventKind::ConfigureReuse: name = "configure-reuse"_str; break;
        case lito::BuildEventKind::BuildToolFetch: name = "build-tool-fetch"_str; break;
        case lito::BuildEventKind::BuildToolReuse: name = "build-tool-reuse"_str; break;
        case lito::BuildEventKind::BuildToolRun: name = "build-tool-run"_str; break;
        case lito::BuildEventKind::BuildToolRunReuse: name = "build-tool-run-reuse"_str; break;
        case lito::BuildEventKind::GeneratedResource: name = "generated-resource"_str; break;
        case lito::BuildEventKind::GeneratedResourceReuse:
            name = "generated-resource-reuse"_str;
            break;
        case lito::BuildEventKind::CMakeConfigure: name = "cmake-configure"_str; break;
        case lito::BuildEventKind::CMakeBuild: name = "cmake-build"_str; break;
        case lito::BuildEventKind::CMakeInstall: name = "cmake-install"_str; break;
        case lito::BuildEventKind::CMakeQuery: name = "cmake-query"_str; break;
        case lito::BuildEventKind::CMakeQueryBuild: name = "cmake-query-build"_str; break;
        case lito::BuildEventKind::CMakeSnapshot: name = "cmake-snapshot"_str; break;
        case lito::BuildEventKind::CMakeReuse: name = "cmake-reuse"_str; break;
        }
        return formatter.write_str(name);
    }
};

} // namespace rstd
