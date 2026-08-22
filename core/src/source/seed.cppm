module;
#include <rstd/macro.hpp>

export module lito.core:source.seed;

import rstd;
import rstd.json;
import :source.fetch;
import :source.error;
import lito.system;

using namespace rstd::prelude;
using PathBuf = rstd::path::PathBuf;
using namespace lito::system;
using namespace rstd::literals;
using Json      = rstd::json::Value;
using StringSet = rstd::collections::BTreeMap<String, empty>;

export namespace lito::source
{

enum class FetchSeedKind
{
    Git,
    Archive,
    RegistryBlob,
};

struct FetchSeedEntry {
    String        identity;
    FetchSeedKind kind { FetchSeedKind::Git };
    PathBuf       path;
    Vec<String>   architectures;
};

struct FetchSeedCatalog {
    PathBuf             root;
    Vec<FetchSeedEntry> sources;

    auto lookup(const FetchIdentity& identity) const -> Option<PathBuf> {
        auto key = fetch_identity_text(identity);
        for (const auto& source : sources) {
            if (source.identity.as_str() == key.as_str()) {
                return Some(root.join(source.path.as_path()));
            }
        }
        return None();
    }
};

} // namespace lito::source

using namespace lito::source;

auto seed_root_key(ref<str> key) -> bool {
    return key == "sources"_str || key == "version"_str;
}

auto seed_entry_key(ref<str> key) -> bool {
    return key == "architectures"_str || key == "identity"_str || key == "kind"_str ||
           key == "path"_str;
}

auto reject_seed_unknown(const Json& value, ref<str> context, bool (*allowed)(ref<str>))
    -> SourceResult<empty> {
    auto object = value.as_object();
    if (object.is_none()) {
        return source_failure<empty>(rstd::format("{} must be an object", context));
    }
    auto keys = (**object).keys();
    for (auto key : keys) {
        if (! allowed((*key).as_str())) {
            return source_failure<empty>(
                rstd::format("{} contains unknown field '{}'", context, (*key).as_str()));
        }
    }
    return Ok(empty {});
}

auto required_seed_member(const Json& value, ref<str> key, ref<str> context)
    -> SourceResult<ref<Json>> {
    auto member = value.get(key);
    if (member.is_none()) {
        return source_failure<ref<Json>>(rstd::format("{} is missing '{}'", context, key));
    }
    return Ok(*member);
}

auto required_seed_string(const Json& value, ref<str> key, ref<str> context)
    -> SourceResult<ref<str>> {
    auto member = rstd_try(required_seed_member(value, key, context));
    auto text   = member->as_str();
    if (text.is_none()) {
        return source_failure<ref<str>>(rstd::format("{}.{} must be a string", context, key));
    }
    return Ok(*text);
}

auto identity_kind(ref<str> identity) -> Option<FetchSeedKind> {
    if (identity.starts_with("lito-fetch-v1\ngit\n"_str)) return Some(FetchSeedKind::Git);
    if (identity.starts_with("lito-fetch-v1\narchive\n"_str)) return Some(FetchSeedKind::Archive);
    if (identity.starts_with("lito-fetch-v1\nregistry-blob\n"_str)) {
        return Some(FetchSeedKind::RegistryBlob);
    }
    return None();
}

