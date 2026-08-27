module;
#include <rstd/macro.hpp>

module lito.tools.cargo;

import rstd;
import rstd.json;
import rstd.toml;
import lito.core;

using namespace rstd::prelude;
using namespace rstd::literals;
using PathBuf = rstd::path::PathBuf;
using Toml    = rstd::toml::Value;
using Table   = rstd::toml::Table;
using Json    = rstd::json::Value;
using JsonMap = rstd::json::Map;

namespace cargo = lito::tools::cargo;

template<typename T>
auto cargo_flatpak_failure(String message) -> cargo::FlatpakExportResult<T> {
    return Err(cargo::FlatpakExportError::Message(rstd::move(message)));
}

template<typename T>
auto cargo_flatpak_failure(ref<str> message) -> cargo::FlatpakExportResult<T> {
    return cargo_flatpak_failure<T>(String::make(message));
}

template<typename T>
auto cargo_flatpak_io_failure(ref<str>               operation,
                              ref<rstd::path::Path>  path,
                              rstd::io::error::Error source) -> cargo::FlatpakExportResult<T> {
    return Err(cargo::FlatpakExportError::Io(
        String::make(operation), PathBuf::from(path), rstd::move(source)));
}

auto toml_table(const Toml& value, ref<str> context) -> cargo::FlatpakExportResult<ref<Table>> {
    auto table = value.as_table();
    if (table.is_none()) {
        return cargo_flatpak_failure<ref<Table>>(rstd::format("{} must be a table", context));
    }
    return Ok(*table);
}

auto toml_array(const Toml& value, ref<str> context)
    -> cargo::FlatpakExportResult<ref<rstd::toml::Array>> {
    auto array = value.as_array();
    if (array.is_none()) {
        return cargo_flatpak_failure<ref<rstd::toml::Array>>(
            rstd::format("{} must be an array", context));
    }
    return Ok(*array);
}

auto required_toml_member(const Toml& value, ref<str> key, ref<str> context)
    -> cargo::FlatpakExportResult<ref<Toml>> {
    rstd_try(toml_table(value, context));
    auto member = value.get(key);
    if (member.is_none()) {
        return cargo_flatpak_failure<ref<Toml>>(rstd::format("{} is missing '{}'", context, key));
    }
    return Ok(*member);
}

auto required_toml_string(const Toml& value, ref<str> key, ref<str> context)
    -> cargo::FlatpakExportResult<String> {
    auto member = rstd_try(required_toml_member(value, key, context));
    auto text   = (*member).as_str();
    if (text.is_none() || text->is_empty()) {
        return cargo_flatpak_failure<String>(
            rstd::format("{}.{} must be a non-empty string", context, key));
    }
    return Ok(String::make(*text));
}

auto optional_toml_string(const Toml& value, ref<str> key, ref<str> context)
    -> cargo::FlatpakExportResult<Option<String>> {
    auto member = value.get(key);
    if (member.is_none()) return Ok(None());
    auto text = (**member).as_str();
    if (text.is_none() || text->is_empty()) {
        return cargo_flatpak_failure<Option<String>>(
            rstd::format("{}.{} must be a non-empty string", context, key));
    }
    return Ok(Some(String::make(*text)));
}

auto hex_value(u8 value) noexcept -> Option<u8> {
    if (value >= u8('0') && value <= u8('9')) return Some(value - u8('0'));
    if (value >= u8('a') && value <= u8('f')) return Some(value - u8('a') + u8(10));
    if (value >= u8('A') && value <= u8('F')) return Some(value - u8('A') + u8(10));
    return None();
}

auto decode_query_value(ref<str> value, ref<str> source) -> cargo::FlatpakExportResult<String> {
    auto bytes = Vec<u8>::with_capacity(value.len());
    for (usize index {}; index < value.len(); ++index) {
        auto byte = value[index];
        if (byte == u8('+')) {
            bytes.push(u8(' '));
            continue;
        }
        if (byte != u8('%')) {
            bytes.emplace_back(byte);
            continue;
        }
        if (index + usize(2) >= value.len()) {
            return cargo_flatpak_failure<String>(
                rstd::format("Cargo Git source '{}' has invalid percent encoding", source));
        }
        auto high = hex_value(value[index + usize(1)]);
        auto low  = hex_value(value[index + usize(2)]);
        if (high.is_none() || low.is_none()) {
            return cargo_flatpak_failure<String>(
                rstd::format("Cargo Git source '{}' has invalid percent encoding", source));
        }
        bytes.push(*high * u8(16) + *low);
        index += usize(2);
    }
    auto decoded = String::from_utf8(rstd::move(bytes));
    if (decoded.is_err()) {
        return cargo_flatpak_failure<String>(
            rstd::format("Cargo Git source '{}' has a non-UTF-8 selector", source));
    }
    return Ok(rstd::move(decoded).unwrap());
}

