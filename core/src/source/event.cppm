export module lito.core:source.event;

import rstd;

using namespace rstd::prelude;

export namespace lito::source
{

enum class SourceEventKind
{
    Fetch,
    Extract,
};

struct SourceEvent {
    SourceEventKind       kind { SourceEventKind::Fetch };
    ref<str>              source;
    ref<rstd::path::Path> destination;
    rstd::time::Duration  elapsed;
    bool                  completed { false };
};

struct SourceEventSink {
    void* context {};
    void (*notify)(void*, const SourceEvent&) noexcept {};
};

} // namespace lito::source
