module lito.driver:build.source_observer;

import rstd;
import :build.event;
import lito.core;

using namespace rstd::prelude;

namespace lito
{

void forward_source_event(void* context, const SourceEvent& event) noexcept {
    auto* observer = static_cast<const BuildEventSink*>(context);
    if (observer == nullptr || observer->notify == nullptr) return;
    observer->notify(observer->context,
                     BuildEvent { BuildEventKind::Fetch, event.source, event.destination });
}

} // namespace lito

namespace lito
{

auto source_observer(const BuildEventSink& observer) noexcept -> SourceEventSink {
    if (observer.notify == nullptr) return {};
    return SourceEventSink {
        .context = const_cast<BuildEventSink*>(rstd::addressof(observer)),
        .notify  = forward_source_event,
    };
}

auto source_observer(const Option<BuildEventSink>& observer) noexcept -> SourceEventSink {
    if (observer.is_none()) return {};
    return source_observer(*observer);
}

} // namespace lito