auto parse_git_selector(Option<ref<str>> query, ref<str> source)
    -> cargo::FlatpakExportResult<Option<cargo::GitSelector>> {
    if (query.is_none() || query->is_empty()) return Ok(None());
    auto result    = Option<cargo::GitSelector> {};
    auto remaining = *query;
    while (! remaining.is_empty()) {
        auto split = remaining.split_once("&"_str);
        auto item  = split.is_some() ? split->template get<0>() : remaining;
        remaining  = split.is_some() ? split->template get<1>() : ref<str> {};
        auto pair  = item.split_once("="_str);
        if (pair.is_none() || pair->template get<0>().is_empty()) {
            return cargo_flatpak_failure<Option<cargo::GitSelector>>(
                rstd::format("Cargo Git source '{}' has an invalid selector", source));
        }
        auto kind = lito::source::GitReferenceKind::DefaultBranch;
        auto key  = pair->template get<0>();
        if (key == "rev"_str) {
            kind = lito::source::GitReferenceKind::Rev;
        } else if (key == "tag"_str) {
            kind = lito::source::GitReferenceKind::Tag;
        } else if (key == "branch"_str) {
            kind = lito::source::GitReferenceKind::Branch;
        } else {
            continue;
        }
        if (result.is_some()) {
            return cargo_flatpak_failure<Option<cargo::GitSelector>>(rstd::format(
                "Cargo Git source '{}' contains more than one reference selector", source));
        }
        auto value = rstd_try(decode_query_value(pair->template get<1>(), source));
        if (value.is_empty()) {
            return cargo_flatpak_failure<Option<cargo::GitSelector>>(
                rstd::format("Cargo Git source '{}' has an empty selector", source));
        }
        result = Some(cargo::GitSelector { kind, rstd::move(value) });
    }
    return Ok(rstd::move(result));
}

auto canonical_git_url(ref<str> value, ref<str> source) -> cargo::FlatpakExportResult<String> {
    auto parsed = lito::parse::FetchUrl::parse(value);
    if (parsed.is_err()) {
        return cargo_flatpak_failure<String>(
            rstd::format("Cargo Git source '{}' has an invalid repository URL", source));
    }
    auto result = String::make(value);
    while (result.as_str().ends_with("/"_str)) result.truncate(result.len() - usize(1));
    if (result.as_str().ends_with(".git"_str)) result.truncate(result.len() - usize(4));
    if (result.as_str().starts_with("http://github.com/"_str)) {
        auto suffix = result.as_str().strip_prefix("http://"_str).unwrap();
        auto secure = String::make("https://"_str);
        secure.push_str(suffix);
        result = rstd::move(secure);
    }
    if (result.as_str().starts_with("https://github.com/"_str)) {
        result.as_mut_str().make_ascii_lowercase();
    }
    return Ok(rstd::move(result));
}

auto parse_git_source(ref<str> source) -> cargo::FlatpakExportResult<cargo::GitSource> {
    auto raw  = source.strip_prefix("git+"_str).unwrap();
    auto hash = raw.rsplit_once("#"_str);
    if (hash.is_none() || hash->template get<0>().contains("#"_str) ||
        ! lito::source::git_commit_is_valid(hash->template get<1>())) {
        return cargo_flatpak_failure<cargo::GitSource>(
            rstd::format("Cargo Git source '{}' must end with a full 40-digit commit", source));
    }
    auto repository = hash->template get<0>();
    auto query      = repository.split_once("?"_str);
    auto url        = query.is_some() ? query->template get<0>() : repository;
    auto selector   = rstd_try(
        parse_git_selector(query.is_some() ? Some(query->template get<1>()) : None(), source));
    return Ok(cargo::GitSource {
        .original = String::make(source),
        .url      = rstd_try(canonical_git_url(url, source)),
        .commit   = String::make(hash->template get<1>()),
        .selector = rstd::move(selector),
    });
}

auto parse_locked_source(ref<str> source) -> cargo::FlatpakExportResult<cargo::LockedSource> {
    if (source == "registry+https://github.com/rust-lang/crates.io-index"_str ||
        source == "sparse+https://index.crates.io/"_str ||
        source == "sparse+https://index.crates.io"_str) {
        return Ok(cargo::LockedSource::CratesIo());
    }
    if (source.starts_with("git+"_str)) {
        return Ok(cargo::LockedSource::Git(rstd_try(parse_git_source(source))));
    }
    return cargo_flatpak_failure<cargo::LockedSource>(
        rstd::format("Cargo source '{}' is not supported for Flatpak export", source));
}

