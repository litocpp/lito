module lito.driver:build.cmake_observer;

import rstd;
import :build.event;
import lito.toolchain.common;

using namespace rstd::prelude;

namespace lito
{

auto build_event_kind(ToolchainEventKind kind) noexcept -> BuildEventKind {
    switch (kind) {
    case ToolchainEventKind::CMakeConfigure: return BuildEventKind::CMakeConfigure;
    case ToolchainEventKind::CMakeBuild: return BuildEventKind::CMakeBuild;
    case ToolchainEventKind::CMakeInstall: return BuildEventKind::CMakeInstall;
    case ToolchainEventKind::CMakeQuery: return BuildEventKind::CMakeQuery;
    case ToolchainEventKind::CMakeQueryBuild: return BuildEventKind::CMakeQueryBuild;
    case ToolchainEventKind::CMakeSnapshot: return BuildEventKind::CMakeSnapshot;
    case ToolchainEventKind::CMakeReuse: return BuildEventKind::CMakeReuse;
    }
    return BuildEventKind::CMakeConfigure;
}

void forward_toolchain_event(void* context, const ToolchainEvent& event) noexcept {
    auto* observer = static_cast<const BuildEventSink*>(context);
    if (observer == nullptr || observer->notify == nullptr) return;
    observer->notify(observer->context,
                     BuildEvent { build_event_kind(event.kind),
                                  event.target,
                                  event.path,
                                  event.elapsed,
                                  event.completed });
}

} // namespace lito

namespace lito
{

auto cmake_observer(const Option<BuildEventSink>& observer) noexcept -> Option<ToolchainEventSink> {
    if (observer.is_none() || observer->notify == nullptr) return None();
    return Some(ToolchainEventSink {
        .context = const_cast<BuildEventSink*>(rstd::addressof(*observer)),
        .notify  = forward_toolchain_event,
    });
}

} // namespace lito
