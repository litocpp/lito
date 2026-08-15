export module lito.command.doc_contract;

import rstd;
import lito.error;
import lito.build.contract;
import lito.config.contract;
export import lito.command.doc_error_contract;

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

struct DocObserver {
    void* context {};
    void (*notify)(void*, const DocEvent&) noexcept {};
};

struct DocRequest {
    BuildRequest        build;
    DocConfig           config;
    PathBuf             output;
    PathBuf             data_output;
    Option<PathBuf>     frontend;
    bool                data_only { false };
    Option<DocObserver> observer;
};

struct DocSummary {
    BuildSummary build;
    PathBuf      tool;
    PathBuf      output;
    PathBuf      data_output;
    usize        extracted {};
    usize        reused {};
};

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
