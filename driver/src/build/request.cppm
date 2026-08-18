export module lito.driver:build.request;

import rstd;
import lito.core;
import :build.event;
import :build.setup_report;
import lito.cpp;
import lito.system;

using namespace rstd::prelude;
using namespace lito::system;

export namespace lito
{

struct ScanExecutionPolicy {
    Option<usize> jobs;
    Option<usize> max_in_flight;
};

struct CompileExecutionPolicy {
    Option<usize> jobs;
    Option<usize> max_in_flight;
};

struct BuildExecutionPolicy {
    ScanExecutionPolicy    scan;
    CompileExecutionPolicy compile;
};

struct BuildRequest {
    lito::package::PackageSelection           selection;
    Vec<String>                               targets;
    Vec<lito::package::PackageTargetId>       exact_targets;
    PathBuf                                   output;
    ProcessEnvironmentSpec                    environment;
    cpp::BuildConfiguration                   configuration;
    lito::lock::LockConfig                    lock;
    Option<lito::manifest::BuildProfileName>  profile;
    lito::source::PackageSourceConfig         sources;
    lito::dependency::PkgConfigProviderConfig pkg_config;
    lito::dependency::CMakeProviderConfig     cmake;
    lito::dependency::CMakeBuildOverrideSet   cmake_build_overrides;
    lito::package::PackageSelectionPurpose    purpose {
        lito::package::PackageSelectionPurpose::Production
    };
    bool                         locked { false };
    BuildExecutionPolicy         execution;
    Option<BuildEventSink>       observer;
    Option<BuildSetupReportSink> setup_reporter;
};

} // namespace lito
