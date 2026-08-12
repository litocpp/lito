export module lito.command.project_contract;

import rstd;
import lito.error;
import lito.toolchain.spec;
import lito.system.environment_contract;
import lito.source.contract;
import lito.workspace.contract;

using namespace rstd::prelude;

export namespace lito
{

enum class FormatMode
{
    Write,
    Check,
};

struct FormatRequest {
    PackageSelection       selection;
    ProcessEnvironmentSpec environment;
    ToolchainSpec          toolchain;
    PackageSourceConfig    sources;
    FormatMode             mode { FormatMode::Write };
};

struct UpdateRequest {
    PathBuf                root;
    ProcessEnvironmentSpec environment;
    PackageSourceConfig    sources;
};

struct FormatSummary {
    usize        packages {};
    usize        files {};
    Vec<PathBuf> unformatted_files;

    auto success() const noexcept -> bool { return unformatted_files.is_empty(); }
};

} // namespace lito
