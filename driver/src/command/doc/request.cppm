export module lito.driver:command.doc.request;

import rstd;
import lito.core;
import :build.request;
import :command.doc.event;

using namespace rstd::prelude;

export namespace lito
{

struct DocRequest {
    BuildRequest         build;
    DocConfig            config;
    PathBuf              output;
    PathBuf              data_output;
    Option<PathBuf>      frontend;
    bool                 data_only { false };
    Option<DocEventSink> observer;
};

} // namespace lito
