export module lito.driver:command.format;

import rstd;
import lito.core;
import :command.error;
import :build.event;
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
    lito::package::PackageSelection   selection;
    ProcessEnvironmentSpec            environment;
    lito::config::ToolchainSpec       toolchain;
    lito::lock::LockConfig            lock;
    lito::source::PackageSourceConfig sources;
    FormatMode                        mode { FormatMode::Write };
    Option<BuildEventSink>            observer;
};

struct FormatSummary {
    usize        packages {};
    usize        files {};
    Vec<PathBuf> unformatted_files;

    auto success() const noexcept -> bool { return unformatted_files.is_empty(); }
};

auto format(const FormatRequest& request) -> CommandResult<FormatSummary>;

} // namespace lito
