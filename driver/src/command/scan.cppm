export module lito.driver:command.scan;

import rstd;
import lito.core;
export import :command.error;
import :build.event;
import lito.cpp;
import lito.toolchain;
import lito.frontend;
import lito.system;

using namespace rstd::prelude;
using namespace lito::system;

export namespace lito
{

struct ScanRequest {
    PackageSelection         selection;
    Vec<String>              targets;
    PathBuf                  source;
    ProcessEnvironmentSpec   environment;
    cpp::BuildConfiguration  configuration;
    LockConfig               lock;
    Option<BuildProfileName> profile;
    PackageSourceConfig      sources;
    PkgConfigProviderConfig  pkg_config;
    CMakeProviderConfig      cmake;
    bool                     locked { false };
    Option<BuildEventSink>   observer;
};

struct ScanReport {
    String                   target;
    String                   profile;
    PathBuf                  primary_output;
    frontend::FrontendResult result;
};

enum class ScanOutputFormat
{
    Lito,
    P1689,
};

auto scan_output_format_name(ScanOutputFormat format) -> ref<str>;
auto parse_scan_output_format(ref<str> name) -> CommandResult<ScanOutputFormat>;
auto scan(const ScanRequest& request) -> CommandResult<ScanReport>;
auto lito_scan_report_json(const ScanReport& report) -> CommandResult<String>;
auto p1689_scan_report_json(const ScanReport& report) -> CommandResult<String>;
auto scan_report_json(const ScanReport& report, ScanOutputFormat format = ScanOutputFormat::Lito)
    -> CommandResult<String>;

} // namespace lito