auto package_context(ref<str> name, ref<str> version) -> String {
    return rstd::format("Cargo.lock package '{} {}'", name, version);
}

auto cargo::parse_locked_document(ref<rstd::path::Path> path)
    -> FlatpakExportResult<LockedDocument> {
    auto contents = rstd::fs::read_to_string(path);
    if (contents.is_err()) {
        return cargo_flatpak_io_failure<LockedDocument>(
            "read Cargo lock"_str, path, rstd::move(contents).unwrap_err());
    }
    auto parsed = rstd::toml::from_str(contents->as_str());
    if (parsed.is_err()) {
        return Err(FlatpakExportError::Toml(PathBuf::from(path), rstd::move(parsed).unwrap_err()));
    }
    auto document = rstd::move(parsed).unwrap();
    rstd_try(toml_table(document, "Cargo.lock"_str));
    auto version_value = rstd_try(required_toml_member(document, "version"_str, "Cargo.lock"_str));
    auto version       = version_value->as_integer();
    if (version.is_none() || (version->to_primitive() != 3 && version->to_primitive() != 4)) {
        return cargo_flatpak_failure<LockedDocument>("Cargo.lock version must be 3 or 4"_str);
    }
    auto package_values = rstd_try(
        toml_array(*rstd_try(required_toml_member(document, "package"_str, "Cargo.lock"_str)),
                   "Cargo.lock.package"_str));
    auto packages = Vec<LockedPackage>::with_capacity((*package_values).len());
    for (usize index {}; index < (*package_values).len(); ++index) {
        const auto& value   = (*package_values)[index];
        auto        context = rstd::format("Cargo.lock.package[{}]", index);
        rstd_try(toml_table(value, context.as_str()));
        auto name         = rstd_try(required_toml_string(value, "name"_str, context.as_str()));
        auto version_text = rstd_try(required_toml_string(value, "version"_str, context.as_str()));
        auto source_text  = rstd_try(optional_toml_string(value, "source"_str, context.as_str()));
        auto checksum_text =
            rstd_try(optional_toml_string(value, "checksum"_str, context.as_str()));
        auto source = Option<LockedSource> {};
        if (source_text.is_some()) {
            auto parsed_source = parse_locked_source(source_text->as_str());
            if (parsed_source.is_err()) {
                return cargo_flatpak_failure<LockedDocument>(
                    rstd::format("{} has an invalid source '{}': {}",
                                 package_context(name.as_str(), version_text.as_str()).as_str(),
                                 source_text->as_str(),
                                 rstd::move(parsed_source).unwrap_err()));
            }
            source = Some(rstd::move(parsed_source).unwrap());
        }
        auto checksum = Option<lito::crypto::Sha256Digest> {};
        if (checksum_text.is_some()) {
            auto parsed_checksum = lito::parse::parse_sha256(
                checksum_text->as_str(), lito::parse::Sha256TextMode::Canonical);
            if (parsed_checksum.is_err()) {
                return cargo_flatpak_failure<LockedDocument>(
                    rstd::format("{} has an invalid checksum",
                                 package_context(name.as_str(), version_text.as_str()).as_str()));
            }
            checksum = Some(rstd::move(parsed_checksum).unwrap());
        }
        if (source.is_some() && source->is_CratesIo() && checksum.is_none()) {
            auto package = package_context(name.as_str(), version_text.as_str());
            return cargo_flatpak_failure<LockedDocument>(
                rstd::format("{} is missing checksum", package.as_str()));
        }
        packages.push(LockedPackage {
            .name     = rstd::move(name),
            .version  = rstd::move(version_text),
            .source   = rstd::move(source),
            .checksum = rstd::move(checksum),
        });
    }
    rstd::slice_::sort_unstable_by(
        packages.as_mut_slice().as_mut_ref(),
        [](const LockedPackage& left, const LockedPackage& right) {
            if (left.name != right.name) return left.name < right.name;
            if (left.version != right.version) return left.version < right.version;
            auto left_git  = left.source.is_some() && left.source->is_Git();
            auto right_git = right.source.is_some() && right.source->is_Git();
            if (left_git != right_git) return ! left_git;
            if (! left_git) return false;
            return left.source->as_Git().source.original < right.source->as_Git().source.original;
        });
    return Ok(LockedDocument {
        .path     = PathBuf::from(path),
        .version  = u64(version->to_primitive()),
        .packages = rstd::move(packages),
    });
}

