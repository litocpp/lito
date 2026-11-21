export module lito.driver:command.pack;

import rstd;
import :build.request;
import :command.error;

using namespace rstd::prelude;

export namespace lito
{

struct PackRequest {
    BuildRequest    build;
    String          recipe;
    Option<PathBuf> output;
    bool            no_build { false };
};

auto pack(PackRequest request) -> CommandResult<PathBuf>;

} // namespace lito
