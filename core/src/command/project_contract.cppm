export module lito.command.project_contract;

import rstd;
import lito.error;
import lito.toolchain.spec;
import lito.system.environment_contract;
import lito.source.contract;
import lito.workspace.contract;
import lito.lock.contract;
import lito.build.contract;

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
    LockConfig             lock;
    PackageSourceConfig    sources;
    FormatMode             mode { FormatMode::Write };
    Option<BuildObserver>  observer;
};

struct UpdateRequest {
    PathBuf                root;
    ProcessEnvironmentSpec environment;
    LockConfig             lock;
    PackageSourceConfig    sources;
    Option<BuildObserver>  observer;
};

struct FormatSummary {
    usize        packages {};
    usize        files {};
    Vec<PathBuf> unformatted_files;

    auto success() const noexcept -> bool { return unformatted_files.is_empty(); }
};

} // namespace lito
