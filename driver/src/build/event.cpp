module lito.driver;

import rstd;
import lito.core;
import lito.tools.cmake;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace lito
{

auto build_event_kind(lito::source::SourceEventKind kind) noexcept -> BuildEventKind {
    switch (kind) {
    case lito::source::SourceEventKind::Fetch: return BuildEventKind::Fetch;
    case lito::source::SourceEventKind::Extract: return BuildEventKind::Extract;
    }
    return BuildEventKind::Fetch;
}

void forward_source_event(void* context, const lito::source::SourceEvent& event) noexcept {
    auto* observer = static_cast<const BuildEventSink*>(context);
    if (observer == nullptr || observer->notify == nullptr) return;
    observer->notify(observer->context,
                     BuildEvent { build_event_kind(event.kind), event.source, event.destination });
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

auto build_event_kind(lito::tools::cmake::EventKind kind) noexcept -> BuildEventKind {
    switch (kind) {
    case lito::tools::cmake::EventKind::Configure: return BuildEventKind::CMakeConfigure;
    case lito::tools::cmake::EventKind::Build: return BuildEventKind::CMakeBuild;
    case lito::tools::cmake::EventKind::Install: return BuildEventKind::CMakeInstall;
    case lito::tools::cmake::EventKind::Query: return BuildEventKind::CMakeQuery;
    case lito::tools::cmake::EventKind::QueryBuild: return BuildEventKind::CMakeQueryBuild;
    case lito::tools::cmake::EventKind::Snapshot: return BuildEventKind::CMakeSnapshot;
    case lito::tools::cmake::EventKind::Reuse: return BuildEventKind::CMakeReuse;
    }
    return BuildEventKind::CMakeConfigure;
}

void forward_cmake_event(void* context, const lito::tools::cmake::Event& event) noexcept {
    auto* observer = static_cast<const BuildEventSink*>(context);
    if (observer == nullptr || observer->notify == nullptr) return;
    observer->notify(observer->context,
                     BuildEvent { build_event_kind(event.kind),
                                  event.target,
                                  event.path,
                                  event.elapsed,
                                  event.completed });
}

auto cmake_observer(const Option<BuildEventSink>& observer) noexcept
    -> Option<lito::tools::cmake::EventSink> {
    if (observer.is_none() || observer->notify == nullptr) return None();
    return Some(lito::tools::cmake::EventSink {
        .context = const_cast<BuildEventSink*>(rstd::addressof(*observer)),
        .notify  = forward_cmake_event,
    });
}

} // namespace lito
