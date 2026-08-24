module;
#include <rstd/macro.hpp>

export module lito.tools.cmake:executor;

import rstd;
import lito.tools;
import lito.system;
import :model;
import :invocation;
import :file_api;
import :snapshot;

using namespace rstd::prelude;
using namespace lito::system;
using namespace rstd::literals;

export namespace lito::tools::cmake
{

auto plan_cmake_package(const Request&                requirement,
                        const Provider&               provider,
                        const ToolchainConfiguration& toolchain,
                        const ProfileConfiguration&   profile,
                        ref<str>                      effective_target,
                        ref<rstd::path::Path>         profile_cmake_root,
                        usize jobs = usize(1)) -> lito::tools::ToolResult<CMakePackagePlan> {
    if (jobs == usize {}) {
        return cmake_failure<CMakePackagePlan>("CMake build jobs must be greater than zero"_str);
    }
    auto area =
        work_area(requirement, provider, toolchain, profile, effective_target, profile_cmake_root);
    if (area.is_err()) return Err(rstd::move(area).unwrap_err());
    auto operations = Vec<CMakePackageOperation>::make();
    if (requirement.source.is_Directory() && requirement.adapter.is_none()) {
        operations.push(CMakePackageOperation::ConfigureSource);
        operations.push(CMakePackageOperation::BuildSource);
        operations.push(CMakePackageOperation::InstallSource);
    }
    operations.push(CMakePackageOperation::WriteQuery);
    operations.push(CMakePackageOperation::ConfigureQuery);
    operations.push(CMakePackageOperation::BuildQuery);
    operations.push(CMakePackageOperation::ReadUsage);
    return Ok(CMakePackagePlan {
        .requirement      = clone_cmake_requirement(requirement),
        .provider         = clone_provider(provider),
        .toolchain        = clone_toolchain(toolchain),
        .profile          = clone_profile(profile),
        .area             = rstd::move(area).unwrap(),
        .effective_target = String::make(effective_target),
        .operations       = rstd::move(operations),
        .jobs             = jobs,
    });
}

auto identify_cmake_provider(Provider provider, const ResolvedProcessEnvironment& environment)
    -> lito::tools::ToolResult<Provider> {
    auto executable = path_text(provider.executable.as_path(), "CMake executable"_str);
    if (executable.is_err()) return Err(rstd::move(executable).unwrap_err());
    auto arguments = Vec<String>::make();
    arguments.push(rstd::move(executable).unwrap());
    arguments.push(String::make("--version"_str));
    auto output = invoke_cmake(arguments, environment, None(), false);
    if (output.is_err()) {
        return Err(rstd::into<lito::tools::ToolError>(rstd::move(output).unwrap_err()));
    }
    if (output->exit_code != i32 {}) {
        return Err(lito::tools::ToolError::Execution(String::make("CMake provider identity"_str),
                                                     output->exit_code,
                                                     rstd::move(output->standard_output),
                                                     rstd::move(output->standard_error)));
    }
    provider.identity = String::make(output->standard_output.as_str().trim_ascii());
    if (provider.identity.is_empty()) {
        return cmake_failure<Provider>("CMake provider returned an empty identity"_str);
    }
    return Ok(rstd::move(provider));
}

auto execute_cmake_package(const CMakePackagePlan&           plan,
                           const ResolvedProcessEnvironment& environment,
                           const Option<EventSink>&          observer = None())
    -> lito::tools::ToolResult<CMakeUsageSnapshot> {
    const auto& requirement = plan.requirement;
    const auto& provider    = plan.provider;
    const auto& toolchain   = plan.toolchain;
    const auto& profile     = plan.profile;
    const auto& area        = plan.area;
    auto        created     = rstd::fs::create_dir_all(area.root.as_path());
    if (created.is_err()) {
        return cmake_io_failure<CMakeUsageSnapshot>("create CMake work directory"_str,
                                                    area.root.as_path(),
                                                    rstd::move(created).unwrap_err());
    }
    auto lock_path = area.root.join(PathBuf::from("lock"_str).as_path());
    auto lock_file = rstd::fs::File::create(lock_path.as_path());
    if (lock_file.is_err()) {
        return cmake_io_failure<CMakeUsageSnapshot>("open CMake dependency lock"_str,
                                                    lock_path.as_path(),
                                                    rstd::move(lock_file).unwrap_err());
    }
    auto locked = rstd::fs::FileLock::acquire(rstd::move(lock_file).unwrap(),
                                              rstd::fs::FileLockMode::Exclusive);
    if (locked.is_err()) {
        return cmake_io_failure<CMakeUsageSnapshot>(
            rstd::format("lock CMake dependency '{}'", requirement.alias.as_str()).as_str(),
            lock_path.as_path(),
            rstd::move(locked).unwrap_err());
    }
    auto cacheable =
        requirement.source.is_Directory() && requirement.source.as_Directory().cacheable;
    if (cacheable) {
        auto cached = read_usage_snapshot(area, requirement);
        if (cached.is_err()) return Err(rstd::move(cached).unwrap_err());
        if (cached->is_some()) {
            emit_cmake(
                observer, EventKind::Reuse, requirement.alias.as_str(), area.query_root.as_path());
            return Ok(rstd::move(cached).unwrap().unwrap());
        }
    }
    auto install_current = cacheable ? rstd_try(source_install_current(area)) : false;
    auto snapshots       = Option<CMakeUsageSnapshot> {};
    for (auto operation : plan.operations) {
        switch (operation) {
        case CMakePackageOperation::ConfigureSource:
            if (! install_current) {
                rstd_try(with_operation_context(
                    execute_observed(
                        observer,
                        EventKind::Configure,
                        requirement.alias.as_str(),
                        area.build.as_path(),
                        [&] {
                            return configure_source(
                                requirement, provider, toolchain, profile, area, environment);
                        }),
                    plan,
                    operation));
            }
            break;
        case CMakePackageOperation::BuildSource:
            if (! install_current) {
                rstd_try(with_operation_context(
                    execute_observed(observer,
                                     EventKind::Build,
                                     requirement.alias.as_str(),
                                     area.build.as_path(),
                                     [&] {
                                         return build_source(
                                             requirement, provider, area, plan.jobs, environment);
                                     }),
                    plan,
                    operation));
            }
            break;
        case CMakePackageOperation::InstallSource:
            if (! install_current) {
                rstd_try(with_operation_context(
                    execute_observed(observer,
                                     EventKind::Install,
                                     requirement.alias.as_str(),
                                     area.install.as_path(),
                                     [&] {
                                         return install_source(
                                             requirement, provider, area, cacheable, environment);
                                     }),
                    plan,
                    operation));
                install_current = true;
            } else {
                emit_cmake(
                    observer, EventKind::Reuse, requirement.alias.as_str(), area.install.as_path());
            }
            break;
        case CMakePackageOperation::WriteQuery:
            rstd_try(with_operation_context(write_probe_files(requirement, area), plan, operation));
            break;
        case CMakePackageOperation::ConfigureQuery:
            rstd_try(with_operation_context(
                execute_observed(
                    observer,
                    EventKind::Query,
                    requirement.alias.as_str(),
                    area.query_build.as_path(),
                    [&] {
                        return configure_probe(
                            requirement, provider, toolchain, profile, area, environment);
                    }),
                plan,
                operation));
            break;
        case CMakePackageOperation::BuildQuery:
            rstd_try(with_operation_context(
                execute_observed(observer,
                                 EventKind::QueryBuild,
                                 requirement.alias.as_str(),
                                 area.query_build.as_path(),
                                 [&] {
                                     return build_probe(
                                         requirement, provider, area, plan.jobs, environment);
                                 }),
                plan,
                operation));
            break;
        case CMakePackageOperation::ReadUsage:
            snapshots = Some(rstd_try(with_operation_context(
                execute_observed(observer,
                                 EventKind::Snapshot,
                                 requirement.alias.as_str(),
                                 area.query_root.as_path(),
                                 [&] {
                                     return read_probe_snapshots(area, requirement);
                                 }),
                plan,
                operation)));
            break;
        }
    }
    if (snapshots.is_none()) {
        return cmake_failure<CMakeUsageSnapshot>(rstd::format(
            "CMake package '{}' plan produced no usage snapshot", requirement.package.as_str()));
    }
    auto version_path =
        area.query_build.join(PathBuf::from("lito-package-version.txt"_str).as_path());
    auto version = rstd::fs::read_to_string(version_path.as_path());
    if (version.is_err()) {
        return cmake_io_failure<CMakeUsageSnapshot>(
            rstd::format("read CMake package '{}' version", requirement.package.as_str()).as_str(),
            version_path.as_path(),
            rstd::move(version).unwrap_err());
    }
    auto normalized_version = String::make(version->as_str().trim_ascii());
    if (normalized_version.is_empty()) normalized_version = String::make("unknown"_str);
    snapshots->version = rstd::move(normalized_version);
    if (cacheable) rstd_try(write_usage_snapshot(area, *snapshots));
    return Ok(rstd::move(snapshots).unwrap());
}

} // namespace lito::tools::cmake
