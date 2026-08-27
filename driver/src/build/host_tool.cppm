module;
#include <rstd/macro.hpp>

export module lito.driver:build.host_tool;

import rstd;
import lito.crypto;
import lito.tools;
import rstd.json;
import lito.core;
import :build.event;
import :build.layout;
import :build.host_tool_error;
import :source;
import lito.cpp;
import lito.system;

using namespace rstd::prelude;
using namespace lito::system;
using namespace rstd::literals;

namespace lito
{

using Json = rstd::json::Value;

template<typename T>
auto host_tool_io_failure(ref<str>               operation,
                          ref<rstd::path::Path>  path,
                          rstd::io::error::Error error) -> HostBuildToolResult<T> {
    return Err(HostBuildToolError::System(
        SystemError::Io(String::make(operation), PathBuf::from(path), rstd::move(error))));
}

auto requested_package(const Vec<String>& packages, ref<str> name) noexcept -> bool {
    for (const auto& package : packages) {
        if (package == name) return true;
    }
    return false;
}

auto executable_digest(ref<rstd::path::Path> path) -> HostBuildToolResult<String> {
    auto data = rstd::fs::read(path);
    if (data.is_err()) {
        return Err(HostBuildToolError::System(
            SystemError::Io(String::make("read host build-tool executable"_str),
                            PathBuf::from(path),
                            rstd::move(data).unwrap_err())));
    }
    return Ok(lito::crypto::sha256_hex(data->as_slice()));
}

auto host_tool_receipt_identity(const cpp::PackageBuildToolRequirement&         owned,
                                const lito::manifest::BuildToolArchiveManifest& archive,
                                const HostInfo&                                 host,
                                ref<str>                                        source_identity,
                                ref<str>                                        digest) -> String {
    return lito::crypto::sha256_hex(
        rstd::format("host-build-tool-v2\n{}\n{}\n{}\n{}\n{}\n{}\n{}\n{}\n{}\n{}",
                     owned.package.as_str(),
                     owned.requirement.alias.as_str(),
                     owned.requirement.version.as_str(),
                     host.os.as_str(),
                     architecture_name(host.architecture),
                     archive.url.as_str(),
                     archive.sha256,
                     owned.requirement.executable.as_path(),
                     source_identity,
                     digest)
            .as_str());
}

auto host_tool_receipt_matches(ref<rstd::path::Path>                           receipt,
                               const cpp::PackageBuildToolRequirement&         owned,
                               const lito::manifest::BuildToolArchiveManifest& archive,
                               const HostInfo&                                 host,
                               ref<str>                                        source_identity,
                               ref<str>                                        executable_digest,
                               ref<str> receipt_identity) -> HostBuildToolResult<bool> {
    auto contents = rstd::fs::read_to_string(receipt);
    if (contents.is_err()) {
        auto error = rstd::move(contents).unwrap_err();
        if (error.kind() == rstd::io::error::ErrorKind { rstd::io::error::ErrorKind::NotFound }) {
            return Ok(false);
        }
        return host_tool_io_failure<bool>(
            "read host build-tool receipt"_str, receipt, rstd::move(error));
    }
    auto parsed = rstd::json::from_str(contents->as_str());
    if (parsed.is_err()) return Ok(false);
    const auto matches = [&parsed](ref<str> key, ref<str> expected) {
        auto value = parsed->get(key);
        return value.is_some() && (**value).as_str() == Some(expected);
    };
    return Ok(matches("format"_str, "lito-host-build-tool"_str) &&
              parsed->get("version"_str).is_some() &&
              (**parsed->get("version"_str)).as_u64() == Some(u64(1)) &&
              matches("package"_str, owned.package.as_str()) &&
              matches("alias"_str, owned.requirement.alias.as_str()) &&
              matches("declared_version"_str, owned.requirement.version.as_str()) &&
              matches("host_os"_str, host.os.as_str()) &&
              matches("host_architecture"_str, architecture_name(host.architecture)) &&
              matches("url"_str, archive.url.as_str()) &&
              matches("archive_sha256"_str, archive.sha256.to_hex().as_str()) &&
              matches("executable"_str,
                      owned.requirement.executable.as_path().to_string_lossy().as_str()) &&
              matches("source_identity"_str, source_identity) &&
              matches("executable_digest"_str, executable_digest) &&
              matches("receipt_identity"_str, receipt_identity));
}

auto write_host_tool_receipt(ref<rstd::path::Path>                           receipt,
                             const cpp::PackageBuildToolRequirement&         owned,
                             const lito::manifest::BuildToolArchiveManifest& archive,
                             const HostInfo&                                 host,
                             ref<str>                                        source_identity,
                             ref<str>                                        executable_digest,
                             ref<str> receipt_identity) -> HostBuildToolResult<empty> {
    auto document = rstd::json::Map::make();
    document.insert(String::make("format"_str),
                    Json::String(String::make("lito-host-build-tool"_str)));
    document.insert(String::make("version"_str),
                    Json::Number(rstd::json::Number::from_u64(u64(1))));
    document.insert(String::make("package"_str), Json::String(owned.package.clone()));
    document.insert(String::make("alias"_str), Json::String(owned.requirement.alias.clone()));
    document.insert(String::make("declared_version"_str),
                    Json::String(owned.requirement.version.clone()));
    document.insert(String::make("host_os"_str), Json::String(host.os.clone()));
    document.insert(String::make("host_architecture"_str),
                    Json::String(String::make(architecture_name(host.architecture))));
    document.insert(String::make("url"_str), Json::String(String::make(archive.url.as_str())));
    document.insert(String::make("archive_sha256"_str), Json::String(archive.sha256.to_hex()));
    document.insert(String::make("executable"_str),
                    Json::String(owned.requirement.executable.as_path().to_string_lossy()));
    document.insert(String::make("source_identity"_str),
                    Json::String(String::make(source_identity)));
    document.insert(String::make("executable_digest"_str),
                    Json::String(String::make(executable_digest)));
    document.insert(String::make("receipt_identity"_str),
                    Json::String(String::make(receipt_identity)));
    auto text =
        rstd::json::to_string(Json::Object(rstd::move(document)),
                              rstd::json::FormatOptions { .pretty = true, .indent = usize(2) });
    text.push_ascii('\n');
    auto written = rstd::fs::write_atomic(receipt, text.as_str().as_bytes());
    if (written.is_err()) {
        return host_tool_io_failure<empty>(
            "write host build-tool receipt"_str, receipt, rstd::move(written).unwrap_err());
    }
    return Ok(empty {});
}

void emit_host_tool(BuildEventSink        observer,
                    BuildEventKind        kind,
                    ref<str>              alias,
                    ref<rstd::path::Path> executable) noexcept {
    if (observer.notify != nullptr)
        observer.notify(observer.context, BuildEvent { kind, alias, executable });
}

} // namespace lito

