module;
#include <rstd/macro.hpp>

module lito.driver;

import rstd;
import lito.core;
import lito.tools;
import lito.tools.cargo;
import lito.system;
import :command.lock;
import :source;

using namespace rstd::prelude;
using namespace rstd::literals;
using PathBuf = rstd::path::PathBuf;
using namespace lito::system;

namespace lito
{

auto attachment_path(ref<rstd::path::Path> root, ref<rstd::path::Path> requested)
    -> CommandResult<PathBuf> {
    auto path =
        requested.is_absolute() ? PathBuf::from(requested) : PathBuf::from(root).join(requested);
    auto metadata = rstd::fs::symlink_metadata(path.as_path());
    if (metadata.is_err()) {
        return Err(
            CommandError::System(SystemError::Io(String::make("inspect Cargo lock attachment"_str),
                                                 path.clone(),
                                                 rstd::move(metadata).unwrap_err())));
    }
    if (! metadata->is_file() || metadata->is_symlink()) {
        return Err(CommandError::Message(rstd::format(
            "Cargo lock attachment '{}' must be a regular non-symlink file", path.as_path())));
    }
    auto canonical = rstd::fs::canonicalize(path.as_path());
    if (canonical.is_err()) {
        return Err(
            CommandError::System(SystemError::Io(String::make("resolve Cargo lock attachment"_str),
                                                 rstd::move(path),
                                                 rstd::move(canonical).unwrap_err())));
    }
    return Ok(rstd::move(canonical).unwrap());
}

auto acquire_cargo_git_checkouts(const LockExportRequest&                  request,
                                 const lito::tools::cargo::LockedDocument& document)
    -> CommandResult<Vec<lito::tools::cargo::GitCheckout>> {
    auto git_requests = lito::tools::cargo::locked_git_requests(document);
    auto result       = Vec<lito::tools::cargo::GitCheckout>::with_capacity(git_requests.len());
    if (git_requests.is_empty()) return Ok(rstd::move(result));

    auto environment = ResolvedProcessEnvironment::resolve(request.environment);
    if (environment.is_err()) {
        return Err(rstd::into<CommandError>(rstd::move(environment).unwrap_err()));
    }
    auto resolver =
        lito::tools::ToolResolver(*environment, request.tools.clone(), request.tool_reporter);
    auto source_config = request.sources.clone();
    source_config.patches.clear();
    auto manager =
        lito::source::SourceManager(request.root.as_path(),
                                    lito::source::SourceResolutionOptions {
                                        .git     = lito::source::GitResolutionMode::ReuseLocked,
                                        .sources = rstd::move(source_config),
                                    },
                                    resolver,
                                    *environment);
    auto requests = Vec<lito::source::PackageSourceFetchRequest>::with_capacity(git_requests.len());
    for (const auto& git : git_requests) {
        requests.push(lito::source::PackageSourceFetchRequest {
            .owner  = String::make("Cargo lock attachment"_str),
            .name   = rstd::format("{}#{}", git.url.as_str(), git.commit.as_str()),
            .source = lito::source::PackageSourceRequirement::Git(
                git.url.clone(),
                lito::source::GitReference {
                    .kind  = lito::source::GitReferenceKind::Commit,
                    .value = git.commit.clone(),
                }),
            .declaring_root = request.root.clone(),
        });
    }
    auto acquired = manager.acquire_external_frontier(rstd::move(requests), usize(1));
    if (acquired.is_err()) {
        return Err(rstd::into<CommandError>(rstd::move(acquired).unwrap_err()));
    }
    auto outcomes = rstd::move(acquired).unwrap();
    for (usize index {}; index < outcomes.len(); ++index) {
        result.push(lito::tools::cargo::GitCheckout {
            .url    = git_requests[index].url.clone(),
            .commit = git_requests[index].commit.clone(),
            .root   = rstd::move(outcomes[index].acquired.root),
        });
    }
    return Ok(rstd::move(result));
}

auto export_lock_sources(const LockExportRequest& request) -> CommandResult<LockExportSummary> {
    if (request.format != lito::lock::LockExportFormat::FlatpakSources) {
        return Err(CommandError::Message(String::make("unsupported lock export format"_str)));
    }
    auto locked = lito::lock::load_locked_project(request.root.as_path(), request.lock);
    if (locked.is_err()) {
        return Err(rstd::into<CommandError>(rstd::move(locked).unwrap_err()));
    }
    auto sources = lito::lock::project_flatpak_sources(*locked);
    if (sources.is_err()) {
        return Err(rstd::into<CommandError>(rstd::move(sources).unwrap_err()));
    }
    auto lito_entries     = sources->entries.len();
    auto attached_entries = usize {};
    if (request.cargo_lock.is_some()) {
        auto path =
            rstd_try(attachment_path(request.root.as_path(), request.cargo_lock->as_path()));
        auto cargo_document = lito::tools::cargo::parse_locked_document(path.as_path());
        if (cargo_document.is_err()) {
            return Err(rstd::into<CommandError>(rstd::move(cargo_document).unwrap_err()));
        }
        auto checkouts = rstd_try(acquire_cargo_git_checkouts(request, *cargo_document));
        auto cargo_sources =
            lito::tools::cargo::project_flatpak_sources(*cargo_document, checkouts);
        if (cargo_sources.is_err()) {
            return Err(rstd::into<CommandError>(rstd::move(cargo_sources).unwrap_err()));
        }
        attached_entries = cargo_sources->entries.len();
        sources->append(rstd::move(cargo_sources).unwrap());
    }
    auto written =
        lito::flatpak::write_sources(request.root.as_path(), request.output.as_path(), *sources);
    if (written.is_err()) {
        return Err(rstd::into<CommandError>(rstd::move(written).unwrap_err()));
    }
    return Ok(LockExportSummary {
        .output           = rstd::move(written).unwrap(),
        .lito_entries     = lito_entries,
        .attached_entries = attached_entries,
    });
}

} // namespace lito
