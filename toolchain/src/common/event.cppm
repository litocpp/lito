export module lito.toolchain.common:event;

import rstd;

using namespace rstd::prelude;

export namespace lito
{

enum class ToolchainEventKind
{
    CMakeConfigure,
    CMakeBuild,
    CMakeInstall,
    CMakeQuery,
    CMakeQueryBuild,
    CMakeSnapshot,
    CMakeReuse,
};

struct ToolchainEvent {
    ToolchainEventKind    kind { ToolchainEventKind::CMakeConfigure };
    ref<str>              target;
    ref<rstd::path::Path> path;
    rstd::time::Duration  elapsed;
    bool                  completed { false };
};

struct ToolchainEventSink {
    void* context {};
    void (*notify)(void*, const ToolchainEvent&) noexcept {};
};

} // namespace lito