export namespace lito
{

auto select_host_build_tool_archive(const lito::manifest::BuildToolRequirement& requirement,
                                    const HostInfo&                             host)
    -> HostBuildToolResult<ref<lito::manifest::BuildToolArchiveManifest>> {
    for (const auto& archive : requirement.archives) {
        if (archive.host.os == host.os.as_str() && archive.host.architecture == host.architecture) {
            return Ok(ref<lito::manifest::BuildToolArchiveManifest>::from_raw_parts(
                rstd::addressof(archive)));
        }
    }
    return Err(
        HostBuildToolError::UnsupportedHost(requirement.alias.clone(),
                                            host.os.clone(),
                                            String::make(architecture_name(host.architecture))));
}

auto resolve_host_build_tool_archives(const lito::package::ResolvedPackageGraph& graph,
                                      const Vec<String>&                         packages,
                                      const HostInfo&                            host)
    -> HostBuildToolResult<Vec<lito::source::ArchiveSourceFetchRequest>> {
    auto requests = Vec<lito::source::ArchiveSourceFetchRequest>::make();
    for (const auto& package : graph.packages) {
        if (! requested_package(packages, package.manifest.name.as_str())) continue;
        for (const auto& tool : package.manifest.build_tools) {
            auto archive = rstd_try(select_host_build_tool_archive(tool, host));
            requests.push(lito::source::ArchiveSourceFetchRequest {
                .owner  = package.manifest.name.clone(),
                .name   = tool.alias.clone(),
                .url    = archive->url.fetch_url()->clone(),
                .sha256 = archive->sha256.clone(),
            });
        }
    }
    return Ok(rstd::move(requests));
}

struct ResolvedHostBuildTool {
    String   package;
    String   alias;
    String   version;
    HostInfo host;
    PathBuf  executable;
    String   source_identity;
    String   receipt_identity;
};

class ResolvedHostBuildTools {
public:
    auto get(ref<str> package, ref<str> alias) const noexcept
        -> Option<ref<ResolvedHostBuildTool>> {
        for (const auto& tool : tools_) {
            if (tool->package == package && tool->alias == alias)
                return Some(ref<ResolvedHostBuildTool>::from_raw_parts(rstd::addressof(*tool)));
        }
        return None();
    }