auto cargo::locked_git_requests(const LockedDocument& document) -> Vec<GitRequest> {
    auto unique = rstd::collections::BTreeMap<String, GitRequest>::make();
    for (const auto& package : document.packages) {
        if (package.source.is_none() || ! package.source->is_Git()) continue;
        const auto& source = package.source->as_Git().source;
        auto        key    = rstd::format("{}\n{}", source.url.as_str(), source.commit.as_str());
        if (unique.contains_key(key.as_str())) continue;
        unique.insert(rstd::move(key), GitRequest { source.url.clone(), source.commit.clone() });
    }
    auto result = Vec<GitRequest>::with_capacity(unique.len());
    for (auto value : unique.values()) {
        result.push(GitRequest { (*value).url.clone(), (*value).commit.clone() });
    }
    return result;
}

struct ScannedPackage {
    PathBuf      relative;
    Toml         document;
    Option<Toml> workspace;
};

struct ScannedRepository {
    String                                              key;
    rstd::collections::BTreeMap<String, ScannedPackage> packages;
};

auto read_toml_file(ref<rstd::path::Path> path) -> cargo::FlatpakExportResult<Toml> {
    auto contents = rstd::fs::read_to_string(path);
    if (contents.is_err()) {
        return cargo_flatpak_io_failure<Toml>(
            "read Cargo manifest"_str, path, rstd::move(contents).unwrap_err());
    }
    auto parsed = rstd::toml::from_str(contents->as_str());
    if (parsed.is_err()) {
        return Err(
            cargo::FlatpakExportError::Toml(PathBuf::from(path), rstd::move(parsed).unwrap_err()));
    }
    return Ok(rstd::move(parsed).unwrap());
}

auto child_directories(ref<rstd::path::Path> directory)
    -> cargo::FlatpakExportResult<Vec<PathBuf>> {
    auto opened = rstd::fs::read_dir(directory);
    if (opened.is_err()) {
        return cargo_flatpak_io_failure<Vec<PathBuf>>(
            "enumerate Cargo Git checkout"_str, directory, rstd::move(opened).unwrap_err());
    }
    auto result  = Vec<PathBuf>::make();
    auto entries = rstd::move(opened).unwrap();
    for (auto next : entries) {
        if (next.is_err()) {
            return cargo_flatpak_io_failure<Vec<PathBuf>>(
                "enumerate Cargo Git checkout"_str, directory, rstd::move(next).unwrap_err());
        }
        auto entry = rstd::move(next).unwrap();
        auto type  = entry.file_type();
        if (type.is_err()) {
            auto path = entry.path();
            return cargo_flatpak_io_failure<Vec<PathBuf>>("inspect Cargo Git checkout entry"_str,
                                                          path.as_path(),
                                                          rstd::move(type).unwrap_err());
        }
        if (! type->is_dir() || type->is_symlink()) continue;
        auto name = entry.file_name().into_string();
        if (name.is_err()) {
            return cargo_flatpak_failure<Vec<PathBuf>>(
                rstd::format("Cargo Git checkout '{}' contains a non-UTF-8 directory", directory));
        }
        auto text = name->as_str();
        if (text == ".git"_str || text == "target"_str) continue;
        result.push(entry.path());
    }
    return Ok(rstd::move(result));
}

auto scan_repository_directory(ref<rstd::path::Path> directory,
                               ref<rstd::path::Path> root,
                               Option<Toml>          inherited_workspace,
                               rstd::collections::BTreeMap<String, ScannedPackage>& packages)
    -> cargo::FlatpakExportResult<empty> {
    auto manifest  = PathBuf::from(directory).join(PathBuf::from("Cargo.toml"_str).as_path());
    auto metadata  = rstd::fs::symlink_metadata(manifest.as_path());
    auto workspace = rstd::move(inherited_workspace);
    if (metadata.is_ok()) {
        if (! metadata->is_file() || metadata->is_symlink()) {
            return cargo_flatpak_failure<empty>(
                rstd::format("Cargo manifest '{}' must be a regular file", manifest.as_path()));
        }
        auto document        = rstd_try(read_toml_file(manifest.as_path()));
        auto root_table      = rstd_try(toml_table(document, "Cargo manifest"_str));
        auto local_workspace = document.get("workspace"_str);
        if (local_workspace.is_some()) {
            rstd_try(toml_table(**local_workspace, "Cargo manifest workspace"_str));
            workspace = Some((**local_workspace).clone());
        }
        auto package = document.get("package"_str);
        if (package.is_some()) {
            auto name = rstd_try(required_toml_string(**package, "name"_str, "Cargo package"_str));
            if (packages.contains_key(name.as_str())) {
                return cargo_flatpak_failure<empty>(rstd::format(
                    "Cargo Git checkout '{}' contains more than one package named '{}'",
                    root,
                    name.as_str()));
            }
            auto relative = directory.strip_prefix(root);
            if (relative.is_none()) {
                return cargo_flatpak_failure<empty>(rstd::format(
                    "Cargo package directory '{}' escapes checkout '{}'", directory, root));
            }
            packages.insert(
                rstd::move(name),
                ScannedPackage {
                    .relative  = PathBuf::from(*relative),
                    .document  = rstd::move(document),
                    .workspace = workspace.is_some() ? Some(workspace->clone()) : None<Toml>(),
                });
        }
        (void)root_table;
    } else {
        auto error = rstd::move(metadata).unwrap_err();
        if (error.kind() != rstd::io::error::ErrorKind { rstd::io::error::ErrorKind::NotFound }) {
            return cargo_flatpak_io_failure<empty>(
                "inspect Cargo manifest"_str, manifest.as_path(), rstd::move(error));
        }
    }
    auto children = rstd_try(child_directories(directory));
    for (const auto& child : children) {
        auto child_workspace = workspace.is_some() ? Some(workspace->clone()) : None<Toml>();
        rstd_try(scan_repository_directory(
            child.as_path(), root, rstd::move(child_workspace), packages));
    }
    return Ok(empty {});
}

