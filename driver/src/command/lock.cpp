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
import :registry.http;
import :registry.index;

using namespace rstd::prelude;
using namespace rstd::literals;
using PathBuf = rstd::path::PathBuf;
using namespace lito::system;

namespace lito
{

struct RegistryFlatpakResolver {
    const lito::config::LitoBootstrapConfig* config {};
    PathBuf                                  cache;
    lito::registry::RegistryHttpTransport    http;

    static auto resolve(void*                                    raw,
                        const lito::registry::RegistryPackageId& package,
                        const lito::registry::SemanticVersion&   version,
                        const lito::registry::PackageChecksum&   checksum) noexcept
        -> lito::lock::LockResult<lito::lock::RegistryFlatpakSource> {
        auto& self       = *static_cast<RegistryFlatpakResolver*>(raw);
        auto  configured = self.config == nullptr
                               ? Option<ref<lito::config::NamedRegistryConfig>> {}
                               : self.config->resolve_registry(package.registry.as_str());
        if (configured.is_none()) {
            return Err(lito::lock::LockError::Schema(
                rstd::format("Registry '{}' is not configured", package.registry.as_str())));
        }
        auto indices =
            lito::registry::RegistryIndexClient(self.cache.clone(),
                                                **configured,
                                                lito::registry::RegistryNetworkPolicy::Online,
                                                lito::registry::RegistryIndexUpdatePolicy::Reuse,
                                                self.http);
        auto record = indices.source_bundle_record(package, version, checksum);
        if (record.is_err()) {
            return Err(lito::lock::LockError::Schema(
                rstd::format("cannot export Registry index for '{}': {}",
                             lito::registry::registry_package_id_text(package).as_str(),
                             rstd::move(record).unwrap_err().message)));
        }
        return Ok(lito::lock::RegistryFlatpakSource {
            .download_url =
                (**configured).effective_endpoints()->download.render(package.name, version),
            .index_record = rstd::move(record).unwrap(),
        });
    }

    auto provider() noexcept -> lito::lock::RegistryFlatpakSourceProvider {
        return lito::lock::RegistryFlatpakSourceProvider {
            .context = this,
            .resolve = resolve,
        };
    }
};

auto has_registry_sources(const lito::lock::LockedProject& project) -> bool {
    for (const auto& package : project.packages) {
        if (package.source.is_some() && package.source->is_Registry()) return true;
    }
    return false;
}

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
    source_config.package_patches.clear();
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
    auto registry_provider    = lito::lock::RegistryFlatpakSourceProvider {};
    auto registry_environment = Option<ResolvedProcessEnvironment> {};
    auto registry_http        = Option<lito::registry::CurlRegistryHttpTransport> {};
    auto registry_resolver    = Option<RegistryFlatpakResolver> {};
    if (has_registry_sources(*locked)) {
        if (request.registries.is_none()) {
            return Err(CommandError::Message(
                String::make("Flatpak Registry source export has no Registry configuration"_str)));
        }
        auto environment = ResolvedProcessEnvironment::resolve(request.environment);
        if (environment.is_err()) {
            return Err(rstd::into<CommandError>(rstd::move(environment).unwrap_err()));
        }
        registry_environment = Some(rstd::move(environment).unwrap());
        auto tools           = lito::tools::ToolResolver(
            *registry_environment, request.tools.clone(), request.tool_reporter);
        auto curl = tools.require(
            lito::tools::Tool::Curl,
            lito::tools::command_tool_requirement(lito::tools::HostToolCapability::HttpDownload,
                                                  "Flatpak Registry source export"_str));
        if (curl.is_err()) {
            return Err(rstd::into<CommandError>(rstd::move(curl).unwrap_err()));
        }
        registry_http = Some(lito::registry::CurlRegistryHttpTransport(curl->executable.clone(),
                                                                       *registry_environment));
        auto data     = lito::system::LitoDataRoot::resolve();
        if (data.is_err()) {
            return Err(rstd::into<CommandError>(rstd::move(data).unwrap_err()));
        }
        registry_resolver = Some(RegistryFlatpakResolver {
            .config = rstd::addressof(*request.registries),
            .cache  = PathBuf::from(data->root()),
            .http   = registry_http->transport(),
        });
        registry_provider = registry_resolver->provider();
    }
    auto sources = lito::lock::project_flatpak_sources(*locked, registry_provider);
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
