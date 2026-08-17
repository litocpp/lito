module lito.driver;

import rstd;
import lito.core;
import lito.toolchain;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace lito
{

void forward_source_event(void* context, const lito::source::SourceEvent& event) noexcept {
    auto* observer = static_cast<const BuildEventSink*>(context);
    if (observer == nullptr || observer->notify == nullptr) return;
    observer->notify(observer->context,
                     BuildEvent { BuildEventKind::Fetch, event.source, event.destination });
}

auto source_observer(const BuildEventSink& observer) noexcept -> lito::source::SourceEventSink {
    if (observer.notify == nullptr) return {};
    return lito::source::SourceEventSink {
        .context = const_cast<BuildEventSink*>(rstd::addressof(observer)),
        .notify  = forward_source_event,
    };
}

auto source_observer(const Option<BuildEventSink>& observer) noexcept
    -> lito::source::SourceEventSink {
    if (observer.is_none()) return {};
    return source_observer(*observer);
}

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

auto cmake_observer(const Option<BuildEventSink>& observer) noexcept -> Option<ToolchainEventSink> {
    if (observer.is_none() || observer->notify == nullptr) return None();
    return Some(ToolchainEventSink {
        .context = const_cast<BuildEventSink*>(rstd::addressof(*observer)),
        .notify  = forward_toolchain_event,
    });
}

auto emit_build_toolchain(const Option<BuildEventSink>& observer,
                          const ClangToolchain&         toolchain) noexcept -> void {
    if (observer.is_none() || observer->notify == nullptr) return;
    observer->notify(observer->context,
                     BuildEvent { BuildEventKind::Toolchain, "cc"_str, toolchain.cc_path() });
    observer->notify(observer->context,
                     BuildEvent { BuildEventKind::Toolchain, "cxx"_str, toolchain.cxx_path() });
    observer->notify(observer->context,
                     BuildEvent { BuildEventKind::Toolchain, "ld"_str, toolchain.ld_path() });
    observer->notify(observer->context,
                     BuildEvent { BuildEventKind::Toolchain, "ar"_str, toolchain.ar_path() });
}

} // namespace lito
