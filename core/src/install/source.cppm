module;
#include <rstd/macro.hpp>

export module lito.install.source;

import rstd;
import rstd.json;
import lito.error;
import lito.install.contract;
import lito.install.package_contract;
import lito.config.contract;
import lito.source;
import lito.workspace;

using namespace rstd::prelude;
using namespace rstd::literals;
using Json    = rstd::json::Value;
using JsonMap = rstd::json::Map;

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

auto reject_source_fields(const Json& source,
                          bool (*allowed)(ref<str>)) -> InstallSourceResult<empty> {
    auto object = source.as_object();
    auto keys = (**object).keys();
    for (auto key = keys.next(); key.is_some(); key = keys.next()) {
        if (! allowed((**key).as_str())) {
            return install_source_failure<empty>(rstd::format(
                "installed package source contains unknown field '{}'", (**key).as_str()));
        }
    }
    return Ok(empty {});
}

} // namespace lito

export namespace lito
{

auto resolve_install_source(InstallSourceRequirement requirement)
    -> InstallSourceResult<ResolvedInstallSource>;

auto install_source_identity(const InstallSourceProvenance& provenance)
    -> InstallSourceResult<String>;

auto install_source_provenance(const ResolvedPackageSource& source)
    -> InstallSourceResult<InstallSourceProvenance>;

auto serialize_install_source_provenance(const InstallSourceProvenance& provenance)
    -> InstallSourceResult<Json>;

auto parse_install_source_provenance(const Json& source)
    -> InstallSourceResult<InstallSourceProvenance>;

auto resolve_install_root(ref<rstd::path::Path> invocation_root,
                          Option<PathBuf>       command_root,
                          const InstallConfig&  config) -> InstallSourceResult<InstallRoot>;

auto resolve_install_source(InstallSourceRequirement requirement)
    -> InstallSourceResult<ResolvedInstallSource> {
    if (! requirement.is_LocalProject()) {
        return Err(InstallSourceError::Message(
            String::make("unsupported install source requirement"_str)));
    }
    auto project =
        rstd_try(resolve_project_entry(requirement.as_LocalProject().requested_root.as_path()));
    auto root = project.root.clone();
    return Ok(ResolvedInstallSource {
        .project    = rstd::move(project),
        .provenance = InstallSourceProvenance::Local(
            root.clone(), path_source_identity(root.as_path())),
        .identity   = path_source_identity(root.as_path()),
        .storage    = InstallSourceStorage::BorrowedLocal,
    });
}

auto install_source_identity(const InstallSourceProvenance& provenance)
    -> InstallSourceResult<String> {
    if (provenance.is_Git()) {
        const auto& source = provenance.as_Git();
        if (! git_commit_is_valid(source.commit.as_str()) || source.url.is_empty()) {
            return install_source_failure<String>("installed Git source is invalid"_str);
        }
        auto expected = git_source_identity(source.url.as_str(), source.commit.as_str());
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

auto install_source_provenance(const ResolvedPackageSource& source)
    -> InstallSourceResult<InstallSourceProvenance> {
    if (source.identity.is_empty()) {
        return install_source_failure<InstallSourceProvenance>(
            "resolved package source identity is empty"_str);
    }
    if (source.kind == PackageSourceKind::Path) {
        if (! source.root_directory.as_path().is_absolute()) {
            return install_source_failure<InstallSourceProvenance>(
                "resolved package source root must be absolute"_str);
        }
        return Ok(InstallSourceProvenance::Local(
            source.root_directory.clone(), source.identity.clone()));
    }
    if (! git_commit_is_valid(source.commit.as_str())) {
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
    auto        identity = rstd_try(install_source_identity(provenance));
    auto source = JsonMap::make();
    source.insert(String::make("identity"_str), Json::String(rstd::move(identity)));
    if (provenance.is_Git()) {
        const auto& git = provenance.as_Git();
        source.insert(String::make("commit"_str), Json::String(git.commit.clone()));
        source.insert(String::make("kind"_str), Json::String(String::make("git"_str)));
        source.insert(String::make("reference"_str), Json::String(git.reference.value.clone()));
        source.insert(String::make("reference-kind"_str),
                      Json::String(String::make(git_reference_kind_name(git.reference.kind))));
        source.insert(String::make("url"_str), Json::String(git.url.clone()));
        return Ok(Json::Object(rstd::move(source)));
    }
    const auto& root = provenance.as_Local().root;
    auto path = root.as_path().to_str();
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
    auto kind = rstd_try(required_source_string(source, "kind"_str));
    auto identity = rstd_try(required_source_string(source, "identity"_str));
    if (kind == "git"_str) {
        rstd_try(reject_source_fields(source, git_install_source_key));
        auto reference_kind = rstd_try(required_source_string(source, "reference-kind"_str));
        auto parsed_kind = GitReferenceKind::DefaultBranch;
        if (reference_kind == "branch"_str) parsed_kind = GitReferenceKind::Branch;
        else if (reference_kind == "tag"_str) parsed_kind = GitReferenceKind::Tag;
        else if (reference_kind == "rev"_str) parsed_kind = GitReferenceKind::Rev;
        else if (reference_kind == "commit"_str) parsed_kind = GitReferenceKind::Commit;
        else if (reference_kind != "default"_str) {
            return install_source_failure<InstallSourceProvenance>(
                "installed Git source reference kind is invalid"_str);
        }
        auto provenance = InstallSourceProvenance::Git(
            rstd_try(required_source_string(source, "url"_str)),
            GitReference {
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
    auto provenance = InstallSourceProvenance::Local(
        PathBuf::from(path.as_str()), rstd::move(identity));
    auto expected   = rstd_try(install_source_identity(provenance));
    static_cast<void>(expected);
    return Ok(rstd::move(provenance));
}

auto resolve_install_root(ref<rstd::path::Path> invocation_root,
                          Option<PathBuf>       command_root,
                          const InstallConfig&  config) -> InstallSourceResult<InstallRoot> {
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

} // namespace lito