    auto get(ref<str> alias) const noexcept -> Option<ref<ResolvedHostBuildTool>> {
        auto result = Option<ref<ResolvedHostBuildTool>> {};
        for (const auto& tool : tools_) {
            if (tool->alias != alias) continue;
            if (result.is_some()) return None();
            result = Some(ref<ResolvedHostBuildTool>::from_raw_parts(rstd::addressof(*tool)));
        }
        return result;
    }

    auto identity(ref<str> package, ref<str> alias) const noexcept -> const void* {
        auto tool = get(package, alias);
        return tool.is_some() ? static_cast<const void*>(rstd::addressof(**tool)) : nullptr;
    }

    auto identity(ref<str> alias) const noexcept -> const void* {
        auto tool = get(alias);
        return tool.is_some() ? static_cast<const void*>(rstd::addressof(**tool)) : nullptr;
    }

    auto from_identity(const void* identity) const noexcept -> Option<ref<ResolvedHostBuildTool>> {
        for (const auto& tool : tools_) {
            if (static_cast<const void*>(rstd::addressof(*tool)) == identity)
                return Some(ref<ResolvedHostBuildTool>::from_raw_parts(rstd::addressof(*tool)));
        }
        return None();
    }

    void push(Box<ResolvedHostBuildTool> tool) { tools_.push(rstd::move(tool)); }

private:
    Vec<Box<ResolvedHostBuildTool>> tools_;
};

auto resolve_host_build_tools(const cpp::PackageMetadata&              metadata,
                              const Vec<String>&                       packages,
                              const HostInfo&                          host,
                              const BuildLayout&                       layout,
                              lito::tools::ToolResolver&               resolver,
                              const ResolvedProcessEnvironment&        environment,
                              const lito::source::PackageSourceConfig& sources,
                              usize                                    jobs,
                              BuildEventSink                           observer = {})
    -> HostBuildToolResult<ResolvedHostBuildTools> {
    auto requirements = Vec<ref<cpp::PackageBuildToolRequirement>>::make();
    auto selected     = Vec<ref<lito::manifest::BuildToolArchiveManifest>>::make();
    auto archives     = Vec<lito::source::ArchiveSourceFetchRequest>::make();
    for (const auto& owned : metadata.build_tools) {
        if (! requested_package(packages, owned.package.as_str())) continue;
        auto archive = rstd_try(select_host_build_tool_archive(owned.requirement, host));
        requirements.push(
            ref<cpp::PackageBuildToolRequirement>::from_raw_parts(rstd::addressof(owned)));
        archives.push(lito::source::ArchiveSourceFetchRequest {
            .owner  = owned.package.clone(),
            .name   = owned.requirement.alias.clone(),
            .url    = archive->url.fetch_url()->clone(),
            .sha256 = archive->sha256.clone(),
        });
        selected.push(rstd::move(archive));
    }
    auto result = ResolvedHostBuildTools {};
    if (requirements.is_empty()) return Ok(rstd::move(result));
    auto materialization_root = layout.source_materialization_root();
    auto acquired =
        rstd_try(lito::source::acquire_archive_frontier(rstd::move(archives),
                                                        jobs,
                                                        materialization_root.as_path(),
                                                        resolver,
                                                        environment,
                                                        sources,
                                                        lito::source::SourceEventSink {}));
    for (usize index {}; index < requirements.len(); ++index) {
        const auto& owned       = *requirements[index];
        const auto& requirement = owned.requirement;
        auto        executable  = acquired[index].root.join(requirement.executable.as_path());
        auto        inspected   = rstd::fs::symlink_metadata(executable.as_path());
        if (inspected.is_err() || inspected->is_symlink() || ! inspected->is_file()) {
            return Err(HostBuildToolError::MissingExecutable(requirement.alias.clone(),
                                                             rstd::move(executable)));
        }
        auto canonical = rstd::fs::canonicalize(executable.as_path());
        if (canonical.is_err() ||
            canonical->as_path().strip_prefix(acquired[index].root.as_path()).is_none()) {
            return Err(HostBuildToolError::MissingExecutable(requirement.alias.clone(),
                                                             rstd::move(executable)));
        }
        auto executable_text = canonical->as_path().to_str();
        if (executable_text.is_none()) {
            return Err(HostBuildToolError::MissingExecutable(requirement.alias.clone(),
                                                             rstd::move(canonical).unwrap()));
        }
        const auto& archive = *selected[index];
        auto        key     = lito::crypto::sha256_hex(
            rstd::format("host-build-tool-store-v1\n{}\n{}\n{}\n{}\n{}\n{}\n{}\n{}\n{}",
                         owned.package.as_str(),
                         requirement.alias.as_str(),
                         requirement.version.as_str(),
                         host.os.as_str(),
                         architecture_name(host.architecture),
                         archive.url.as_str(),
                         archive.sha256,
                         requirement.executable.as_path(),
                         acquired[index].identity.as_str())
                .as_str());
        auto area    = layout.host_build_tool_root().join(PathBuf::from(key).as_path());
        auto created = rstd::fs::create_dir_all(area.as_path());
        if (created.is_err()) {
            return host_tool_io_failure<ResolvedHostBuildTools>("create host build-tool store"_str,
                                                                area.as_path(),
                                                                rstd::move(created).unwrap_err());
        }
        auto lock_path = area.join(PathBuf::from("lock"_str).as_path());
        auto opened    = rstd::fs::OpenOptions::make().read(true).write(true).create(true).open(
            lock_path.as_path());
        if (opened.is_err()) {
            return host_tool_io_failure<ResolvedHostBuildTools>("open host build-tool lock"_str,
                                                                lock_path.as_path(),
                                                                rstd::move(opened).unwrap_err());
        }
        auto locked = rstd::fs::FileLock::acquire(rstd::move(opened).unwrap(),
                                                  rstd::fs::FileLockMode::Exclusive);
        if (locked.is_err()) {
            return host_tool_io_failure<ResolvedHostBuildTools>("lock host build-tool store"_str,
                                                                lock_path.as_path(),
                                                                rstd::move(locked).unwrap_err());
        }
        auto digest           = rstd_try(executable_digest(canonical->as_path()));
        auto receipt_identity = host_tool_receipt_identity(
            owned, archive, host, acquired[index].identity.as_str(), digest.as_str());
        auto receipt  = area.join(PathBuf::from("receipt.json"_str).as_path());
        auto reusable = rstd_try(host_tool_receipt_matches(receipt.as_path(),
                                                           owned,
                                                           archive,
                                                           host,
                                                           acquired[index].identity.as_str(),
                                                           digest.as_str(),
                                                           receipt_identity.as_str()));
        if (reusable) {
            emit_host_tool(observer,
                           BuildEventKind::BuildToolReuse,
                           requirement.alias.as_str(),
                           canonical->as_path());
            result.push(Box<ResolvedHostBuildTool>::make(ResolvedHostBuildTool {
                .package          = owned.package.clone(),
                .alias            = requirement.alias.clone(),
                .version          = requirement.version.clone(),
                .host             = host.clone(),
                .executable       = rstd::move(canonical).unwrap(),
                .source_identity  = acquired[index].identity.clone(),
                .receipt_identity = rstd::move(receipt_identity),
            }));
            continue;
        }
        auto arguments = Vec<String>::make();
        arguments.push(String::make(*executable_text));
        arguments.push(String::make("--version"_str));
        auto probed = rstd_try(run_command(arguments, environment));
        auto actual = String::make(probed.standard_output.as_str().trim_ascii());
        if (probed.exit_code != i32 {} || actual != requirement.version.as_str()) {
            return Err(HostBuildToolError::Version(requirement.alias.clone(),
                                                   requirement.version.clone(),
                                                   rstd::move(actual),
                                                   rstd::move(canonical).unwrap()));
        }
        rstd_try(write_host_tool_receipt(receipt.as_path(),
                                         owned,
                                         archive,
                                         host,
                                         acquired[index].identity.as_str(),
                                         digest.as_str(),
                                         receipt_identity.as_str()));
        emit_host_tool(observer,
                       BuildEventKind::BuildToolFetch,
                       requirement.alias.as_str(),
                       canonical->as_path());
        result.push(Box<ResolvedHostBuildTool>::make(ResolvedHostBuildTool {
            .package          = owned.package.clone(),
            .alias            = requirement.alias.clone(),
            .version          = requirement.version.clone(),
            .host             = host.clone(),
            .executable       = rstd::move(canonical).unwrap(),
            .source_identity  = acquired[index].identity.clone(),
            .receipt_identity = rstd::move(receipt_identity),
        }));
    }
    return Ok(rstd::move(result));
}

} // namespace lito
