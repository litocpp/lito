export module lito.driver:command.doc.event;

import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito
{

enum class DocEventKind
{
    ToolBuild,
    ToolReuse,
    Extract,
    ExtractReuse,
    Generate,
};

struct DocEvent {
    DocEventKind          kind { DocEventKind::Extract };
    ref<str>              target;
    ref<rstd::path::Path> path;
};

struct DocEventSink {
    void* context {};
    void (*notify)(void*, const DocEvent&) noexcept {};
};

} // namespace lito

namespace lito
{

auto emit_doc(const Option<DocEventSink>& observer,
              DocEventKind                kind,
              ref<str>                    target,
              ref<rstd::path::Path>       path) noexcept -> void {
    if (observer.is_none() || observer->notify == nullptr) return;
    observer->notify(observer->context, DocEvent { .kind = kind, .target = target, .path = path });
}

} // namespace lito

export namespace rstd
{

template<>
struct Impl<fmt::Display, lito::DocEventKind> : ImplBase<lito::DocEventKind> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        switch (this->self()) {
        case lito::DocEventKind::ToolBuild: return formatter.write_str("doc tool"_str);
        case lito::DocEventKind::ToolReuse: return formatter.write_str("doc tool reuse"_str);
        case lito::DocEventKind::Extract: return formatter.write_str("doc extract"_str);
        case lito::DocEventKind::ExtractReuse: return formatter.write_str("doc extract reuse"_str);
        case lito::DocEventKind::Generate: return formatter.write_str("doc generate"_str);
        }
        rstd::unreachable();
    }
};

} // namespace rstd
