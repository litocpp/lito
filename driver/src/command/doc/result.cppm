export module lito.driver:command.doc.result;

import rstd;
import lito.core;
import :build.result;

using namespace rstd::prelude;

export namespace lito
{

struct DocSummary {
    BuildSummary build;
    PathBuf      tool;
    PathBuf      output;
    PathBuf      data_output;
    usize        extracted {};
    usize        reused {};
};

} // namespace lito
