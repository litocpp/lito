module;
#include <rstd/enum.hpp>
#include <rstd/macro.hpp>

export module lito.driver:install.source;

import rstd;
import rstd.json;
import lito.core;
import :config.project;
import :source.acquisition;
import :registry.graph;
import :install.error;
import :install.destination;
import :install.package;

using namespace rstd::prelude;
using namespace rstd::literals;
using Json    = rstd::json::Value;
using JsonMap = rstd::json::Map;

export namespace lito
{

enum class InstallSourceStorage
{
    BorrowedLocal,
    ManagedCache,
};

class InstallSourceRequirement {
    RSTD_ENUM(InstallSourceRequirement, (LocalProject, (PathBuf requested_root;)))
};

struct RegistryInstallGraphSeed {
    lito::source::ResolvedPackageSource              root;
    Vec<lito::registry::ResolvedRegistryGraphSource> sources;
    bool                                             consumed {};

    static auto resolve(void*, slice<lito::registry::RegistryGraphRequirement>) noexcept
        -> lito::registry::RegistryGraphResult<Vec<lito::registry::ResolvedRegistryGraphSource>>;

    auto provider() noexcept -> lito::registry::RegistryGraphProvider {
        return lito::registry::RegistryGraphProvider {
            .context     = this,
            .root_source = rstd::addressof(root),
            .resolve     = resolve,
        };
    }
};

struct ResolvedInstallSource {
    lito::workspace::ResolvedProjectEntry project;
    InstallSourceProvenance               provenance;
    String                                identity;
    InstallSourceStorage                  storage { InstallSourceStorage::BorrowedLocal };
    Option<PathBuf>                       managed_build_root;
    Option<RegistryInstallGraphSeed>      registry_graph;
};

} // namespace lito

namespace lito
{

template<typename T>
auto install_source_failure(String message) -> InstallSourceResult<T> {
    return Err(InstallSourceError::Message(rstd::move(message)));
}

template<typename T>
auto install_source_failure(ref<str> message) -> InstallSourceResult<T> {
    return Err(InstallSourceError::Message(String::make(message)));
}

auto absolute_root(ref<rstd::path::Path> base, PathBuf root) -> PathBuf {
    if (root.as_path().is_absolute()) return root;
    return PathBuf::from(base).join(root.as_path());
}

auto environment_root(ref<str> variable) -> InstallSourceResult<Option<PathBuf>> {
    auto value = rstd::env::var(variable);
    if (value.is_none() || value->is_empty()) return Ok(Option<PathBuf> {});
    auto path = PathBuf::from(rstd::move(value).unwrap());
    if (! path.as_path().is_absolute()) {
        return install_source_failure<Option<PathBuf>>(
            rstd::format("{} must be an absolute path", variable));
    }
    return Ok(Some(rstd::move(path)));
}

auto path_install_source_key(ref<str> key) -> bool {
    return key == "identity"_str || key == "kind"_str || key == "path"_str;
}

auto git_install_source_key(ref<str> key) -> bool {
    return key == "commit"_str || key == "identity"_str || key == "kind"_str ||
           key == "reference"_str || key == "reference-kind"_str || key == "url"_str;
}

auto registry_install_source_key(ref<str> key) -> bool {
    return key == "archive-format"_str || key == "blob"_str || key == "blob-size"_str ||
           key == "identity"_str || key == "kind"_str || key == "manifest"_str ||
           key == "package"_str || key == "registry"_str || key == "release"_str ||
           key == "source"_str || key == "version"_str;
}

auto required_source_string(const Json& source, ref<str> key) -> InstallSourceResult<String> {
    auto member = source.get(key);
    if (member.is_none()) {
        return install_source_failure<String>(
            rstd::format("installed package source is missing '{}'", key));
    }
    auto value = (**member).as_str();
    if (value.is_none() || value->is_empty()) {
        return install_source_failure<String>(
            rstd::format("installed package source.{} must be a non-empty string", key));
    }
    return Ok(String::make(*value));
}

auto required_source_text(const Json& source, ref<str> key) -> InstallSourceResult<String> {
    auto member = source.get(key);
    if (member.is_none()) {
        return install_source_failure<String>(
            rstd::format("installed package source is missing '{}'", key));
    }
    auto value = (**member).as_str();
    if (value.is_none()) {
        return install_source_failure<String>(
            rstd::format("installed package source.{} must be a string", key));
    }
    return Ok(String::make(*value));
}

auto reject_source_fields(const Json& source, bool (*allowed)(ref<str>))
    -> InstallSourceResult<empty> {
    auto object = source.as_object();
    auto keys   = (**object).keys();
    for (auto key : keys) {
        if (! allowed((*key).as_str())) {
            return install_source_failure<empty>(rstd::format(
                "installed package source contains unknown field '{}'", (*key).as_str()));
        }
    }
    return Ok(empty {});
}

} // namespace lito