auto scan_repository(const cargo::GitCheckout& checkout)
    -> cargo::FlatpakExportResult<ScannedRepository> {
    auto packages = rstd::collections::BTreeMap<String, ScannedPackage>::make();
    rstd_try(scan_repository_directory(
        checkout.root.as_path(), checkout.root.as_path(), None(), packages));
    if (packages.is_empty()) {
        return cargo_flatpak_failure<ScannedRepository>(
            rstd::format("Cargo Git checkout '{}' contains no packages", checkout.root.as_path()));
    }
    return Ok(ScannedRepository {
        .key      = rstd::format("{}\n{}", checkout.url.as_str(), checkout.commit.as_str()),
        .packages = rstd::move(packages),
    });
}

auto clone_keys(const Table& table) -> Vec<String> {
    auto result = Vec<String>::with_capacity(table.len());
    for (auto key : table.keys()) result.push((*key).clone());
    return result;
}

auto merge_features(Toml& destination, const Toml& inherited) -> void {
    auto target = destination.as_array_mut();
    auto source = inherited.as_array();
    if (target.is_none() || source.is_none()) {
        destination = inherited.clone();
        return;
    }
    for (const auto& feature : **source) (**target).push(feature.clone());
}

auto apply_workspace_values(Table& package, const Table* workspace) -> void {
    auto keys = clone_keys(package);
    for (const auto& key : keys) {
        auto item_ref = package.get_mut(key.as_str());
        if (item_ref.is_none()) continue;
        auto& item = **item_ref;
        if (key.as_str() == "target"_str) {
            auto targets = item.as_table_mut();
            if (targets.is_some()) {
                for (auto target : (**targets).values_mut()) {
                    auto table = (*target).as_table_mut();
                    if (table.is_some()) apply_workspace_values(**table, workspace);
                }
            }
            continue;
        }
        if (key.as_str() == "dev-dependencies"_str || key.as_str() == "build-dependencies"_str) {
            auto table = item.as_table_mut();
            auto inherited =
                workspace == nullptr ? Option<ref<Toml>> {} : workspace->get("dependencies"_str);
            auto inherited_table =
                inherited.is_some() ? (**inherited).as_table() : None<ref<Table>>();
            if (table.is_some()) {
                apply_workspace_values(
                    **table,
                    inherited_table.is_some() ? rstd::addressof(**inherited_table) : nullptr);
            }
            continue;
        }
        if (workspace == nullptr) continue;
        auto inherited = workspace->get(key.as_str());
        if (inherited.is_none()) continue;
        auto item_table = item.as_table_mut();
        if (item_table.is_none()) continue;
        if ((**item_table).contains_key("workspace"_str)) {
            auto inherited_table = (**inherited).as_table();
            if (inherited_table.is_some()) {
                (**item_table).remove("workspace"_str);
                auto inherited_keys = clone_keys(**inherited_table);
                for (const auto& inherited_key : inherited_keys) {
                    auto value = (**inherited_table).get(inherited_key.as_str()).unwrap();
                    if (inherited_key.as_str() == "features"_str &&
                        (**item_table).contains_key("features"_str)) {
                        auto features = (**item_table).get_mut("features"_str).unwrap();
                        merge_features(*features, *value);
                    } else {
                        (**item_table).insert(inherited_key.clone(), value->clone());
                    }
                }
            } else if ((**item_table).len() > usize(1)) {
                (**item_table).remove("workspace"_str);
                (**item_table).insert(String::make("version"_str), (**inherited).clone());
            } else {
                item = (**inherited).clone();
            }
            continue;
        }
        auto inherited_table = (**inherited).as_table();
        if (inherited_table.is_some()) {
            apply_workspace_values(**item_table, rstd::addressof(**inherited_table));
        }
    }
}

