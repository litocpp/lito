module;
#include <rstd/macro.hpp>

export module lito.install.source;

import rstd;
import rstd.json;
import lito.error;
import lito.install.contract;
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
auto install_source_failure(String message) -> Result<T> {
    return Err(Error::make(ErrorKind::InvalidRequest, rstd::move(message)));
}

template<typename T>
auto install_source_failure(ref<str> message) -> Result<T> {
    return Err(Error::make(ErrorKind::InvalidRequest, message));
}

auto absolute_root(ref<rstd::path::Path> base, PathBuf root) -> PathBuf {
    if (root.as_path().is_absolute()) return root;
    return PathBuf::from(base).join(root.as_path());
}

auto environment_root(ref<str> variable) -> Result<Option<PathBuf>> {
    auto value = rstd::env::var(variable);
    if (value.is_none() || value->is_empty()) return Ok(Option<PathBuf> {});
    auto path = PathBuf::from(rstd::move(value).unwrap());
    if (! path.as_path().is_absolute()) {
        return install_source_failure<Option<PathBuf>>(
            rstd::format("{} must be an absolute path", variable));
    }
    return Ok(Some(rstd::move(path)));
}

auto install_source_key(ref<str> key) -> bool {
    return key == "identity"_str || key == "kind"_str || key == "path"_str;
}

auto required_source_string(const Json& source, ref<str> key) -> Result<String> {
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

} // namespace lito

export namespace lito
{

auto resolve_install_source(InstallSourceRequirement requirement) -> Result<ResolvedInstallSource>;

auto install_source_identity(const InstallSourceProvenance& provenance) -> Result<String>;

auto serialize_install_source_provenance(const InstallSourceProvenance& provenance) -> Result<Json>;

auto parse_install_source_provenance(const Json& source) -> Result<InstallSourceProvenance>;

auto resolve_install_root(ref<rstd::path::Path> invocation_root,
                          Option<PathBuf>       command_root,
                          const InstallConfig&  config) -> Result<InstallRoot>;

auto resolve_install_source(InstallSourceRequirement requirement) -> Result<ResolvedInstallSource> {
    if (! requirement.is_LocalProject()) {
        return install_source_failure<ResolvedInstallSource>(
            "unsupported install source requirement"_str);
    }
    auto project =
        rstd_try(resolve_project_entry(requirement.as_LocalProject().requested_root.as_path()));
    auto root = project.root.clone();
    return Ok(ResolvedInstallSource {
        .project    = rstd::move(project),
        .provenance = InstallSourceProvenance::Local(root.clone()),
        .identity   = path_source_identity(root.as_path()),
        .storage    = InstallSourceStorage::BorrowedLocal,
    });
}

auto install_source_identity(const InstallSourceProvenance& provenance) -> Result<String> {
    if (! provenance.is_Local()) {
        return install_source_failure<String>("unsupported install source provenance"_str);
    }
    const auto& root = provenance.as_Local().root;
    if (! root.as_path().is_absolute()) {
        return install_source_failure<String>("installed local source path must be absolute"_str);
    }
    return Ok(path_source_identity(root.as_path()));
}

auto serialize_install_source_provenance(const InstallSourceProvenance& provenance)
    -> Result<Json> {
    auto        identity = rstd_try(install_source_identity(provenance));
    const auto& root     = provenance.as_Local().root;
    auto        path     = root.as_path().to_str();
    if (path.is_none()) {
        return install_source_failure<Json>(
            rstd::format("install source path '{}' is not valid UTF-8", root.as_path()));
    }
    auto source = JsonMap::make();
    source.insert(String::make("identity"_str), Json::String(rstd::move(identity)));
    source.insert(String::make("kind"_str), Json::String(String::make("local"_str)));
    source.insert(String::make("path"_str), Json::String(String::make(*path)));
    return Ok(Json::Object(rstd::move(source)));
}

auto parse_install_source_provenance(const Json& source) -> Result<InstallSourceProvenance> {
    auto object = source.as_object();
    if (object.is_none()) {
        return install_source_failure<InstallSourceProvenance>(
            "installed package source must be an object"_str);
    }
    auto keys = (**object).keys();
    for (auto key = keys.next(); key.is_some(); key = keys.next()) {
        if (! install_source_key((**key).as_str())) {
            return install_source_failure<InstallSourceProvenance>(rstd::format(
                "installed package source contains unknown field '{}'", (**key).as_str()));
        }
    }
    auto kind = rstd_try(required_source_string(source, "kind"_str));
    if (kind != "local"_str) {
        return install_source_failure<InstallSourceProvenance>(
            rstd::format("installed package source kind '{}' is unsupported", kind.as_str()));
    }
    auto identity   = rstd_try(required_source_string(source, "identity"_str));
    auto path       = rstd_try(required_source_string(source, "path"_str));
    auto provenance = InstallSourceProvenance::Local(PathBuf::from(path.as_str()));
    auto expected   = rstd_try(install_source_identity(provenance));
    if (identity != expected.as_str()) {
        return install_source_failure<InstallSourceProvenance>(
            "installed local source identity does not match its path"_str);
    }
    return Ok(rstd::move(provenance));
}

auto resolve_install_root(ref<rstd::path::Path> invocation_root,
                          Option<PathBuf>       command_root,
                          const InstallConfig&  config) -> Result<InstallRoot> {
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