auto lito::RegistryInstallGraphSeed::resolve(
    void*                                           raw,
    slice<lito::registry::RegistryGraphRequirement> requirements) noexcept
    -> lito::registry::RegistryGraphResult<Vec<lito::registry::ResolvedRegistryGraphSource>> {
    auto& self = *static_cast<RegistryInstallGraphSeed*>(raw);
    if (self.consumed) {
        return Err(lito::registry::RegistryGraphError {
            .message = String::make("Registry install graph seed was already consumed"_str),
        });
    }
    for (const auto& requirement : requirements) {
        auto present = false;
        for (const auto& source : self.sources) {
            if (source.package.name == requirement.package) {
                present = true;
                break;
            }
        }
        if (! present) {
            return Err(lito::registry::RegistryGraphError {
                .message = rstd::format("Registry install graph seed has no package '{}'",
                                        requirement.package.as_str()),
            });
        }
    }
    self.consumed = true;
    return Ok(rstd::move(self.sources));
}

export namespace lito
{

auto resolve_install_source(InstallSourceRequirement requirement)
    -> InstallSourceResult<ResolvedInstallSource>;

auto resolve_registry_install_source(const lito::registry::RegistryPackageName&       package,
                                     Vec<lito::registry::ResolvedRegistryGraphSource> graph,
                                     ref<rstd::path::Path>                            cache_root)
    -> InstallSourceResult<ResolvedInstallSource>;

auto install_source_identity(const InstallSourceProvenance& provenance)
    -> InstallSourceResult<String>;

auto install_source_provenance(const lito::source::ResolvedPackageSource& source)
    -> InstallSourceResult<InstallSourceProvenance>;

auto serialize_install_source_provenance(const InstallSourceProvenance& provenance)
    -> InstallSourceResult<Json>;

auto parse_install_source_provenance(const Json& source)
    -> InstallSourceResult<InstallSourceProvenance>;

auto resolve_install_root(ref<rstd::path::Path>              invocation_root,
                          Option<PathBuf>                    command_root,
                          const lito::config::InstallConfig& config)
    -> InstallSourceResult<InstallRoot>;

auto resolve_install_destination(ref<rstd::path::Path>              invocation_root,
                                 InstallDestinationRequirement      requirement,
                                 const lito::config::InstallConfig& config)
    -> InstallSourceResult<InstallDestination>;

auto resolve_install_source(InstallSourceRequirement requirement)
    -> InstallSourceResult<ResolvedInstallSource> {
    if (! requirement.is_LocalProject()) {
        return Err(InstallSourceError::Message(
            String::make("unsupported install source requirement"_str)));
    }
    auto project = rstd_try(lito::workspace::resolve_project_entry(
        requirement.as_LocalProject().requested_root.as_path()));
    auto root    = project.root.clone();
    return Ok(ResolvedInstallSource {
        .project    = rstd::move(project),
        .provenance = InstallSourceProvenance::Local(
            root.clone(), lito::source::path_source_identity(root.as_path())),
        .identity = lito::source::path_source_identity(root.as_path()),
        .storage  = InstallSourceStorage::BorrowedLocal,
    });
}

auto resolve_registry_install_source(const lito::registry::RegistryPackageName&       package,
                                     Vec<lito::registry::ResolvedRegistryGraphSource> graph,
                                     ref<rstd::path::Path>                            cache_root)
    -> InstallSourceResult<ResolvedInstallSource> {
    auto selected  = Option<lito::registry::ResolvedRegistryGraphSource> {};
    auto remaining = Vec<lito::registry::ResolvedRegistryGraphSource>::make();
    for (auto& source : graph) {
        if (source.package.name == package) {
            if (selected.is_some()) {
                return install_source_failure<ResolvedInstallSource>(
                    rstd::format("Registry install graph contains package '{}' more than once",
                                 package.as_str()));
            }
            selected = Some(rstd::move(source));
        } else {
            remaining.push(rstd::move(source));
        }
    }
    if (selected.is_none()) {
        return install_source_failure<ResolvedInstallSource>(
            rstd::format("Registry install graph omitted root package '{}'", package.as_str()));
    }
    auto root = rstd::move(selected).unwrap();
    if (! (root.catalog.root().starts_with(root.source.root_directory.as_path()) &&
           root.source.root_directory.as_path().starts_with(root.catalog.root()))) {
        return install_source_failure<ResolvedInstallSource>(
            "Registry install root catalog does not match its materialized source"_str);
    }
    auto identity   = root.source.identity.clone();
    auto provenance = rstd_try(install_source_provenance(root.source));
    auto build_root = PathBuf::from(cache_root)
                          .join(PathBuf::from("registry"_str).as_path())
                          .join(PathBuf::from("builds"_str).as_path())
                          .join(PathBuf::from("sha256"_str).as_path())
                          .join(PathBuf::from(root.release.release.digest().to_hex()).as_path());
    return Ok(ResolvedInstallSource {
        .project =
            lito::workspace::ResolvedProjectEntry {
                .root    = root.source.root_directory.clone(),
                .catalog = rstd::move(root.catalog),
            },
        .provenance         = rstd::move(provenance),
        .identity           = rstd::move(identity),
        .storage            = InstallSourceStorage::ManagedCache,
        .managed_build_root = Some(rstd::move(build_root)),
        .registry_graph     = Some(RegistryInstallGraphSeed {
            .root    = root.source.clone(),
            .sources = rstd::move(remaining),
        }),
    });
}

auto install_source_identity(const InstallSourceProvenance& provenance)
    -> InstallSourceResult<String> {
    if (provenance.is_Registry()) {
        const auto& source = provenance.as_Registry();
        auto expected      = lito::source::registry_source_identity(source.package, source.version);
        if (source.identity != expected.as_str()) {
            return install_source_failure<String>(
                "installed Registry source identity does not match its package and version"_str);
        }
        return Ok(source.identity.clone());
    }
    if (provenance.is_Git()) {
        const auto& source = provenance.as_Git();
        if (! lito::source::git_commit_is_valid(source.commit.as_str()) || source.url.is_empty()) {
            return install_source_failure<String>("installed Git source is invalid"_str);
        }
        auto expected =
            lito::source::git_source_identity(source.url.as_str(), source.commit.as_str());
        if (source.identity != expected.as_str()) {
            return install_source_failure<String>(
                "installed Git source identity does not match its URL and commit"_str);
        }
        return Ok(source.identity.clone());
    }
    const auto& source = provenance.as_Local();
    const auto& root   = source.root;
    if (! root.as_path().is_absolute()) {
        return install_source_failure<String>("installed local source path must be absolute"_str);
    }
    if (source.identity.is_empty() || ! source.identity.as_str().starts_with("path+"_str)) {
        return install_source_failure<String>("installed local source identity is invalid"_str);
    }
    return Ok(source.identity.clone());
}

auto install_source_provenance(const lito::source::ResolvedPackageSource& source)
    -> InstallSourceResult<InstallSourceProvenance> {
    if (source.identity.is_empty()) {
        return install_source_failure<InstallSourceProvenance>(
            "resolved package source identity is empty"_str);
    }
    if (source.kind == lito::source::PackageSourceKind::Path) {
        if (! source.root_directory.as_path().is_absolute()) {
            return install_source_failure<InstallSourceProvenance>(
                "resolved package source root must be absolute"_str);
        }
        return Ok(
            InstallSourceProvenance::Local(source.root_directory.clone(), source.identity.clone()));
    }
    if (source.kind == lito::source::PackageSourceKind::Registry) {
        if (source.registry_package.is_none() || source.registry_version.is_none() ||
            source.release_digest.is_none() || source.source_digest.is_none() ||
            source.manifest_digest.is_none() || source.blob_digest.is_none() ||
            source.blob_size.is_none() || source.archive_format.is_none()) {
            return install_source_failure<InstallSourceProvenance>(
                "resolved Registry package source is missing exact provenance"_str);
        }
        auto provenance = InstallSourceProvenance::Registry(source.registry_package->clone(),
                                                            source.registry_version->clone(),
                                                            source.release_digest->clone(),
                                                            source.source_digest->clone(),
                                                            source.manifest_digest->clone(),
                                                            source.blob_digest->clone(),
                                                            source.blob_size->clone(),
                                                            source.archive_format->clone(),
                                                            source.identity.clone());
        rstd_try(install_source_identity(provenance));
        return Ok(rstd::move(provenance));
    }
    if (source.kind != lito::source::PackageSourceKind::Git) {
        return install_source_failure<InstallSourceProvenance>(
            "resolved package source kind cannot be installed"_str);
    }
    if (! lito::source::git_commit_is_valid(source.commit.as_str())) {
        return install_source_failure<InstallSourceProvenance>(
            "resolved Git package source commit is invalid"_str);
    }
    return Ok(InstallSourceProvenance::Git(source.git.clone(),
                                           source.reference.clone(),
                                           source.commit.clone(),
                                           source.identity.clone()));
}

auto serialize_install_source_provenance(const InstallSourceProvenance& provenance)
    -> InstallSourceResult<Json> {
    auto identity = rstd_try(install_source_identity(provenance));
    auto source   = JsonMap::make();
    source.insert(String::make("identity"_str), Json::String(rstd::move(identity)));
    if (provenance.is_Registry()) {
        const auto& registry = provenance.as_Registry();
        source.insert(String::make("archive-format"_str),
                      Json::String(String::make(registry.archive_format.as_str())));
        source.insert(String::make("blob"_str), Json::String(registry.blob.text()));
        source.insert(String::make("blob-size"_str), Json::String(registry.blob_size.text()));
        source.insert(String::make("kind"_str), Json::String(String::make("registry"_str)));
        source.insert(String::make("manifest"_str), Json::String(registry.manifest.text()));
        source.insert(String::make("package"_str),
                      Json::String(String::make(registry.package.name.as_str())));
        source.insert(String::make("registry"_str),
                      Json::String(String::make(registry.package.registry.as_str())));
        source.insert(String::make("release"_str), Json::String(registry.release.text()));
        source.insert(String::make("source"_str), Json::String(registry.source.text()));
        source.insert(String::make("version"_str), Json::String(registry.version.text()));
        return Ok(Json::Object(rstd::move(source)));
    }
    if (provenance.is_Git()) {
        const auto& git = provenance.as_Git();
        source.insert(String::make("commit"_str), Json::String(git.commit.clone()));
        source.insert(String::make("kind"_str), Json::String(String::make("git"_str)));
        source.insert(String::make("reference"_str), Json::String(git.reference.value.clone()));
        source.insert(
            String::make("reference-kind"_str),
            Json::String(String::make(lito::source::git_reference_kind_name(git.reference.kind))));
        source.insert(String::make("url"_str), Json::String(git.url.clone()));
        return Ok(Json::Object(rstd::move(source)));
    }
    const auto& root = provenance.as_Local().root;
    auto        path = root.as_path().to_str();
    if (path.is_none()) {
        return install_source_failure<Json>(
            rstd::format("install source path '{}' is not valid UTF-8", root.as_path()));
    }
    source.insert(String::make("kind"_str), Json::String(String::make("path"_str)));
    source.insert(String::make("path"_str), Json::String(String::make(*path)));
    return Ok(Json::Object(rstd::move(source)));
}

auto parse_install_source_provenance(const Json& source)
    -> InstallSourceResult<InstallSourceProvenance> {
    auto object = source.as_object();
    if (object.is_none()) {
        return install_source_failure<InstallSourceProvenance>(
            "installed package source must be an object"_str);
    }
    auto kind     = rstd_try(required_source_string(source, "kind"_str));
    auto identity = rstd_try(required_source_string(source, "identity"_str));
    if (kind == "registry"_str) {
        rstd_try(reject_source_fields(source, registry_install_source_key));
        auto registry_text  = rstd_try(required_source_string(source, "registry"_str));
        auto package_text   = rstd_try(required_source_string(source, "package"_str));
        auto version_text   = rstd_try(required_source_string(source, "version"_str));
        auto release_text   = rstd_try(required_source_string(source, "release"_str));
        auto source_text    = rstd_try(required_source_string(source, "source"_str));
        auto manifest_text  = rstd_try(required_source_string(source, "manifest"_str));
        auto blob_text      = rstd_try(required_source_string(source, "blob"_str));
        auto size_text      = rstd_try(required_source_string(source, "blob-size"_str));
        auto format_text    = rstd_try(required_source_string(source, "archive-format"_str));
        auto registry       = lito::registry::RegistryId::parse(registry_text.as_str());
        auto package        = lito::registry::RegistryPackageName::parse(package_text.as_str());
        auto version        = lito::registry::SemanticVersion::parse(version_text.as_str());
        auto release        = lito::registry::ReleaseDigest::parse(release_text.as_str());
        auto source_digest  = lito::registry::SourceDigest::parse(source_text.as_str());
        auto manifest       = lito::registry::ManifestDigest::parse(manifest_text.as_str());
        auto blob           = lito::registry::BlobDigest::parse(blob_text.as_str());
        auto blob_size      = lito::registry::RegistryBlobSize::parse(size_text.as_str());
        auto archive_format = lito::registry::RegistryArchiveFormat::parse(format_text.as_str());
        if (registry.is_err() || package.is_err() || version.is_err() || release.is_err() ||
            source_digest.is_err() || manifest.is_err() || blob.is_err() || blob_size.is_err() ||
            archive_format.is_err()) {
            return install_source_failure<InstallSourceProvenance>(
                "installed Registry source contains invalid exact provenance"_str);
        }
        auto provenance = InstallSourceProvenance::Registry(
            lito::registry::RegistryPackageId {
                .registry = rstd::move(registry).unwrap(),
                .name     = rstd::move(package).unwrap(),
            },
            rstd::move(version).unwrap(),
            rstd::move(release).unwrap(),
            rstd::move(source_digest).unwrap(),
            rstd::move(manifest).unwrap(),
            rstd::move(blob).unwrap(),
            rstd::move(blob_size).unwrap(),
            rstd::move(archive_format).unwrap(),
            rstd::move(identity));
        rstd_try(install_source_identity(provenance));
        return Ok(rstd::move(provenance));
    }
    if (kind == "git"_str) {
        rstd_try(reject_source_fields(source, git_install_source_key));
        auto reference_kind = rstd_try(required_source_string(source, "reference-kind"_str));
        auto parsed_kind    = lito::source::GitReferenceKind::DefaultBranch;
        if (reference_kind == "branch"_str)
            parsed_kind = lito::source::GitReferenceKind::Branch;
        else if (reference_kind == "tag"_str)
            parsed_kind = lito::source::GitReferenceKind::Tag;
        else if (reference_kind == "rev"_str)
            parsed_kind = lito::source::GitReferenceKind::Rev;
        else if (reference_kind == "commit"_str)
            parsed_kind = lito::source::GitReferenceKind::Commit;
        else if (reference_kind != "default"_str) {
            return install_source_failure<InstallSourceProvenance>(
                "installed Git source reference kind is invalid"_str);
        }
        auto provenance = InstallSourceProvenance::Git(
            rstd_try(required_source_string(source, "url"_str)),
            lito::source::GitReference {
                .kind  = parsed_kind,
                .value = rstd_try(required_source_text(source, "reference"_str)),
            },
            rstd_try(required_source_string(source, "commit"_str)),
            rstd::move(identity));
        rstd_try(install_source_identity(provenance));
        return Ok(rstd::move(provenance));
    }
    if (kind != "path"_str) {
        return install_source_failure<InstallSourceProvenance>(
            rstd::format("installed package source kind '{}' is unsupported", kind.as_str()));
    }
    rstd_try(reject_source_fields(source, path_install_source_key));
    auto path = rstd_try(required_source_string(source, "path"_str));
    auto provenance =
        InstallSourceProvenance::Local(PathBuf::from(path.as_str()), rstd::move(identity));
    auto expected = rstd_try(install_source_identity(provenance));
    static_cast<void>(expected);
    return Ok(rstd::move(provenance));
}

auto resolve_install_root(ref<rstd::path::Path>              invocation_root,
                          Option<PathBuf>                    command_root,
                          const lito::config::InstallConfig& config)
    -> InstallSourceResult<InstallRoot> {
    if (command_root.is_some()) {
        if (command_root->is_empty()) {
            return install_source_failure<InstallRoot>("install root must not be empty"_str);
        }
        auto root = absolute_root(invocation_root, rstd::move(command_root).unwrap());
        return Ok(InstallRoot { .path = rstd::move(root) });
    }
    auto environment = environment_root("LITO_INSTALL_ROOT"_str);
    if (environment.is_err()) return Err(rstd::move(environment).unwrap_err());
    if (environment->is_some()) {
        return Ok(InstallRoot { .path = rstd::move(environment).unwrap().unwrap() });
    }
    if (config.root.is_some()) return Ok(InstallRoot { .path = config.root->clone() });
    auto lito_home = environment_root("LITO_HOME"_str);
    if (lito_home.is_err()) return Err(rstd::move(lito_home).unwrap_err());
    if (lito_home->is_some()) {
        return Ok(InstallRoot { .path = rstd::move(lito_home).unwrap().unwrap() });
    }
    auto home = environment_root("HOME"_str);
    if (home.is_err()) return Err(rstd::move(home).unwrap_err());
    if (home->is_none()) {
        return install_source_failure<InstallRoot>(
            "install root requires --root, LITO_INSTALL_ROOT, LITO_HOME, or HOME"_str);
    }
    auto root = rstd::move(home).unwrap().unwrap();
    root.push(PathBuf::from(".lito"_str).as_path());
    return Ok(InstallRoot { .path = rstd::move(root) });
}

auto resolve_install_destination(ref<rstd::path::Path>              invocation_root,
                                 InstallDestinationRequirement      requirement,
                                 const lito::config::InstallConfig& config)
    -> InstallSourceResult<InstallDestination> {
    if (requirement.is_Prefix()) {
        auto path = rstd::move(requirement).as_Prefix().path;
        if (path.is_empty()) {
            return install_source_failure<InstallDestination>(
                "install prefix must not be empty"_str);
        }
        return Ok(InstallDestination::Prefix(
            InstallPrefix { .path = absolute_root(invocation_root, rstd::move(path)) }));
    }
    auto root = rstd_try(resolve_install_root(
        invocation_root, rstd::move(requirement).as_Managed().command_root, config));
    return Ok(InstallDestination::Managed(rstd::move(root)));
}

} // namespace lito