auto normalized_manifest(const ScannedPackage& package, ref<rstd::path::Path> path)
    -> cargo::FlatpakExportResult<String> {
    auto document = package.document.clone();
    auto table    = document.as_table_mut().unwrap();
    auto workspace =
        package.workspace.is_some() ? package.workspace->as_table() : None<ref<Table>>();
    apply_workspace_values(*table, workspace.is_some() ? rstd::addressof(**workspace) : nullptr);
    auto serialized = rstd::toml::to_string(document);
    if (serialized.is_err()) {
        return Err(cargo::FlatpakExportError::Serialize(PathBuf::from(path),
                                                        rstd::move(serialized).unwrap_err()));
    }
    return Ok(rstd::move(serialized).unwrap());
}

auto checksum_json(Option<ref<lito::crypto::Sha256Digest>> checksum) -> String {
    auto files = JsonMap::make();
    auto root  = JsonMap::make();
    if (checksum.is_some()) {
        auto text = (*checksum)->to_hex();
        root.insert(String::make("package"_str), Json::String(rstd::move(text)));
    } else {
        root.insert(String::make("package"_str), Json::Null());
    }
    root.insert(String::make("files"_str), Json::Object(rstd::move(files)));
    return rstd::json::to_string(Json::Object(rstd::move(root)));
}

auto git_repository_name(ref<str> url, ref<str> commit) -> cargo::FlatpakExportResult<String> {
    auto slash = url.rsplit_once("/"_str);
    auto name  = slash.is_some() ? slash->template get<1>() : ref<str> {};
    if (name.is_empty()) {
        return cargo_flatpak_failure<String>(
            rstd::format("Cargo Git URL '{}' has no repository name", url));
    }
    auto prefix = commit.get(usize {}, usize(7));
    if (prefix.is_none()) {
        return cargo_flatpak_failure<String>(
            rstd::format("Cargo Git commit '{}' is too short", commit));
    }
    return Ok(rstd::format("{}-{}", name, *prefix));
}

auto shell_quote(ref<str> value) -> String {
    auto result = String::make("'"_str);
    for (auto byte : value.as_bytes()) {
        if (byte == u8('\'')) {
            result.push_str("'\\''"_str);
        } else {
            result.push_ascii(byte);
        }
    }
    result.push_ascii('\'');
    return result;
}

auto checkout_for(const Vec<cargo::GitCheckout>& checkouts, ref<str> url, ref<str> commit)
    -> cargo::FlatpakExportResult<ref<cargo::GitCheckout>> {
    auto matched = Option<ref<cargo::GitCheckout>> {};
    for (const auto& checkout : checkouts) {
        if (checkout.url.as_str() != url || checkout.commit.as_str() != commit) continue;
        if (matched.is_some()) {
            return cargo_flatpak_failure<ref<cargo::GitCheckout>>(rstd::format(
                "Cargo Git source '{}#{}' has more than one materialized checkout", url, commit));
        }
        matched = Some(ref<cargo::GitCheckout>::from_raw_parts(rstd::addressof(checkout)));
    }
    if (matched.is_none()) {
        return cargo_flatpak_failure<ref<cargo::GitCheckout>>(
            rstd::format("Cargo Git source '{}#{}' has no materialized checkout", url, commit));
    }
    return Ok(*matched);
}

auto scanned_repository_for(Vec<ScannedRepository>&        repositories,
                            const Vec<cargo::GitCheckout>& checkouts,
                            ref<str>                       url,
                            ref<str>                       commit)
    -> cargo::FlatpakExportResult<mut_ref<ScannedRepository>> {
    auto key = rstd::format("{}\n{}", url, commit);
    for (auto& repository : repositories) {
        if (repository.key.as_str() == key.as_str()) {
            return Ok(mut_ref<ScannedRepository>::from_raw_parts(rstd::addressof(repository)));
        }
    }
    auto checkout = rstd_try(checkout_for(checkouts, url, commit));
    repositories.push(rstd_try(scan_repository(*checkout)));
    return Ok(mut_ref<ScannedRepository>::from_raw_parts(
        rstd::addressof(repositories[repositories.len() - usize(1)])));
}

auto selector_equal(const Option<cargo::GitSelector>& left,
                    const Option<cargo::GitSelector>& right) noexcept -> bool {
    if (left.is_some() != right.is_some()) return false;
    if (left.is_none()) return true;
    return left->kind == right->kind && left->value == right->value;
}