export namespace lito::source
{

auto load_fetch_seed_catalog(ref<rstd::path::Path> root) -> SourceResult<FetchSeedCatalog> {
    auto catalog_path = PathBuf::from(root).join(PathBuf::from("catalog.json"_str).as_path());
    auto contents     = rstd::fs::read_to_string(catalog_path.as_path());
    if (contents.is_err()) {
        return source_io_failure<FetchSeedCatalog>("read fetch seed catalog"_str,
                                                   catalog_path.as_path(),
                                                   rstd::move(contents).unwrap_err());
    }
    auto parsed = rstd::json::from_str(contents->as_str());
    if (parsed.is_err()) {
        return source_failure<FetchSeedCatalog>(
            rstd::format("fetch seed catalog '{}' is invalid JSON: {}",
                         catalog_path.as_path(),
                         parsed.unwrap_err()));
    }
    auto document = rstd::move(parsed).unwrap();
    rstd_try(reject_seed_unknown(document, "fetch seed catalog"_str, seed_root_key));
    auto version_value =
        rstd_try(required_seed_member(document, "version"_str, "fetch seed catalog"_str));
    auto version = version_value->as_u64();
    if (version.is_none() || *version != u64(1)) {
        return source_failure<FetchSeedCatalog>("fetch seed catalog version must be integer 1"_str);
    }
    auto sources_value =
        rstd_try(required_seed_member(document, "sources"_str, "fetch seed catalog"_str));
    auto values = sources_value->as_array();
    if (values.is_none()) {
        return source_failure<FetchSeedCatalog>("fetch seed catalog sources must be an array"_str);
    }

    auto sources = Vec<FetchSeedEntry>::with_capacity((**values).len());
    auto seen    = StringSet::make();
    for (const auto& value : **values) {
        rstd_try(reject_seed_unknown(value, "fetch seed source"_str, seed_entry_key));
        auto identity =
            rstd_try(required_seed_string(value, "identity"_str, "fetch seed source"_str));
        auto kind = rstd_try(required_seed_string(value, "kind"_str, "fetch seed source"_str));
        auto path = rstd_try(required_seed_string(value, "path"_str, "fetch seed source"_str));
        auto parsed_kind = identity_kind(identity);
        if (parsed_kind.is_none()) {
            return source_failure<FetchSeedCatalog>(
                "fetch seed source identity has an unsupported version or kind"_str);
        }
        const auto expected = kind == "git"_str             ? Some(FetchSeedKind::Git)
                              : kind == "archive"_str       ? Some(FetchSeedKind::Archive)
                              : kind == "registry-blob"_str ? Some(FetchSeedKind::RegistryBlob)
                                                            : Option<FetchSeedKind> {};
        if (expected.is_none() || *expected != *parsed_kind) {
            return source_failure<FetchSeedCatalog>(
                "fetch seed source kind does not match its identity"_str);
        }
        if (seen.contains_key(identity)) {
            return source_failure<FetchSeedCatalog>(
                "fetch seed catalog contains a duplicate identity"_str);
        }
        auto relative = PathBuf::from(path);
        if (relative.is_empty() || ! relative.as_path().is_safe_relative()) {
            return source_failure<FetchSeedCatalog>(
                "fetch seed source path must be a non-empty safe relative path"_str);
        }
        auto architectures      = Vec<String>::make();
        auto architecture_value = value.get("architectures"_str);
        if (architecture_value.is_some()) {
            auto architecture_values = (**architecture_value).as_array();
            if (architecture_values.is_none() || (**architecture_values).is_empty()) {
                return source_failure<FetchSeedCatalog>(
                    "fetch seed source architectures must be a non-empty array"_str);
            }
            auto architecture_seen = StringSet::make();
            auto previous          = Option<String> {};
            for (const auto& architecture : **architecture_values) {
                auto name = architecture.as_str();
                if (name.is_none() || name->is_empty()) {
                    return source_failure<FetchSeedCatalog>(
                        "fetch seed source architecture must be a non-empty string"_str);
                }
                auto canonical = canonical_architecture(*name);
                if (canonical.is_err() || canonical->as_str() != *name) {
                    return source_failure<FetchSeedCatalog>(
                        "fetch seed source architecture must use a canonical Lito name"_str);
                }
                if (architecture_seen.contains_key(*name)) {
                    return source_failure<FetchSeedCatalog>(
                        "fetch seed source contains a duplicate architecture"_str);
                }
                auto canonical_name = String::make(*name);
                if (previous.is_some() && canonical_name < *previous) {
                    return source_failure<FetchSeedCatalog>(
                        "fetch seed source architectures must use stable sorted order"_str);
                }
                architecture_seen.insert(canonical_name.clone(), empty {});
                previous = Some(canonical_name.clone());
                architectures.push(rstd::move(canonical_name));
            }
        }
        seen.insert(String::make(identity), empty {});
        sources.push(FetchSeedEntry {
            .identity      = String::make(identity),
            .kind          = *parsed_kind,
            .path          = rstd::move(relative),
            .architectures = rstd::move(architectures),
        });
    }
    return Ok(FetchSeedCatalog {
        .root    = PathBuf::from(root),
        .sources = rstd::move(sources),
    });
}

auto locate_fetch_seed(const Vec<PathBuf>& roots, const FetchIdentity& identity)
    -> SourceResult<Option<PathBuf>> {
    for (const auto& root : roots) {
        auto catalog = load_fetch_seed_catalog(root.as_path());
        if (catalog.is_err()) return Err(rstd::move(catalog).unwrap_err());
        auto path = catalog->lookup(identity);
        if (path.is_none()) continue;
        auto exists = rstd::fs::exists(path->as_path());
        if (exists.is_err()) {
            return source_io_failure<Option<PathBuf>>(
                "inspect fetch seed source"_str, path->as_path(), rstd::move(exists).unwrap_err());
        }
        if (! *exists) continue;
        auto canonical_root = rstd::fs::canonicalize(root.as_path());
        if (canonical_root.is_err()) {
            return source_io_failure<Option<PathBuf>>("resolve fetch seed root"_str,
                                                      root.as_path(),
                                                      rstd::move(canonical_root).unwrap_err());
        }
        auto canonical_source = rstd::fs::canonicalize(path->as_path());
        if (canonical_source.is_err()) {
            return source_io_failure<Option<PathBuf>>("resolve fetch seed source"_str,
                                                      path->as_path(),
                                                      rstd::move(canonical_source).unwrap_err());
        }
        if (! canonical_source->as_path().starts_with(canonical_root->as_path())) {
            return source_failure<Option<PathBuf>>(
                "fetch seed source resolves outside its catalog root"_str);
        }
        return Ok(Some(rstd::move(canonical_source).unwrap()));
    }
    return Ok(None());
}

} // namespace lito::source
