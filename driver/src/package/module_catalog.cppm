module;
#include <rstd/macro.hpp>

export module lito.driver:package.module_catalog;

import rstd;
import lito.crypto;
import luato;
import lito.core;

using namespace rstd::prelude;
using namespace rstd::literals;
using PathBuf = rstd::path::PathBuf;

export namespace lito::package
{

class ScriptModuleCatalog {
public:
    static auto make(ref<rstd::path::Path>            root,
                     ref<str>                         owner_identity,
                     slice<String>                    dependencies,
                     slice<ResolvedScriptPackageView> packages,
                     lito::manifest::ScriptHostKind   host) -> PackageResult<ScriptModuleCatalog>;

    auto entry(ref<rstd::path::Path> script, ref<str> logical_name)
        -> luato::Result<luato::LuaModuleSource>;
    auto resolve(luato::ModuleRequest request) -> luato::Result<luato::LuaModuleSource>;

private:
    struct Owner {
        Option<String>                      package;
        String                              identity;
        PathBuf                             root;
        const lito::source::SourceTree*     source {};
        Vec<String>                         dependencies;
        String                              require_name;
        Vec<lito::manifest::ScriptHostKind> supports;
    };

    struct LoadedSource {
        String identity;
        usize  owner {};
    };

    Vec<Owner>                     owners_;
    Vec<LoadedSource>              loaded_sources_;
    lito::manifest::ScriptHostKind host_ { lito::manifest::ScriptHostKind::Build };

    auto load(usize owner, ref<str> path, ref<str> logical_name)
        -> luato::Result<luato::LuaModuleSource>;
};

} // namespace lito::package

using namespace lito::package;

auto catalog_failure(ref<str> source, String message) -> luato::Result<luato::LuaModuleSource> {
    return Err(
        luato::Error::make(luato::ErrorKind::Module, String::make(source), rstd::move(message)));
}

auto source_tree_file(const lito::source::SourceTree& tree, ref<str> path) -> Option<slice<u8>> {
    for (const auto& entry : tree.entries()) {
        if (entry.path().as_str() == path && entry.kind() == lito::source::SourceEntryKind::File) {
            return Some(entry.contents());
        }
    }
    return None();
}

auto module_identity(ref<str> owner, ref<str> path, slice<u8> bytes) -> String {
    return rstd::format("lua:{}:{}:{}", owner, path, lito::crypto::sha256_hex(bytes).as_str());
}

auto ScriptModuleCatalog::make(ref<rstd::path::Path>            root,
                               ref<str>                         owner_identity,
                               slice<String>                    dependencies,
                               slice<ResolvedScriptPackageView> packages,
                               lito::manifest::ScriptHostKind   host)
    -> PackageResult<ScriptModuleCatalog> {
    auto catalog           = ScriptModuleCatalog {};
    catalog.host_          = host;
    auto root_dependencies = Vec<String>::with_capacity(dependencies.len());
    for (const auto& dependency : dependencies) root_dependencies.push(dependency.clone());
    catalog.owners_.push(Owner {
        .identity     = String::make(owner_identity),
        .root         = PathBuf::from(root),
        .dependencies = rstd::move(root_dependencies),
    });
    for (const auto& package : packages) {
        auto owned_dependencies = package.dependencies.clone();
        catalog.owners_.push(Owner {
            .package  = Some(package.name.clone()),
            .identity = package.source_identity.clone(),
            .root     = package.root.clone(),
            .source = package.embedded_source.is_some() ? rstd::addressof(*package.embedded_source)
                                                        : nullptr,
            .dependencies = rstd::move(owned_dependencies),
            .require_name = package.require_name.clone(),
            .supports     = package.supports.clone(),
        });
    }
    return Ok(rstd::move(catalog));
}