auto source_config(const rstd::collections::BTreeMap<String, cargo::GitSource>& git_sources,
                   bool                                                         crates_io,
                   ref<rstd::path::Path> path) -> cargo::FlatpakExportResult<String> {
    auto source   = Table::make();
    auto vendored = Table::make();
    vendored.insert(String::make("directory"_str), Toml::String(String::make("cargo/vendor"_str)));
    source.insert(String::make("vendored-sources"_str), Toml::Table(rstd::move(vendored)));
    if (crates_io) {
        auto crates = Table::make();
        crates.insert(String::make("replace-with"_str),
                      Toml::String(String::make("vendored-sources"_str)));
        source.insert(String::make("crates-io"_str), Toml::Table(rstd::move(crates)));
    }
    for (auto entry : git_sources.iter()) {
        const auto& url   = *entry.template get<0>();
        const auto& git   = *entry.template get<1>();
        auto        value = Table::make();
        value.insert(String::make("git"_str), Toml::String(url.clone()));
        value.insert(String::make("replace-with"_str),
                     Toml::String(String::make("vendored-sources"_str)));
        if (git.selector.is_some()) {
            value.insert(String::make(lito::source::git_reference_kind_name(git.selector->kind)),
                         Toml::String(git.selector->value.clone()));
        }
        source.insert(url.clone(), Toml::Table(rstd::move(value)));
    }
    auto root = Table::make();
    root.insert(String::make("source"_str), Toml::Table(rstd::move(source)));
    auto serialized = rstd::toml::to_string(Toml::Table(rstd::move(root)));
    if (serialized.is_err()) {
        return Err(cargo::FlatpakExportError::Serialize(PathBuf::from(path),
                                                        rstd::move(serialized).unwrap_err()));
    }
    return Ok(rstd::move(serialized).unwrap());
}

auto cargo::project_flatpak_sources(const LockedDocument&   document,
                                    const Vec<GitCheckout>& checkouts)
    -> FlatpakExportResult<lito::flatpak::SourceSet> {
    auto expected_requests = locked_git_requests(document);
    if (expected_requests.len() != checkouts.len()) {
        return cargo_flatpak_failure<lito::flatpak::SourceSet>(
            rstd::format("Cargo.lock '{}' requires {} Git checkouts but {} were provided",
                         document.path.as_path(),
                         expected_requests.len(),
                         checkouts.len()));
    }

    auto result            = lito::flatpak::SourceSet {};
    auto repositories      = Vec<ScannedRepository>::make();
    auto git_sources       = rstd::collections::BTreeMap<String, GitSource>::make();
    auto registry_packages = rstd::collections::BTreeMap<String, empty>::make();
    auto crates_io         = false;

    for (const auto& request : expected_requests) {
        auto checkout =
            rstd_try(checkout_for(checkouts, request.url.as_str(), request.commit.as_str()));
        (void)rstd_try(scanned_repository_for(
            repositories, checkouts, request.url.as_str(), request.commit.as_str()));
        auto name = rstd_try(git_repository_name(request.url.as_str(), request.commit.as_str()));
        auto destination =
            PathBuf::from("flatpak-cargo/git"_str).join(PathBuf::from(name.as_str()).as_path());
        result.push(
            rstd::format("Cargo.lock Git '{}#{}'", request.url.as_str(), request.commit.as_str()),
            lito::flatpak::Source::Git(request.url.clone(),
                                       request.commit.clone(),
                                       rstd::move(destination),
                                       Vec<String>::make()));
        (void)checkout;
    }

    for (const auto& package : document.packages) {
        if (package.source.is_none()) continue;
        auto origin = package_context(package.name.as_str(), package.version.as_str());
        if (package.source->is_CratesIo()) {
            crates_io     = true;
            auto checksum = package.checksum->to_hex();
            auto identity = rstd::format(
                "{}\n{}\n{}", package.name.as_str(), package.version.as_str(), checksum.as_str());
            if (registry_packages.contains_key(identity.as_str())) continue;
            registry_packages.insert(rstd::move(identity), empty {});
            auto destination =
                rstd::format("cargo/vendor/{}-{}", package.name.as_str(), package.version.as_str());
            auto url = rstd::format("https://static.crates.io/crates/{}/{}-{}.crate",
                                    package.name.as_str(),
                                    package.name.as_str(),
                                    package.version.as_str());
            result.push(origin.clone(),
                        lito::flatpak::Source::Archive(rstd::move(url),
                                                       package.checksum->clone(),
                                                       lito::flatpak::ArchiveType::TarGzip,
                                                       PathBuf::from(destination.as_str()),
                                                       Vec<String>::make()));
            result.push(rstd::move(origin),
                        lito::flatpak::Source::Inline(
                            checksum_json(Some(ref<lito::crypto::Sha256Digest>::from_raw_parts(
                                rstd::addressof(*package.checksum)))),
                            PathBuf::from(destination.as_str()),
                            String::make(".cargo-checksum.json"_str)));
            continue;
        }

        const auto& git      = package.source->as_Git().source;
        auto        existing = git_sources.get(git.url.as_str());
        if (existing.is_some() && ((**existing).commit != git.commit ||
                                   ! selector_equal((**existing).selector, git.selector))) {
            return cargo_flatpak_failure<lito::flatpak::SourceSet>(
                rstd::format("Cargo.lock '{}' uses Git source '{}' with incompatible selectors",
                             document.path.as_path(),
                             git.url.as_str()));
        }
        if (existing.is_none()) git_sources.insert(git.url.clone(), git.clone());

        auto repository = rstd_try(
            scanned_repository_for(repositories, checkouts, git.url.as_str(), git.commit.as_str()));
        auto scanned = repository->packages.get(package.name.as_str());
        if (scanned.is_none()) {
            return cargo_flatpak_failure<lito::flatpak::SourceSet>(
                rstd::format("{} was not found in Git checkout '{}#{}'",
                             origin.as_str(),
                             git.url.as_str(),
                             git.commit.as_str()));
        }
        auto repository_name = rstd_try(git_repository_name(git.url.as_str(), git.commit.as_str()));
        auto source_path     = PathBuf::from("flatpak-cargo/git"_str)
                                   .join(PathBuf::from(repository_name.as_str()).as_path())
                                   .join((**scanned).relative.as_path());
        auto destination =
            PathBuf::from("cargo/vendor"_str).join(PathBuf::from(package.name.as_str()).as_path());
        auto source_text      = source_path.as_path().to_str().unwrap();
        auto destination_text = destination.as_path().to_str().unwrap();
        auto command          = rstd::format("cp -r --reflink=auto {}/. {}",
                                             shell_quote(source_text).as_str(),
                                             shell_quote(destination_text).as_str());
        auto commands         = Vec<String>::make();
        commands.push(rstd::move(command));
        result.push(origin.clone(), lito::flatpak::Source::Shell(rstd::move(commands)));
        auto manifest_path = (**scanned).relative.join(PathBuf::from("Cargo.toml"_str).as_path());
        result.push(origin.clone(),
                    lito::flatpak::Source::Inline(
                        rstd_try(normalized_manifest(**scanned, manifest_path.as_path())),
                        destination.clone(),
                        String::make("Cargo.toml"_str)));
        result.push(rstd::move(origin),
                    lito::flatpak::Source::Inline(checksum_json(None()),
                                                  rstd::move(destination),
                                                  String::make(".cargo-checksum.json"_str)));
    }

    if (! document.packages.is_empty()) {
        result.push(rstd::format("Cargo.lock '{}' source replacement", document.path.as_path()),
                    lito::flatpak::Source::Inline(
                        rstd_try(source_config(git_sources, crates_io, document.path.as_path())),
                        PathBuf::from("cargo"_str),
                        String::make("config"_str)));
    }
    return Ok(rstd::move(result));
}

