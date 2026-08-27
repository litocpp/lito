export module lito.driver:command.lock;

import rstd;
import lito.core;
import lito.tools;
import lito.system;
import :command.error;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;
using namespace lito::system;

export namespace lito
{

struct LockExportRequest {
    PathBuf                           root;
    lito::lock::LockConfig            lock;
    lito::lock::LockExportFormat      format { lito::lock::LockExportFormat::FlatpakSources };
    PathBuf                           output;
    Option<PathBuf>                   cargo_lock;
    ProcessEnvironmentSpec            environment;
    lito::tools::ToolSpec             tools;
    lito::source::PackageSourceConfig sources;
    Option<lito::tools::HostToolResolutionSink> tool_reporter;
};

struct LockExportSummary {
    PathBuf output;
    usize   lito_entries {};
    usize   attached_entries {};
};

auto export_lock_sources(const LockExportRequest& request) -> CommandResult<LockExportSummary>;

} // namespace lito
