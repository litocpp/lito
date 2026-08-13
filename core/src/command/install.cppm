module;
#include <rstd/macro.hpp>

export module lito.command.install;

import rstd;
import lito.error;
import lito.build;
import lito.install;
import lito.package.identity;
import lito.package.target_contract;
import lito.workspace.contract;
import lito.build.profile_contract;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace lito
{

template<typename T>
auto install_failure(String message) -> InstallResult<T> {
    return Err(InstallError::Message(rstd::move(message)));
}

template<typename T>
auto install_failure(ref<str> message) -> InstallResult<T> {
    return Err(InstallError::Message(String::make(message)));
}

} // namespace lito

export namespace lito
{

auto install(InstallRequest request) -> InstallResult<InstallSummary>;

auto install(InstallRequest request) -> InstallResult<InstallSummary> {
    request.build.selection.root = request.source.project.root.clone();
    request.build.purpose        = PackageSelectionPurpose::Install;
    if (request.build.profile.is_none()) {
        request.build.profile = Some(BuildProfileName { .value = String::make("release"_str) });
    }
    if (! request.build.targets.is_empty()) {
        return install_failure<InstallSummary>(
            "install build targets must be selected through install binaries"_str);
    }
    for (const auto& binary : request.binaries) {
        if (binary.is_empty()) {
            return install_failure<InstallSummary>("install binary name must not be empty"_str);
        }
        for (const auto& prior : request.build.targets) {
            if (prior.as_str().strip_prefix("bin:"_str) == Some(binary.as_str())) {
                return install_failure<InstallSummary>(rstd::format(
                    "install binary '{}' was selected more than once", binary.as_str()));
            }
        }
        request.build.targets.push(rstd::format("bin:{}", binary.as_str()));
    }

    auto requested_packages = Vec<String>::with_capacity(request.build.selection.packages.len());
    for (const auto& package : request.build.selection.packages) {
        requested_packages.push(package.clone());
    }
    auto summary = rstd_try(
        build_resolved_project(rstd::move(request.build), rstd::move(request.source.project)));
    if (summary.selected_targets.is_empty()) {
        return install_failure<InstallSummary>("install selected no binaries"_str);
    }
    for (const auto& requested : requested_packages) {
        auto matched = false;
        for (const auto& package : summary.selected_packages) {
            if (package.name == requested.as_str()) {
                matched = true;
                break;
            }
        }
        if (! matched) {
            return install_failure<InstallSummary>(rstd::format(
                "package '{}' has no selected installable binaries", requested.as_str()));
        }
    }

    auto packages = Vec<InstallPackageRecord>::make();
    for (const auto& package : summary.selected_packages) {
        if (package.version.is_none()) {
            return install_failure<InstallSummary>(
                rstd::format("package '{}' has no installable version", package.name.as_str()));
        }
        auto binaries = Vec<InstallBinary>::make();
        for (const auto& target : summary.selected_targets) {
            if (target.package != package.name.as_str()) continue;
            if (target.kind != PackageTargetKind::Binary) {
                return install_failure<InstallSummary>(
                    rstd::format("selected target '{}' is not an installable binary",
                                 package_target_id_text(target).as_str()));
            }
            const BuiltArtifact* matched = nullptr;
            for (const auto& artifact : summary.artifacts) {
                if (artifact.target != target) continue;
                if (matched != nullptr) {
                    return install_failure<InstallSummary>(
                        rstd::format("build returned duplicate artifact for '{}'",
                                     package_target_id_text(target).as_str()));
                }
                matched = rstd::addressof(artifact);
            }
            if (matched == nullptr || matched->kind != ArtifactKind::Executable) {
                return install_failure<InstallSummary>(
                    rstd::format("build did not return an executable artifact for '{}'",
                                 package_target_id_text(target).as_str()));
            }
            binaries.push(InstallBinary {
                .target = target.clone(),
                .source = matched->path.clone(),
            });
        }
        if (binaries.is_empty()) {
            return install_failure<InstallSummary>(rstd::format(
                "package '{}' has no selected installable binaries", package.name.as_str()));
        }
        packages.push(InstallPackageRecord {
            .name     = package.name.clone(),
            .version  = package.version->clone(),
            .profile  = summary.profile.clone(),
            .target   = summary.target.clone(),
            .binaries = rstd::move(binaries),
        });
    }
    if (packages.is_empty()) {
        return install_failure<InstallSummary>("install selected no packages"_str);
    }
    auto stored = rstd_try(install_artifacts(InstallStoreRequest {
        .root       = InstallRoot { .path = rstd::move(request.root.path) },
        .provenance = rstd::move(request.source.provenance),
        .packages   = rstd::move(packages),
        .force      = request.force,
    }));
    return Ok(InstallSummary {
        .build    = rstd::move(summary),
        .root     = InstallRoot { .path = stored.layout.root.path.clone() },
        .packages = rstd::move(stored.packages),
        .binaries = rstd::move(stored.binaries),
    });
}

} // namespace lito