auto rstd::Impl<rstd::fmt::Display, cargo::FlatpakExportError>::fmt(
    rstd::fmt::Formatter& formatter) const -> bool {
    const auto& error = this->self();
    if (error.is_Message()) return formatter.write_str(error.as_Message().message.as_str());
    if (error.is_Io()) {
        const auto& value = error.as_Io();
        return formatter.write_fmt(
            rstd::fmt::Arguments::make("cannot {} '{}'", value.operation, value.path.as_path()));
    }
    if (error.is_Toml()) {
        return formatter.write_fmt(rstd::fmt::Arguments::make("cannot parse Cargo TOML '{}'",
                                                              error.as_Toml().path.as_path()));
    }
    return formatter.write_fmt(rstd::fmt::Arguments::make("cannot serialize Cargo TOML '{}'",
                                                          error.as_Serialize().path.as_path()));
}

auto rstd::Impl<rstd::fmt::Debug, cargo::FlatpakExportError>::fmt(
    rstd::fmt::Formatter& formatter) const -> bool {
    return rstd::as<rstd::fmt::Display>(this->self()).fmt(formatter);
}

auto rstd::Impl<rstd::error::Error, cargo::FlatpakExportError>::source() const noexcept
    -> Option<rstd::error::ErrorRef> {
    const auto& error = this->self();
    if (error.is_Io()) return Some(dyn<rstd::error::Error>::from_ref(error.as_Io().source));
    if (error.is_Toml()) return Some(dyn<rstd::error::Error>::from_ref(error.as_Toml().source));
    if (error.is_Serialize()) {
        return Some(dyn<rstd::error::Error>::from_ref(error.as_Serialize().source));
    }
    return None();
}
