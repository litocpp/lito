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

struct FormatRequest {
    PackageSelection       selection;
    ProcessEnvironmentSpec environment;
    ToolchainSpec          toolchain;
    PackageSourceConfig    sources;
};

struct UpdateRequest {
    PathBuf                root;
    ProcessEnvironmentSpec environment;
    PackageSourceConfig    sources;
};

struct FormatSummary {
    usize packages {};
    usize files {};
};

} // namespace lito
