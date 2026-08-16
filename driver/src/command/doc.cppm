export module lito.driver:command.doc;

export import :command.doc.event;
export import :command.doc.request;
export import :command.doc.result;
export import :command.doc_error;

export namespace lito
{

auto doc(DocRequest request) -> DocResult<DocSummary>;

} // namespace lito
