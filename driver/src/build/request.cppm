export module lito.driver:build.request;

import rstd;
import lito.core;
import :build.event;
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
    PackageSelection         selection;
    Vec<String>              targets;
    Vec<PackageTargetId>     exact_targets;
    PathBuf                  output;
    ProcessEnvironmentSpec   environment;
    cpp::BuildConfiguration  configuration;
    LockConfig               lock;
    Option<BuildProfileName> profile;
    PackageSourceConfig      sources;
    PkgConfigProviderConfig  pkg_config;
    CMakeProviderConfig      cmake;
    PackageSelectionPurpose  purpose { PackageSelectionPurpose::Production };
    bool                     locked { false };
    BuildExecutionPolicy     execution;
    Option<BuildEventSink>   observer;
};

} // namespace lito