auto ScriptModuleCatalog::load(usize owner, ref<str> path, ref<str> logical_name)
    -> luato::Result<luato::LuaModuleSource> {
    if (owner >= owners_.len()) {
        return catalog_failure(logical_name, String::make("invalid Lua source owner"_str));
    }
    const auto& source_owner = owners_[owner];
    auto        bytes        = Vec<u8>::make();
    auto        display      = String::make(path);
    if (source_owner.source == nullptr) {
        auto requested = source_owner.root.join(PathBuf::from(path).as_path());
        auto metadata  = rstd::fs::symlink_metadata(requested.as_path());
        if (metadata.is_err()) {
            return catalog_failure(requested.as_path().to_string_lossy().as_str(),
                                   rstd::format("cannot inspect Lua module '{}': {}",
                                                requested.as_path(),
                                                rstd::move(metadata).unwrap_err()));
        }
        if (! metadata->is_file() || metadata->is_symlink()) {
            return catalog_failure(
                requested.as_path().to_string_lossy().as_str(),
                rstd::format("Lua module '{}' must be a regular non-symlink file",
                             requested.as_path()));
        }
        auto canonical_root = rstd::fs::canonicalize(source_owner.root.as_path());
        if (canonical_root.is_err()) {
            return catalog_failure(source_owner.root.as_path().to_string_lossy().as_str(),
                                   rstd::format("cannot resolve Lua package root '{}': {}",
                                                source_owner.root.as_path(),
                                                rstd::move(canonical_root).unwrap_err()));
        }
        auto canonical = rstd::fs::canonicalize(requested.as_path());
        if (canonical.is_err()) {
            return catalog_failure(requested.as_path().to_string_lossy().as_str(),
                                   rstd::format("cannot resolve Lua module '{}': {}",
                                                requested.as_path(),
                                                rstd::move(canonical).unwrap_err()));
        }
        if (canonical->as_path().strip_prefix(canonical_root->as_path()).is_none()) {
            return catalog_failure(
                requested.as_path().to_string_lossy().as_str(),
                rstd::format("Lua module '{}' escapes its package root", requested.as_path()));
        }
        auto read = rstd::fs::read(canonical->as_path());
        if (read.is_err()) {
            return catalog_failure(canonical->as_path().to_string_lossy().as_str(),
                                   rstd::format("cannot read Lua module '{}': {}",
                                                canonical->as_path(),
                                                rstd::move(read).unwrap_err()));
        }
        bytes   = rstd::move(read).unwrap();
        display = canonical->as_path().to_string_lossy();
    } else {
        auto found = source_tree_file(*source_owner.source, path);
        if (found.is_none()) {
            return catalog_failure(
                source_owner.identity.as_str(),
                rstd::format("Lua module '{}' is absent from script package source '{}'",
                             path,
                             source_owner.identity.as_str()));
        }
        bytes   = Vec<u8>::from(*found);
        display = rstd::format("{}/{}", source_owner.identity.as_str(), path);
    }
    auto identity = module_identity(source_owner.identity.as_str(), path, bytes.as_slice());
    auto known    = false;
    for (const auto& loaded : loaded_sources_) {
        if (loaded.identity == identity) {
            known = true;
            break;
        }
    }
    if (! known) {
        loaded_sources_.push(LoadedSource {
            .identity = identity.clone(),
            .owner    = owner,
        });
    }
    return Ok(luato::LuaModuleSource {
        .logical_name = String::make(logical_name),
        .identity     = rstd::move(identity),
        .display_path = rstd::move(display),
        .bytes        = rstd::move(bytes),
    });
}

auto ScriptModuleCatalog::entry(ref<rstd::path::Path> script, ref<str> logical_name)
    -> luato::Result<luato::LuaModuleSource> {
    auto relative = script.strip_prefix(owners_[usize {}].root.as_path());
    if (relative.is_none()) {
        return catalog_failure(
            script.to_string_lossy().as_str(),
            rstd::format("script entry '{}' is outside its package root", script));
    }
    auto text = relative->to_str();
    if (text.is_none() || text->is_empty()) {
        return catalog_failure(
            script.to_string_lossy().as_str(),
            rstd::format("script entry '{}' has an invalid package path", script));
    }
    auto parsed = lito::source::SourcePath::parse(*text);
    if (parsed.is_err()) {
        return catalog_failure(
            script.to_string_lossy().as_str(),
            rstd::format("script entry '{}' has an invalid package path", script));
    }
    return load(usize {}, *text, logical_name);
}

auto ScriptModuleCatalog::resolve(luato::ModuleRequest request)
    -> luato::Result<luato::LuaModuleSource> {
    const LoadedSource* importer = nullptr;
    for (const auto& loaded : loaded_sources_) {
        if (loaded.identity == request.importer_identity) {
            importer = rstd::addressof(loaded);
            break;
        }
    }
    if (importer == nullptr) {
        return catalog_failure(request.importer_path.as_str(),
                               rstd::format("Lua importer '{}' has no registered source owner",
                                            request.importer_identity.as_str()));
    }
    const auto& owner = owners_[importer->owner];
    if (request.requested.as_str().starts_with("@"_str)) {
        for (const auto& dependency : owner.dependencies) {
            for (usize candidate = usize(1); candidate < owners_.len(); ++candidate) {
                const auto& provider = owners_[candidate];
                if (provider.package.is_none() ||
                    provider.package->as_str() != dependency.as_str() ||
                    provider.require_name != request.requested.as_str()) {
                    continue;
                }
                auto supported = false;
                for (auto host : provider.supports) {
                    if (host == host_) supported = true;
                }
                if (! supported) {
                    return catalog_failure(
                        request.importer_path.as_str(),
                        rstd::format("script package '{}' does not support the '{}' Lua host",
                                     provider.package->as_str(),
                                     lito::manifest::script_host_kind_name(host_)));
                }
                return load(candidate, "lib.lua"_str, request.requested.as_str());
            }
        }
        return catalog_failure(
            request.importer_path.as_str(),
            rstd::format("Lua module '{}' is not a direct script dependency of this source owner",
                         request.requested.as_str()));
    }
    auto parsed = lito::source::SourcePath::parse(request.requested.as_str());
    if (parsed.is_err()) {
        return catalog_failure(
            request.importer_path.as_str(),
            rstd::format("Lua module '{}' must be an exact safe source-root file path",
                         request.requested.as_str()));
    }
    return load(importer->owner, request.requested.as_str(), request.requested.as_str());
}
