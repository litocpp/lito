export module lito.driver:command.scan;

import rstd;
import lito.tools;
import lito.tools.cargo;
import lito.core;
import :config.project;
import :config.registry;
import :package.selection;
export import :command.error;
import :build.event;
import :build.setup_report;
import lito.cpp;
import lito.system;

using namespace rstd::prelude;
using namespace lito::system;

export namespace lito
{

struct ScanRequest {
    lito::package::PackageSelection             selection;
    Vec<String>                                 targets;
    PathBuf                                     source;
    ProcessEnvironmentSpec                      environment;
    lito::tools::ToolSpec                       tools;
    Option<config::LitoBootstrapConfig>         registries;
    config::BuildConfigurationRequest           configuration;
    lito::lock::LockConfig                      lock;
    Option<lito::manifest::BuildProfileName>    profile;
    lito::source::PackageSourceConfig           sources;
    lito::tools::cargo::Configuration           cargo;
    lito::dependency::PkgConfigProviderConfig   pkg_config;
    lito::dependency::CMakeProviderConfig       cmake;
    lito::dependency::CMakeBuildOverrideSet     cmake_build_overrides;
    bool                                        locked { false };
    Option<BuildEventSink>                      observer;
    Option<BuildSetupReportSink>                setup_reporter;
    Option<lito::tools::HostToolResolutionSink> tool_reporter;
};

struct ScanReport {
    String          target;
    String          profile;
    PathBuf         primary_output;
    cpp::ScanResult result;
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
