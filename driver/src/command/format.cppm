export module lito.driver:command.format;

import rstd;
import lito.core;
import :command.error;
import lito.toolchain;
import lito.system;

using namespace rstd::prelude;
using namespace lito::system;

export namespace lito
{

enum class FormatMode
{
    Write,
    Check,
};

struct FormatRequest {
    PathBuf                        root;
    Vec<String>                    packages;
    ProcessEnvironmentSpec         environment;
    ToolSpec                       tools;
    FormatMode                     mode { FormatMode::Write };
    Option<HostToolResolutionSink> tool_reporter;
};

struct FormatSummary {
    usize        packages {};
    usize        files {};
    Vec<PathBuf> unformatted_files;

    auto success() const noexcept -> bool { return unformatted_files.is_empty(); }
};

auto format(const FormatRequest& request) -> CommandResult<FormatSummary>;

} // namespace lito
