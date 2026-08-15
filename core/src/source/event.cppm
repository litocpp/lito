export module lito.core:source.event;

import rstd;

using namespace rstd::prelude;

export namespace lito
{

enum class SourceEventKind
{
    Fetch,
};

struct SourceEvent {
    SourceEventKind       kind { SourceEventKind::Fetch };
    ref<str>              source;
    ref<rstd::path::Path> destination;
};

struct SourceEventSink {
    void* context {};
    void (*notify)(void*, const SourceEvent&) noexcept {};
};

} // namespace lito
