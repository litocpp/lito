export module lito.cache;

import rstd;
import rstd.json;
import lito.model;
import lito.frontend;
import lito.build_layout;
import :hash;

using namespace rstd::prelude;
using namespace rstd::literals;
using Json      = rstd::json::Value;
using JsonMap   = rstd::json::Map;
using JsonArray = rstd::json::Array;

namespace lito
{

inline constexpr auto CACHE_VERSION  = u64(3);
inline constexpr auto SCAN_RECIPE    = "lito-native-frontend-v1"_str;
inline constexpr auto COMPILE_RECIPE = "clang-cxx-compile-v3"_str;

template<typename T>
auto cache_failure(String message) -> Result<T> {
    return Err(Error::make(ErrorKind::Artifact, rstd::move(message)));
}

auto cache_string(ref<str> value) -> Json {
    return Json::String(String::make(value));
}

auto cache_u64(u64 value) -> Json {
    return Json::Number(rstd::json::Number::from_u64(value));
}

auto cache_i64(i64 value) -> Json {
    return Json::Number(rstd::json::Number::from_i64(value));
}

auto cache_version(const Json& document) -> Option<u64> {
    auto version = document.get("version"_str);
    if (version.is_none()) return None();
    return (**version).as_u64();
}

auto path_string(ref<rstd::path::Path> path) -> Result<String> {
    auto value = path.to_str();
    if (value.is_none()) {
        return cache_failure<String>(rstd::format("cache path '{}' is not valid UTF-8", path));
    }
    return Ok(String::make(*value));
}

auto write_json(ref<rstd::path::Path> path, const Json& document) -> Result<empty> {
    auto parent = path.parent();
    if (parent.is_none()) {
        return cache_failure<empty>(rstd::format("cache path '{}' has no parent", path));
    }
    auto created = rstd::fs::create_dir_all(*parent);
    if (created.is_err()) {
        return cache_failure<empty>(rstd::format(
            "cannot create cache directory '{}': {}", *parent, rstd::move(created).unwrap_err()));
    }
    auto text = rstd::json::to_string(
        document, rstd::json::FormatOptions { .pretty = true, .indent = usize(2) });
    text.push_ascii('\n');
    auto written = rstd::fs::write_atomic(path, text.as_str().as_bytes());
    if (written.is_err()) {
        return cache_failure<empty>(rstd::format(
            "cannot write cache record '{}': {}", path, rstd::move(written).unwrap_err()));
    }
    return Ok(empty {});
}

struct FileFingerprint {
    PathBuf path;
    u64     size {};
    String  fingerprint;

    auto clone() const -> FileFingerprint {
        return FileFingerprint {
            .path        = path.clone(),
            .size        = size,
            .fingerprint = fingerprint.clone(),
        };
    }
};

auto file_json(const FileFingerprint& file) -> Result<Json> {
    auto path = path_string(file.path.as_path());
    if (path.is_err()) return Err(rstd::move(path).unwrap_err());
    auto object = JsonMap::make();
    object.insert(String::make("fingerprint"_str), cache_string(file.fingerprint.as_str()));
    object.insert(String::make("path"_str), cache_string(path->as_str()));
    object.insert(String::make("size"_str), cache_u64(file.size));
    return Ok(Json::Object(rstd::move(object)));
}

auto output_exists(ref<rstd::path::Path> path) -> Result<bool> {
    auto exists = rstd::fs::exists(path);
    if (exists.is_err()) {
        return cache_failure<bool>(rstd::format(
            "cannot inspect cached output '{}': {}", path, rstd::move(exists).unwrap_err()));
    }
    return Ok(*exists);
}

auto output_content_digest(ref<rstd::path::Path> path) -> Result<String> {
    auto contents = rstd::fs::read(path);
    if (contents.is_err()) {
        return cache_failure<String>(rstd::format(
            "cannot hash compiler output '{}': {}", path, rstd::move(contents).unwrap_err()));
    }
    auto hash = cache::FNV_OFFSET;
    cache::add_text(hash, "lito-output-content-v1"_str);
    cache::add_bytes(hash, contents->as_slice());
    return Ok(cache::hex(hash));
}

auto receipt_output_paths(const Json& document) -> Vec<PathBuf> {
    auto result  = Vec<PathBuf>::make();
    auto outputs = document.get("outputs"_str);
    if (outputs.is_none()) return result;
    const auto append = [&](ref<str> key) {
        auto output = (**outputs).get(key);
        if (output.is_none()) return;
        auto path = (**output).get("path"_str);
        if (path.is_none()) return;
        auto text = (**path).as_str();
        if (text.is_some()) result.push(PathBuf::from(*text));
    };
    append("bmi"_str);
    append("object"_str);
    return result;
}

auto read_receipt_output_paths(ref<rstd::path::Path> path) -> Result<Vec<PathBuf>> {
    auto exists = rstd::fs::exists(path);
    if (exists.is_err()) {
        return cache_failure<Vec<PathBuf>>(
            rstd::format("cannot inspect cache record '{}': {}", path, exists.unwrap_err()));
    }
    if (! *exists) return Ok(Vec<PathBuf>::make());
    auto contents = rstd::fs::read_to_string(path);
    if (contents.is_err()) {
        return cache_failure<Vec<PathBuf>>(
            rstd::format("cannot read cache record '{}': {}", path, contents.unwrap_err()));
    }
    auto parsed = rstd::json::from_str(contents->as_str());
    if (parsed.is_err()) return Ok(Vec<PathBuf>::make());
    return Ok(receipt_output_paths(*parsed));
}

auto remove_owned_output(ref<rstd::path::Path> path, ref<rstd::path::Path> owner_root)
    -> Result<empty> {
    if (path.strip_prefix(owner_root).is_none()) {
        return cache_failure<empty>(
            rstd::format("cache output '{}' is outside build root '{}'", path, owner_root));
    }
    auto exists = rstd::fs::exists(path);
    if (exists.is_err()) {
        return cache_failure<empty>(
            rstd::format("cannot inspect cache output '{}': {}", path, exists.unwrap_err()));
    }
    if (! *exists) return Ok(empty {});
    auto removed = rstd::fs::remove_file(path);
    if (removed.is_err()) {
        return cache_failure<empty>(
            rstd::format("cannot remove cache output '{}': {}", path, removed.unwrap_err()));
    }
    return Ok(empty {});
}

auto collect_stale_records(ref<rstd::path::Path>                             directory,
                           const rstd::collections::BTreeMap<String, empty>& current,
                           ref<rstd::path::Path> owner_root) -> Result<empty> {
    auto exists = rstd::fs::exists(directory);
    if (exists.is_err()) {
        return cache_failure<empty>(rstd::format(
            "cannot inspect cache directory '{}': {}", directory, rstd::move(exists).unwrap_err()));
    }
    if (! *exists) return Ok(empty {});
    auto opened = rstd::fs::read_dir(directory);
    if (opened.is_err()) {
        return cache_failure<empty>(rstd::format("cannot enumerate cache directory '{}': {}",
                                                 directory,
                                                 rstd::move(opened).unwrap_err()));
    }
    auto entries = rstd::move(opened).unwrap();
    for (auto next = entries.next(); next.is_some(); next = entries.next()) {
        auto item = rstd::move(next).unwrap();
        if (item.is_err()) {
            return cache_failure<empty>(rstd::format("cannot enumerate cache directory '{}': {}",
                                                     directory,
                                                     rstd::move(item).unwrap_err()));
        }
        auto entry = rstd::move(item).unwrap();
        auto type  = entry.file_type();
        if (type.is_err()) {
            return cache_failure<empty>(rstd::format("cannot inspect cache entry '{}': {}",
                                                     entry.path().as_path(),
                                                     rstd::move(type).unwrap_err()));
        }
        auto path = entry.path();
        if (type->is_dir()) {
            auto nested = collect_stale_records(path.as_path(), current, owner_root);
            if (nested.is_err()) return nested;
            auto removed = rstd::fs::remove_dir(path.as_path());
            if (removed.is_err() &&
                rstd::move(removed).unwrap_err().kind() !=
                    rstd::io::error::ErrorKind { rstd::io::error::ErrorKind::DirectoryNotEmpty }) {
                return cache_failure<empty>(
                    rstd::format("cannot remove stale cache directory '{}'", path.as_path()));
            }
            continue;
        }
        if (! type->is_file()) continue;
        auto text = path_string(path.as_path());
        if (text.is_err()) return Err(rstd::move(text).unwrap_err());
        if (current.contains_key(text->as_str())) continue;
        auto outputs = read_receipt_output_paths(path.as_path());
        if (outputs.is_err()) return Err(rstd::move(outputs).unwrap_err());
        for (const auto& output : *outputs) {
            auto removed = remove_owned_output(output.as_path(), owner_root);
            if (removed.is_err()) return removed;
        }
        auto removed = rstd::fs::remove_file(path.as_path());
        if (removed.is_err()) {
            return cache_failure<empty>(rstd::format("cannot remove stale cache record '{}': {}",
                                                     path.as_path(),
                                                     rstd::move(removed).unwrap_err()));
        }
    }
    return Ok(empty {});
}

} // namespace lito

export namespace lito
{

struct DependencyArtifact {
    String logical_name;
    String artifact;
};

class CacheEnvironment {
    String key_;
    String scan_key_;
    bool   force_refresh_ { false };

    friend class ScanCacheSession;
    friend class CompileCacheSession;

public:
    static auto create(const BuildLayout&      layout,
                       ref<rstd::path::Path>   owner_root,
                       ref<str>                profile,
                       const CompilerIdentity& compiler) -> Result<CacheEnvironment> {
        auto owner         = path_string(owner_root);
        auto compiler_path = path_string(compiler.path.as_path());
        auto resource      = path_string(compiler.resource_directory.as_path());
        if (owner.is_err()) return Err(rstd::move(owner).unwrap_err());
        if (compiler_path.is_err()) return Err(rstd::move(compiler_path).unwrap_err());
        if (resource.is_err()) return Err(rstd::move(resource).unwrap_err());

        auto identity = String::make("lito-cache-environment-v2\n"_str);
        identity.push_str(owner->as_str());
        identity.push_ascii('\n');
        identity.push_str(profile);
        identity.push_ascii('\n');
        identity.push_str(SCAN_RECIPE);
        identity.push_ascii('\n');
        identity.push_str(COMPILE_RECIPE);
        identity.push_ascii('\n');
        identity.push_str(compiler_path->as_str());
        identity.push_ascii('\n');
        identity.push_str(compiler.version.as_str());
        identity.push_ascii('\n');
        identity.push_str(compiler.target.as_str());
        identity.push_ascii('\n');
        identity.push_str(compiler.build_identity.as_str());
        identity.push_ascii('\n');
        identity.push_str(resource->as_str());
        identity.push_ascii('\n');
        identity.push_str(rstd::format("{}\n{}\n{}",
                                       compiler.size,
                                       compiler.modified_seconds,
                                       compiler.modified_nanoseconds)
                              .as_str());
        auto key = cache::text_identity("lito-cache-environment-key-v2"_str, identity.as_str());

        auto scan_identity = String::make("lito-scan-cache-environment-v1\n"_str);
        scan_identity.push_str(owner->as_str());
        scan_identity.push_ascii('\n');
        scan_identity.push_str(SCAN_RECIPE);
        scan_identity.push_ascii('\n');
        scan_identity.push_str(compiler_path->as_str());
        scan_identity.push_ascii('\n');
        scan_identity.push_str(compiler.version.as_str());
        scan_identity.push_ascii('\n');
        scan_identity.push_str(compiler.target.as_str());
        scan_identity.push_ascii('\n');
        scan_identity.push_str(compiler.build_identity.as_str());
        scan_identity.push_ascii('\n');
        scan_identity.push_str(resource->as_str());
        scan_identity.push_ascii('\n');
        scan_identity.push_str(rstd::format("{}\n{}\n{}",
                                            compiler.size,
                                            compiler.modified_seconds,
                                            compiler.modified_nanoseconds)
                                   .as_str());
        auto scan_key =
            cache::text_identity("lito-scan-cache-environment-key-v1"_str, scan_identity.as_str());

        auto compiler_json = JsonMap::make();
        compiler_json.insert(String::make("modified-nanoseconds"_str),
                             cache_u64(rstd::as_cast<u64>(compiler.modified_nanoseconds)));
        compiler_json.insert(String::make("modified-seconds"_str),
                             cache_i64(compiler.modified_seconds));
        compiler_json.insert(String::make("path"_str), cache_string(compiler_path->as_str()));
        compiler_json.insert(String::make("build-identity"_str),
                             cache_string(compiler.build_identity.as_str()));
        compiler_json.insert(String::make("resource-directory"_str),
                             cache_string(resource->as_str()));
        compiler_json.insert(String::make("size"_str), cache_u64(compiler.size));
        compiler_json.insert(String::make("target"_str), cache_string(compiler.target.as_str()));
        compiler_json.insert(String::make("version"_str), cache_string(compiler.version.as_str()));

        auto root = JsonMap::make();
        root.insert(String::make("compile-recipe"_str), cache_string(COMPILE_RECIPE));
        root.insert(String::make("compiler"_str), Json::Object(rstd::move(compiler_json)));
        root.insert(String::make("key"_str), cache_string(key.as_str()));
        root.insert(String::make("owner-root"_str), cache_string(owner->as_str()));
        root.insert(String::make("profile"_str), cache_string(profile));
        root.insert(String::make("scan-recipe"_str), cache_string(SCAN_RECIPE));
        root.insert(String::make("version"_str), cache_u64(CACHE_VERSION));
        auto desired = Json::Object(rstd::move(root));
        auto path    = layout.cache_environment(key.as_str());

        auto refresh = false;
        auto exists  = rstd::fs::exists(path.as_path());
        if (exists.is_err()) {
            return cache_failure<CacheEnvironment>(
                rstd::format("cannot inspect cache environment '{}': {}",
                             path.as_path(),
                             rstd::move(exists).unwrap_err()));
        }
        if (*exists) {
            auto contents = rstd::fs::read_to_string(path.as_path());
            if (contents.is_err()) {
                return cache_failure<CacheEnvironment>(
                    rstd::format("cannot read cache environment '{}': {}",
                                 path.as_path(),
                                 rstd::move(contents).unwrap_err()));
            }
            auto parsed = rstd::json::from_str(contents->as_str());
            if (parsed.is_err()) {
                auto written = write_json(path.as_path(), desired);
                if (written.is_err()) return Err(rstd::move(written).unwrap_err());
                refresh = true;
            } else if (*parsed != desired) {
                auto version = cache_version(*parsed);
                if (version.is_some() && *version == CACHE_VERSION) {
                    return cache_failure<CacheEnvironment>(
                        rstd::format("cache environment key collision at '{}'", path.as_path()));
                }
                auto written = write_json(path.as_path(), desired);
                if (written.is_err()) return Err(rstd::move(written).unwrap_err());
                refresh = true;
            }
        } else {
            auto written = write_json(path.as_path(), desired);
            if (written.is_err()) return Err(rstd::move(written).unwrap_err());
            refresh = true;
        }
        auto result           = CacheEnvironment {};
        result.key_           = rstd::move(key);
        result.scan_key_      = rstd::move(scan_key);
        result.force_refresh_ = refresh;
        return Ok(rstd::move(result));
    }

    auto key() const -> ref<str> { return key_.as_str(); }
};

enum class ScanCacheMissReason
{
    None,
    Absent,
    Refresh,
    Version,
    Recipe,
    Corrupt,
    Environment,
    Context,
    Source,
    FileDependency,
    IncludeLookup,
    Receipt,
};

struct ScanCacheStatistics {
    usize                hits {};
    usize                misses {};
    usize                uncacheable {};
    usize                absent {};
    usize                refresh {};
    usize                version {};
    usize                recipe {};
    usize                corrupt {};
    usize                environment {};
    usize                context {};
    usize                source {};
    usize                file_dependency {};
    usize                include_lookup {};
    usize                receipt {};
    usize                fingerprint_requests {};
    usize                fingerprint_hits {};
    usize                fingerprint_builds {};
    usize                fingerprint_waits {};
    rstd::time::Duration fingerprint_wait;
};

struct ScanCacheInput {
    PathBuf record;
    String  target;
    PathBuf relative_source;
    PathBuf source;
    String  context_identity;
    PathBuf working_directory;
    String  preprocessor_environment;
};

struct ScanCacheLookup {
    Option<frontend::FrontendAnalysis> hit;
    ScanCacheMissReason                reason { ScanCacheMissReason::Absent };
};

auto include_kind_name(frontend::IncludeLookupKind kind) -> ref<str> {
    switch (kind) {
    case frontend::IncludeLookupKind::Quoted: return "quoted"_str;
    case frontend::IncludeLookupKind::Angled: return "angled"_str;
    case frontend::IncludeLookupKind::NextQuoted: return "next-quoted"_str;
    case frontend::IncludeLookupKind::NextAngled: return "next-angled"_str;
    }
    __builtin_unreachable();
}

auto parse_include_kind(ref<str> value) -> Option<frontend::IncludeLookupKind> {
    if (value == "quoted"_str) return Some(frontend::IncludeLookupKind::Quoted);
    if (value == "angled"_str) return Some(frontend::IncludeLookupKind::Angled);
    if (value == "next-quoted"_str) return Some(frontend::IncludeLookupKind::NextQuoted);
    if (value == "next-angled"_str) return Some(frontend::IncludeLookupKind::NextAngled);
    return None();
}

auto json_member(ref<Json> value, ref<str> key) -> Option<ref<Json>> {
    auto member = value->get(key);
    return member.is_some() ? Some(*member) : Option<ref<Json>> {};
}

auto json_text(ref<Json> value, ref<str> key) -> Option<ref<str>> {
    auto member = json_member(value, key);
    return member.is_some() ? (**member).as_str() : Option<ref<str>> {};
}

auto json_number(ref<Json> value, ref<str> key) -> Option<u64> {
    auto member = json_member(value, key);
    return member.is_some() ? (**member).as_u64() : Option<u64> {};
}

auto json_array(ref<Json> value, ref<str> key) -> Option<ref<JsonArray>> {
    auto member = json_member(value, key);
    return member.is_some() ? (**member).as_array() : Option<ref<JsonArray>> {};
}

auto json_path(ref<Json> value) -> Option<PathBuf> {
    auto text = value->as_str();
    return text.is_some() ? Some(PathBuf::from(*text)) : Option<PathBuf> {};
}

auto path_json(ref<rstd::path::Path> path) -> Result<Json> {
    auto text = path_string(path);
    if (text.is_err()) return Err(rstd::move(text).unwrap_err());
    return Ok(cache_string(text->as_str()));
}

auto include_lookup_json(const frontend::IncludeLookupDependency& lookup) -> Result<Json> {
    auto including = path_string(lookup.including_path.as_path());
    if (including.is_err()) return Err(rstd::move(including).unwrap_err());
    auto missing = JsonArray::make();
    for (const auto& candidate : lookup.missing_candidates) {
        auto encoded = path_json(candidate.as_path());
        if (encoded.is_err()) return Err(rstd::move(encoded).unwrap_err());
        missing.push(rstd::move(encoded).unwrap());
    }
    auto previous = Json::Null();
    if (lookup.previous_search_index.is_some()) {
        previous = cache_u64(rstd::as_cast<u64>(*lookup.previous_search_index));
    }
    auto resolved = Json::Null();
    if (lookup.resolved.is_some()) {
        auto requested = path_string(lookup.resolved->requested_path.as_path());
        auto canonical = path_string(lookup.resolved->canonical_path.as_path());
        if (requested.is_err()) return Err(rstd::move(requested).unwrap_err());
        if (canonical.is_err()) return Err(rstd::move(canonical).unwrap_err());
        auto value = JsonMap::make();
        value.insert(String::make("canonical"_str), cache_string(canonical->as_str()));
        value.insert(String::make("requested"_str), cache_string(requested->as_str()));
        value.insert(String::make("search-index"_str),
                     cache_u64(rstd::as_cast<u64>(lookup.resolved->search_index)));
        resolved = Json::Object(rstd::move(value));
    }
    auto value = JsonMap::make();
    value.insert(String::make("including"_str), cache_string(including->as_str()));
    value.insert(String::make("kind"_str), cache_string(include_kind_name(lookup.kind)));
    value.insert(String::make("missing"_str), Json::Array(rstd::move(missing)));
    value.insert(String::make("name"_str), cache_string(lookup.name.as_str()));
    value.insert(String::make("previous-search-index"_str), rstd::move(previous));
    value.insert(String::make("resolved"_str), rstd::move(resolved));
    return Ok(Json::Object(rstd::move(value)));
}

auto parse_include_lookup(ref<Json> value) -> Option<frontend::IncludeLookupDependency> {
    if (! value->is_object()) return None();
    auto kind_text      = json_text(value, "kind"_str);
    auto name           = json_text(value, "name"_str);
    auto including      = json_text(value, "including"_str);
    auto missing_values = json_array(value, "missing"_str);
    auto previous_value = json_member(value, "previous-search-index"_str);
    auto resolved_value = json_member(value, "resolved"_str);
    if (kind_text.is_none() || name.is_none() || name->is_empty() || including.is_none() ||
        missing_values.is_none() || previous_value.is_none() || resolved_value.is_none()) {
        return None();
    }
    auto kind = parse_include_kind(*kind_text);
    if (kind.is_none()) return None();
    auto previous = Option<usize> {};
    if (! (**previous_value).is_null()) {
        auto number = (**previous_value).as_u64();
        if (number.is_none()) return None();
        previous = Some(usize(static_cast<rstd::size_t>(number->to_primitive())));
    }
    auto missing = Vec<PathBuf>::with_capacity((**missing_values).len());
    for (const auto& candidate : **missing_values) {
        auto path = json_path(ref<Json>::from_raw_parts(rstd::addressof(candidate)));
        if (path.is_none()) return None();
        missing.push(rstd::move(path).unwrap());
    }
    auto resolved = Option<frontend::ResolvedIncludeCandidate> {};
    if (! (**resolved_value).is_null()) {
        auto requested    = json_text(*resolved_value, "requested"_str);
        auto canonical    = json_text(*resolved_value, "canonical"_str);
        auto search_index = json_number(*resolved_value, "search-index"_str);
        if (requested.is_none() || canonical.is_none() || search_index.is_none()) return None();
        resolved = Some(frontend::ResolvedIncludeCandidate {
            .requested_path = PathBuf::from(*requested),
            .canonical_path = PathBuf::from(*canonical),
            .search_index   = usize(static_cast<rstd::size_t>(search_index->to_primitive())),
        });
    }
    return Some(frontend::IncludeLookupDependency {
        .kind                  = *kind,
        .name                  = String::make(*name),
        .including_path        = PathBuf::from(*including),
        .previous_search_index = previous,
        .missing_candidates    = rstd::move(missing),
        .resolved              = rstd::move(resolved),
    });
}

auto snapshot_json(const frontend::FrontendSnapshot& snapshot) -> Result<Json> {
    auto source = path_string(snapshot.source.as_path());
    if (source.is_err()) return Err(rstd::move(source).unwrap_err());
    auto provided = Json::Null();
    if (snapshot.provided.is_some()) {
        auto value = JsonMap::make();
        value.insert(String::make("interface"_str), Json::Bool(snapshot.provided->is_interface));
        value.insert(String::make("logical-name"_str),
                     cache_string(snapshot.provided->logical_name.as_str()));
        provided = Json::Object(rstd::move(value));
    }
    auto implementation = Json::Null();
    if (snapshot.implementation_module.is_some()) {
        implementation = cache_string(snapshot.implementation_module->as_str());
    }
    auto imports = JsonArray::make();
    for (const auto& imported : snapshot.imports) {
        auto path = path_string(imported.location.path.as_path());
        if (path.is_err()) return Err(rstd::move(path).unwrap_err());
        auto location = JsonMap::make();
        location.insert(String::make("line"_str),
                        cache_u64(rstd::as_cast<u64>(imported.location.line)));
        location.insert(String::make("path"_str), cache_string(path->as_str()));
        auto value = JsonMap::make();
        value.insert(String::make("location"_str), Json::Object(rstd::move(location)));
        value.insert(String::make("logical-name"_str),
                     cache_string(imported.logical_name.as_str()));
        imports.push(Json::Object(rstd::move(value)));
    }
    auto headers = JsonArray::make();
    for (const auto& header : snapshot.header_inputs) {
        auto encoded = path_json(header.as_path());
        if (encoded.is_err()) return Err(rstd::move(encoded).unwrap_err());
        headers.push(rstd::move(encoded).unwrap());
    }
    auto value = JsonMap::make();
    value.insert(String::make("header-inputs"_str), Json::Array(rstd::move(headers)));
    value.insert(String::make("implementation-module"_str), rstd::move(implementation));
    value.insert(String::make("imports"_str), Json::Array(rstd::move(imports)));
    value.insert(String::make("input-bytes"_str),
                 cache_u64(rstd::as_cast<u64>(snapshot.input_bytes)));
    value.insert(String::make("preprocessor-environment"_str),
                 cache_string(snapshot.preprocessor_environment.as_str()));
    value.insert(String::make("provided-module"_str), rstd::move(provided));
    value.insert(String::make("source"_str), cache_string(source->as_str()));
    return Ok(Json::Object(rstd::move(value)));
}

auto parse_snapshot(ref<Json> value) -> Option<frontend::FrontendSnapshot> {
    if (! value->is_object()) return None();
    auto source               = json_text(value, "source"_str);
    auto environment          = json_text(value, "preprocessor-environment"_str);
    auto input_bytes          = json_number(value, "input-bytes"_str);
    auto provided_value       = json_member(value, "provided-module"_str);
    auto implementation_value = json_member(value, "implementation-module"_str);
    auto import_values        = json_array(value, "imports"_str);
    auto header_values        = json_array(value, "header-inputs"_str);
    if (source.is_none() || environment.is_none() || input_bytes.is_none() ||
        provided_value.is_none() || implementation_value.is_none() || import_values.is_none() ||
        header_values.is_none()) {
        return None();
    }
    auto provided = Option<frontend::ProvidedModule> {};
    if (! (**provided_value).is_null()) {
        auto name             = json_text(*provided_value, "logical-name"_str);
        auto interface_member = json_member(*provided_value, "interface"_str);
        if (name.is_none() || interface_member.is_none()) return None();
        auto interface = (**interface_member).as_bool();
        if (interface.is_none()) return None();
        provided = Some(frontend::ProvidedModule {
            .logical_name = String::make(*name),
            .is_interface = *interface,
        });
    }
    auto implementation = Option<String> {};
    if (! (**implementation_value).is_null()) {
        auto text = (**implementation_value).as_str();
        if (text.is_none()) return None();
        implementation = Some(String::make(*text));
    }
    auto imports = Vec<frontend::ModuleImport>::with_capacity((**import_values).len());
    for (const auto& item : **import_values) {
        auto item_ref = ref<Json>::from_raw_parts(rstd::addressof(item));
        auto name     = json_text(item_ref, "logical-name"_str);
        auto location = json_member(item_ref, "location"_str);
        if (name.is_none() || location.is_none()) return None();
        auto path = json_text(*location, "path"_str);
        auto line = json_number(*location, "line"_str);
        if (path.is_none() || line.is_none()) return None();
        imports.push(frontend::ModuleImport {
            .logical_name = String::make(*name),
            .location =
                frontend::DependencyLocation {
                    .path = PathBuf::from(*path),
                    .line = usize(static_cast<rstd::size_t>(line->to_primitive())),
                },
        });
    }
    auto headers = Vec<PathBuf>::with_capacity((**header_values).len());
    for (const auto& item : **header_values) {
        auto path = json_path(ref<Json>::from_raw_parts(rstd::addressof(item)));
        if (path.is_none()) return None();
        headers.push(rstd::move(path).unwrap());
    }
    return Some(frontend::FrontendSnapshot {
        .source                   = PathBuf::from(*source),
        .provided                 = rstd::move(provided),
        .implementation_module    = rstd::move(implementation),
        .imports                  = rstd::move(imports),
        .header_inputs            = rstd::move(headers),
        .preprocessor_environment = String::make(*environment),
        .input_bytes              = usize(static_cast<rstd::size_t>(input_bytes->to_primitive())),
    });
}

class ScanCacheTransaction;

class ScanCacheSession {
    using SharedFingerprintError = rstd::sync::Arc<Error>;
    using FingerprintResult      = rstd::Result<FileFingerprint, SharedFingerprintError>;
    using FingerprintCell        = rstd::sync::OnceLock<FingerprintResult>;
    using SharedFingerprintCell  = rstd::sync::Arc<FingerprintCell>;

    struct FingerprintFields {
        rstd::collections::HashMap<String, SharedFingerprintCell> entries;

        FingerprintFields()
            : entries(rstd::collections::HashMap<String, SharedFingerprintCell>::make()) {}
    };

    struct State {
        String                                 environment;
        bool                                   force_refresh { false };
        rstd::sync::Mutex<FingerprintFields>   fingerprints;
        rstd::sync::Mutex<ScanCacheStatistics> statistics;

        State(String environment, bool force_refresh)
            : environment(rstd::move(environment)),
              force_refresh(force_refresh),
              fingerprints(FingerprintFields {}),
              statistics(ScanCacheStatistics {}) {}
    };

    rstd::sync::Arc<State> state_;

    explicit ScanCacheSession(rstd::sync::Arc<State> state): state_(rstd::move(state)) {}

    static auto clone_error(const SharedFingerprintError& error) -> Error {
        return Error::make(error->kind, error->message.clone());
    }

    static auto clone_fingerprint_result(const FingerprintResult& value)
        -> Result<FileFingerprint> {
        auto borrowed = value.as_ref();
        if (borrowed.is_err()) return Err(clone_error(borrowed.unwrap_err_unchecked()));
        return Ok(borrowed.unwrap_unchecked().clone());
    }

    auto record_miss(ScanCacheMissReason reason) -> void {
        auto statistics = state_->statistics.lock().unwrap_unchecked();
        ++statistics->misses;
        switch (reason) {
        case ScanCacheMissReason::Absent: ++statistics->absent; break;
        case ScanCacheMissReason::Refresh: ++statistics->refresh; break;
        case ScanCacheMissReason::Version: ++statistics->version; break;
        case ScanCacheMissReason::Recipe: ++statistics->recipe; break;
        case ScanCacheMissReason::Corrupt: ++statistics->corrupt; break;
        case ScanCacheMissReason::Environment: ++statistics->environment; break;
        case ScanCacheMissReason::Context: ++statistics->context; break;
        case ScanCacheMissReason::Source: ++statistics->source; break;
        case ScanCacheMissReason::FileDependency: ++statistics->file_dependency; break;
        case ScanCacheMissReason::IncludeLookup: ++statistics->include_lookup; break;
        case ScanCacheMissReason::Receipt: ++statistics->receipt; break;
        case ScanCacheMissReason::None: __builtin_unreachable();
        }
    }

    auto miss(ScanCacheMissReason reason) -> Result<ScanCacheLookup> {
        record_miss(reason);
        return Ok(ScanCacheLookup { .reason = reason });
    }

    auto file_fingerprint(ref<rstd::path::Path> path) -> Result<FileFingerprint> {
        auto text = path_string(path);
        if (text.is_err()) return Err(rstd::move(text).unwrap_err());
        auto key = rstd::move(text).unwrap();
        struct Entry {
            SharedFingerprintCell cell;
            bool                  existing {};
        };
        auto entry = [&] {
            auto fields = state_->fingerprints.lock().unwrap_unchecked();
            auto found  = fields->entries.get(key.as_str());
            if (found.is_some()) return Entry { .cell = (**found).clone(), .existing = true };
            auto created = SharedFingerprintCell::make();
            fields->entries.insert(key.clone(), created.clone());
            return Entry { .cell = rstd::move(created) };
        }();
        auto waiting     = entry.existing && entry.cell->get().is_none();
        auto started     = rstd::time::Instant::now();
        auto initialized = false;
        auto stored      = entry.cell->get_or_init([&]() -> FingerprintResult {
            initialized   = true;
            auto metadata = rstd::fs::metadata(path);
            if (metadata.is_err()) {
                return Err(rstd::sync::Arc<Error>::make(
                    Error::make(ErrorKind::Filesystem,
                                rstd::format("cannot inspect scan cache input '{}': {}",
                                             path,
                                             rstd::move(metadata).unwrap_err()))));
            }
            if (! metadata->is_file()) {
                return Err(rstd::sync::Arc<Error>::make(
                    Error::make(ErrorKind::Filesystem,
                                rstd::format("scan cache input '{}' is not a file", path))));
            }
            auto contents = rstd::fs::read(path);
            if (contents.is_err()) {
                return Err(rstd::sync::Arc<Error>::make(
                    Error::make(ErrorKind::Filesystem,
                                rstd::format("cannot hash scan cache input '{}': {}",
                                             path,
                                             rstd::move(contents).unwrap_err()))));
            }
            auto hash = cache::FNV_OFFSET;
            cache::add_text(hash, "lito-file-content-v1"_str);
            cache::add_bytes(hash, contents->as_slice());
            return Ok(FileFingerprint {
                .path        = PathBuf::from(path),
                .size        = metadata->size(),
                .fingerprint = cache::hex(hash),
            });
        });
        {
            auto statistics = state_->statistics.lock().unwrap_unchecked();
            ++statistics->fingerprint_requests;
            if (entry.existing) ++statistics->fingerprint_hits;
            if (initialized) ++statistics->fingerprint_builds;
            if (waiting && ! initialized) {
                ++statistics->fingerprint_waits;
                statistics->fingerprint_wait =
                    statistics->fingerprint_wait.saturating_add(started.elapsed());
            }
        }
        if (stored->is_err()) {
            auto fields  = state_->fingerprints.lock().unwrap_unchecked();
            auto current = fields->entries.get(key.as_str());
            if (current.is_some() && SharedFingerprintCell::ptr_eq(**current, entry.cell)) {
                (void)fields->entries.remove(key.as_str());
            }
        }
        return clone_fingerprint_result(*stored);
    }

    auto receipt(const ScanCacheInput&                                       input,
                 const FileFingerprint&                                      source,
                 const rstd::collections::BTreeMap<String, FileFingerprint>& files,
                 const Vec<frontend::IncludeLookupDependency>&               lookups,
                 const frontend::FrontendResult& result) const -> Result<String> {
        auto source_path = path_string(input.source.as_path());
        auto relative    = path_string(input.relative_source.as_path());
        auto working     = path_string(input.working_directory.as_path());
        if (source_path.is_err()) return Err(rstd::move(source_path).unwrap_err());
        if (relative.is_err()) return Err(rstd::move(relative).unwrap_err());
        if (working.is_err()) return Err(rstd::move(working).unwrap_err());
        auto hash = cache::FNV_OFFSET;
        cache::add_text(hash, "lito-scan-receipt-v1"_str);
        cache::add_text(hash, state_->environment.as_str());
        cache::add_text(hash, input.target.as_str());
        cache::add_text(hash, input.context_identity.as_str());
        cache::add_text(hash, working->as_str());
        cache::add_text(hash, source_path->as_str());
        cache::add_text(hash, relative->as_str());
        cache::add_text(hash, source.fingerprint.as_str());
        auto iter = files.iter();
        for (auto item = iter.next(); item.is_some(); item = iter.next()) {
            cache::add_text(hash, (*(*item).template get<0>()).as_str());
            cache::add_text(hash, (*(*item).template get<1>()).fingerprint.as_str());
        }
        for (const auto& lookup : lookups) {
            cache::add_text(hash, include_kind_name(lookup.kind));
            cache::add_text(hash, lookup.name.as_str());
            auto including = path_string(lookup.including_path.as_path());
            if (including.is_err()) return Err(rstd::move(including).unwrap_err());
            cache::add_text(hash, including->as_str());
            cache::add_text(hash,
                            lookup.previous_search_index.is_some()
                                ? rstd::format("{}", *lookup.previous_search_index).as_str()
                                : "none"_str);
            for (const auto& candidate : lookup.missing_candidates) {
                auto path = path_string(candidate.as_path());
                if (path.is_err()) return Err(rstd::move(path).unwrap_err());
                cache::add_text(hash, path->as_str());
            }
            if (lookup.resolved.is_some()) {
                auto requested = path_string(lookup.resolved->requested_path.as_path());
                auto canonical = path_string(lookup.resolved->canonical_path.as_path());
                if (requested.is_err()) return Err(rstd::move(requested).unwrap_err());
                if (canonical.is_err()) return Err(rstd::move(canonical).unwrap_err());
                cache::add_text(hash, requested->as_str());
                cache::add_text(hash, canonical->as_str());
                cache::add_text(hash, rstd::format("{}", lookup.resolved->search_index).as_str());
            } else {
                cache::add_text(hash, "unresolved"_str);
            }
        }
        auto encoded = snapshot_json(frontend::snapshot(result));
        if (encoded.is_err()) return Err(rstd::move(encoded).unwrap_err());
        auto text = rstd::json::to_string(*encoded);
        cache::add_text(hash, text.as_str());
        return Ok(cache::hex(hash));
    }

public:
    static auto create(const CacheEnvironment& environment) -> ScanCacheSession {
        return ScanCacheSession { rstd::sync::Arc<State>::make(environment.scan_key_.clone(),
                                                               false) };
    }

    auto clone() const -> ScanCacheSession { return ScanCacheSession { state_.clone() }; }

    auto statistics() const -> ScanCacheStatistics {
        return *state_->statistics.lock().unwrap_unchecked();
    }

    auto begin(ScanCacheInput input) const -> ScanCacheTransaction;

private:
    friend class ScanCacheTransaction;

    auto lookup(const ScanCacheInput& input) -> Result<ScanCacheLookup> {
        if (state_->force_refresh) {
            return miss(ScanCacheMissReason::Refresh);
        }
        auto exists = rstd::fs::exists(input.record.as_path());
        if (exists.is_err()) {
            return cache_failure<ScanCacheLookup>(
                rstd::format("cannot inspect scan cache record '{}': {}",
                             input.record.as_path(),
                             rstd::move(exists).unwrap_err()));
        }
        if (! *exists) return miss(ScanCacheMissReason::Absent);
        auto contents = rstd::fs::read_to_string(input.record.as_path());
        if (contents.is_err()) {
            return cache_failure<ScanCacheLookup>(
                rstd::format("cannot read scan cache record '{}': {}",
                             input.record.as_path(),
                             rstd::move(contents).unwrap_err()));
        }
        auto parsed = rstd::json::from_str(contents->as_str());
        if (parsed.is_err() || ! parsed->is_object()) {
            return miss(ScanCacheMissReason::Corrupt);
        }
        auto document    = ref<Json>::from_raw_parts(rstd::addressof(*parsed));
        auto source_path = path_string(input.source.as_path());
        auto relative    = path_string(input.relative_source.as_path());
        auto working     = path_string(input.working_directory.as_path());
        if (source_path.is_err()) return Err(rstd::move(source_path).unwrap_err());
        if (relative.is_err()) return Err(rstd::move(relative).unwrap_err());
        if (working.is_err()) return Err(rstd::move(working).unwrap_err());
        auto version        = json_number(document, "version"_str);
        auto state          = json_text(document, "state"_str);
        auto recipe         = json_text(document, "recipe"_str);
        auto environment    = json_text(document, "environment"_str);
        auto target         = json_text(document, "target"_str);
        auto context        = json_text(document, "context"_str);
        auto stored_working = json_text(document, "working-directory"_str);
        auto stored_source  = json_member(document, "source"_str);
        auto files_value    = json_array(document, "files"_str);
        auto lookups_value  = json_array(document, "include-lookups"_str);
        auto result_value   = json_member(document, "result"_str);
        auto stored_receipt = json_text(document, "receipt"_str);
        if (version.is_none() || state.is_none() || *state != "complete"_str || recipe.is_none() ||
            environment.is_none() || target.is_none() || context.is_none() ||
            stored_working.is_none() || stored_source.is_none() || files_value.is_none() ||
            lookups_value.is_none() || result_value.is_none() || stored_receipt.is_none()) {
            return miss(ScanCacheMissReason::Corrupt);
        }
        if (*version != CACHE_VERSION) return miss(ScanCacheMissReason::Version);
        if (*recipe != SCAN_RECIPE) return miss(ScanCacheMissReason::Recipe);
        if (*environment != state_->environment.as_str()) {
            return miss(ScanCacheMissReason::Environment);
        }
        if (*target != input.target.as_str() || *context != input.context_identity.as_str() ||
            *stored_working != working->as_str()) {
            return miss(ScanCacheMissReason::Context);
        }
        auto stored_source_path        = json_text(*stored_source, "path"_str);
        auto stored_relative           = json_text(*stored_source, "relative"_str);
        auto stored_source_fingerprint = json_text(*stored_source, "fingerprint"_str);
        auto stored_source_size        = json_number(*stored_source, "size"_str);
        if (stored_source_path.is_none() || stored_relative.is_none() ||
            stored_source_fingerprint.is_none() || stored_source_size.is_none() ||
            *stored_source_path != source_path->as_str() ||
            *stored_relative != relative->as_str()) {
            return miss(ScanCacheMissReason::Source);
        }
        auto source_file = file_fingerprint(input.source.as_path());
        if (source_file.is_err()) return Err(rstd::move(source_file).unwrap_err());
        if (source_file->size != *stored_source_size ||
            source_file->fingerprint.as_str() != *stored_source_fingerprint) {
            return miss(ScanCacheMissReason::Source);
        }
        auto files = rstd::collections::BTreeMap<String, FileFingerprint>::make();
        for (const auto& item : **files_value) {
            auto item_ref    = ref<Json>::from_raw_parts(rstd::addressof(item));
            auto path        = json_text(item_ref, "path"_str);
            auto size        = json_number(item_ref, "size"_str);
            auto fingerprint = json_text(item_ref, "fingerprint"_str);
            if (path.is_none() || size.is_none() || fingerprint.is_none() ||
                files.contains_key(*path)) {
                return miss(ScanCacheMissReason::Corrupt);
            }
            auto exists = rstd::fs::exists(PathBuf::from(*path).as_path());
            if (exists.is_err()) {
                return cache_failure<ScanCacheLookup>(
                    rstd::format("cannot inspect scan cache input '{}': {}",
                                 *path,
                                 rstd::move(exists).unwrap_err()));
            }
            if (! *exists) return miss(ScanCacheMissReason::FileDependency);
            auto metadata = rstd::fs::metadata(PathBuf::from(*path).as_path());
            if (metadata.is_err()) {
                return cache_failure<ScanCacheLookup>(
                    rstd::format("cannot inspect scan cache input '{}': {}",
                                 *path,
                                 rstd::move(metadata).unwrap_err()));
            }
            if (! metadata->is_file()) return miss(ScanCacheMissReason::FileDependency);
            auto current = file_fingerprint(PathBuf::from(*path).as_path());
            if (current.is_err()) return Err(rstd::move(current).unwrap_err());
            if (current->size != *size || current->fingerprint.as_str() != *fingerprint) {
                return miss(ScanCacheMissReason::FileDependency);
            }
            files.insert(String::make(*path), rstd::move(current).unwrap());
        }
        auto lookups =
            Vec<frontend::IncludeLookupDependency>::with_capacity((**lookups_value).len());
        for (const auto& item : **lookups_value) {
            auto lookup = parse_include_lookup(ref<Json>::from_raw_parts(rstd::addressof(item)));
            if (lookup.is_none()) {
                return miss(ScanCacheMissReason::Corrupt);
            }
            auto valid = frontend::validate(*lookup);
            if (valid.is_err()) {
                return cache_failure<ScanCacheLookup>(rstd::move(valid).unwrap_err());
            }
            if (! *valid) {
                return miss(ScanCacheMissReason::IncludeLookup);
            }
            lookups.push(rstd::move(lookup).unwrap());
        }
        auto stored_snapshot = parse_snapshot(*result_value);
        if (stored_snapshot.is_none()) {
            return miss(ScanCacheMissReason::Corrupt);
        }
        auto restored = frontend::restore(rstd::move(stored_snapshot).unwrap());
        if (restored.is_none() || restored->source.as_path() != input.source.as_path() ||
            restored->preprocessor_environment.as_str() !=
                input.preprocessor_environment.as_str()) {
            return miss(ScanCacheMissReason::Environment);
        }
        auto header_paths = rstd::collections::BTreeMap<String, empty>::make();
        for (const auto& header : restored->header_inputs) {
            auto path = path_string(header.as_path());
            if (path.is_err()) return Err(rstd::move(path).unwrap_err());
            if (header_paths.contains_key(path->as_str()) || ! files.contains_key(path->as_str())) {
                return miss(ScanCacheMissReason::Corrupt);
            }
            header_paths.insert(rstd::move(path).unwrap(), empty {});
        }
        if (header_paths.len() != files.len()) {
            return miss(ScanCacheMissReason::Corrupt);
        }
        auto expected_receipt = receipt(input, *source_file, files, lookups, *restored);
        if (expected_receipt.is_err()) return Err(rstd::move(expected_receipt).unwrap_err());
        if (expected_receipt->as_str() != *stored_receipt) {
            return miss(ScanCacheMissReason::Receipt);
        }
        {
            auto statistics = state_->statistics.lock().unwrap_unchecked();
            ++statistics->hits;
        }
        return Ok(ScanCacheLookup {
            .hit    = Some(frontend::FrontendAnalysis {
                .result           = rstd::move(restored).unwrap(),
                .context_identity = input.context_identity.clone(),
                .receipt          = rstd::move(expected_receipt).unwrap(),
                .origin           = frontend::FrontendAnalysisOrigin::PersistentCache,
            }),
            .reason = ScanCacheMissReason::None,
        });
    }

    auto publish(const ScanCacheInput& input, frontend::UncachedFrontendAnalysis value)
        -> Result<frontend::FrontendAnalysis> {
        auto source_file = file_fingerprint(input.source.as_path());
        if (source_file.is_err()) return Err(rstd::move(source_file).unwrap_err());
        auto files = rstd::collections::BTreeMap<String, FileFingerprint>::make();
        for (const auto& header : value.result.header_inputs) {
            auto path = path_string(header.as_path());
            if (path.is_err()) return Err(rstd::move(path).unwrap_err());
            if (files.contains_key(path->as_str())) continue;
            auto file = file_fingerprint(header.as_path());
            if (file.is_err()) return Err(rstd::move(file).unwrap_err());
            files.insert(rstd::move(path).unwrap(), rstd::move(file).unwrap());
        }
        auto scan_receipt =
            receipt(input, *source_file, files, value.include_lookups, value.result);
        if (scan_receipt.is_err()) return Err(rstd::move(scan_receipt).unwrap_err());
        auto cacheable = value.result.preprocessor_environment.as_str() ==
                         input.preprocessor_environment.as_str();
        if (cacheable) {
            auto source_path = path_string(input.source.as_path());
            auto relative    = path_string(input.relative_source.as_path());
            auto working     = path_string(input.working_directory.as_path());
            if (source_path.is_err()) return Err(rstd::move(source_path).unwrap_err());
            if (relative.is_err()) return Err(rstd::move(relative).unwrap_err());
            if (working.is_err()) return Err(rstd::move(working).unwrap_err());
            auto files_json = JsonArray::make();
            auto iter       = files.iter();
            for (auto item = iter.next(); item.is_some(); item = iter.next()) {
                auto encoded = file_json(*(*item).template get<1>());
                if (encoded.is_err()) return Err(rstd::move(encoded).unwrap_err());
                files_json.push(rstd::move(encoded).unwrap());
            }
            auto lookups_json = JsonArray::make();
            for (const auto& lookup : value.include_lookups) {
                auto encoded = include_lookup_json(lookup);
                if (encoded.is_err()) return Err(rstd::move(encoded).unwrap_err());
                lookups_json.push(rstd::move(encoded).unwrap());
            }
            auto encoded_result = snapshot_json(frontend::snapshot(value.result));
            if (encoded_result.is_err()) return Err(rstd::move(encoded_result).unwrap_err());
            auto source_json = JsonMap::make();
            source_json.insert(String::make("fingerprint"_str),
                               cache_string(source_file->fingerprint.as_str()));
            source_json.insert(String::make("path"_str), cache_string(source_path->as_str()));
            source_json.insert(String::make("relative"_str), cache_string(relative->as_str()));
            source_json.insert(String::make("size"_str), cache_u64(source_file->size));
            auto root = JsonMap::make();
            root.insert(String::make("context"_str), cache_string(input.context_identity.as_str()));
            root.insert(String::make("environment"_str),
                        cache_string(state_->environment.as_str()));
            root.insert(String::make("files"_str), Json::Array(rstd::move(files_json)));
            root.insert(String::make("include-lookups"_str), Json::Array(rstd::move(lookups_json)));
            root.insert(String::make("receipt"_str), cache_string(scan_receipt->as_str()));
            root.insert(String::make("recipe"_str), cache_string(SCAN_RECIPE));
            root.insert(String::make("result"_str), rstd::move(encoded_result).unwrap());
            root.insert(String::make("source"_str), Json::Object(rstd::move(source_json)));
            root.insert(String::make("state"_str), cache_string("complete"_str));
            root.insert(String::make("target"_str), cache_string(input.target.as_str()));
            root.insert(String::make("version"_str), cache_u64(CACHE_VERSION));
            root.insert(String::make("working-directory"_str), cache_string(working->as_str()));
            auto written = write_json(input.record.as_path(), Json::Object(rstd::move(root)));
            if (written.is_err()) return Err(rstd::move(written).unwrap_err());
        } else {
            auto statistics = state_->statistics.lock().unwrap_unchecked();
            ++statistics->uncacheable;
        }
        return Ok(frontend::FrontendAnalysis {
            .result           = rstd::move(value.result),
            .context_identity = input.context_identity.clone(),
            .receipt          = rstd::move(scan_receipt).unwrap(),
            .origin           = cacheable ? frontend::FrontendAnalysisOrigin::Native
                                          : frontend::FrontendAnalysisOrigin::Uncacheable,
        });
    }
};

class ScanCacheTransaction {
    ScanCacheSession session_;
    ScanCacheInput   input_;

    ScanCacheTransaction(ScanCacheSession session, ScanCacheInput input)
        : session_(rstd::move(session)), input_(rstd::move(input)) {}

    friend class ScanCacheSession;

public:
    ScanCacheTransaction(ScanCacheTransaction&&) noexcept                    = default;
    auto operator=(ScanCacheTransaction&&) noexcept -> ScanCacheTransaction& = default;

    auto lookup() -> Result<ScanCacheLookup> { return session_.lookup(input_); }

    auto publish(frontend::UncachedFrontendAnalysis value) -> Result<frontend::FrontendAnalysis> {
        return session_.publish(input_, rstd::move(value));
    }
};

auto ScanCacheSession::begin(ScanCacheInput input) const -> ScanCacheTransaction {
    return ScanCacheTransaction(clone(), rstd::move(input));
}

class CacheDecision {
    bool         current_ { false };
    String       artifact_;
    PathBuf      record_;
    Json         building_;
    Json         complete_;
    Vec<PathBuf> stale_outputs_;

    CacheDecision(bool         current,
                  String       artifact,
                  PathBuf      record,
                  Json         building,
                  Json         complete,
                  Vec<PathBuf> stale_outputs)
        : current_(current),
          artifact_(rstd::move(artifact)),
          record_(rstd::move(record)),
          building_(rstd::move(building)),
          complete_(rstd::move(complete)),
          stale_outputs_(rstd::move(stale_outputs)) {}

    friend class CompileCacheSession;

public:
    CacheDecision(CacheDecision&&) noexcept                    = default;
    auto operator=(CacheDecision&&) noexcept -> CacheDecision& = default;

    auto current() const noexcept -> bool { return current_; }
    auto artifact() const -> ref<str> { return artifact_.as_str(); }
    auto record() const -> ref<rstd::path::Path> { return record_.as_path(); }
};

class CompileCacheSession {
    String  environment_;
    PathBuf owner_root_;
    bool    force_refresh_ { false };

    auto record_current(const PreparedUnit& unit, const Json& complete) const -> Result<bool> {
        if (force_refresh_) return Ok(false);
        auto exists = rstd::fs::exists(unit.unit.cache_record.as_path());
        if (exists.is_err()) {
            return cache_failure<bool>(rstd::format("cannot inspect cache record '{}': {}",
                                                    unit.unit.cache_record.as_path(),
                                                    rstd::move(exists).unwrap_err()));
        }
        if (! *exists) return Ok(false);
        auto contents = rstd::fs::read_to_string(unit.unit.cache_record.as_path());
        if (contents.is_err()) {
            return cache_failure<bool>(rstd::format("cannot read cache record '{}': {}",
                                                    unit.unit.cache_record.as_path(),
                                                    rstd::move(contents).unwrap_err()));
        }
        auto parsed = rstd::json::from_str(contents->as_str());
        if (parsed.is_err()) return Ok(false);
        auto comparable        = parsed->clone();
        auto comparable_object = comparable.as_object_mut();
        if (comparable_object.is_none()) return Ok(false);
        (**comparable_object).remove("content-digests"_str);
        if (comparable != complete) return Ok(false);
        auto stored_digests = parsed->get("content-digests"_str);
        if (stored_digests.is_none()) return Ok(false);
        auto stored_object_digest = json_text(*stored_digests, "object"_str);
        if (stored_object_digest.is_none()) return Ok(false);
        auto object = output_exists(unit.unit.object.as_path());
        if (object.is_err()) return object;
        if (! *object) return Ok(false);
        auto object_digest = output_content_digest(unit.unit.object.as_path());
        if (object_digest.is_err()) return Err(rstd::move(object_digest).unwrap_err());
        if (object_digest->as_str() != *stored_object_digest) return Ok(false);
        if (unit.unit.bmi.is_some()) {
            auto bmi = output_exists(unit.unit.bmi->path.as_path());
            if (bmi.is_err()) return bmi;
            if (! *bmi) return Ok(false);
            auto stored_bmi_digest = json_text(*stored_digests, "bmi"_str);
            if (stored_bmi_digest.is_none()) return Ok(false);
            auto bmi_digest = output_content_digest(unit.unit.bmi->path.as_path());
            if (bmi_digest.is_err()) return Err(rstd::move(bmi_digest).unwrap_err());
            if (bmi_digest->as_str() != *stored_bmi_digest) return Ok(false);
        }
        return Ok(true);
    }

public:
    static auto create(const CacheEnvironment& environment, ref<rstd::path::Path> owner_root)
        -> CompileCacheSession {
        auto session           = CompileCacheSession {};
        session.environment_   = environment.key_.clone();
        session.owner_root_    = PathBuf::from(owner_root);
        session.force_refresh_ = environment.force_refresh_;
        return session;
    }

    auto evaluate(ref<str>                       target,
                  const PreparedUnit&            unit,
                  ref<str>                       scan_receipt,
                  const CompileInvocation&       invocation,
                  const Vec<DependencyArtifact>& dependencies) -> Result<CacheDecision> {
        auto source   = path_string(unit.unit.source.as_path());
        auto relative = path_string(unit.unit.relative_source.as_path());
        auto object   = path_string(unit.unit.object.as_path());
        if (source.is_err()) return Err(rstd::move(source).unwrap_err());
        if (relative.is_err()) return Err(rstd::move(relative).unwrap_err());
        if (object.is_err()) return Err(rstd::move(object).unwrap_err());

        auto context_key =
            cache::text_identity("lito-context-key-v1"_str, unit.unit.context->id.as_str());
        auto command_key =
            cache::text_identity("lito-command-key-v1"_str, invocation.identity.as_str());
        auto artifact_hash = cache::FNV_OFFSET;
        cache::add_text(artifact_hash, "lito-artifact-v2"_str);
        cache::add_text(artifact_hash, environment_.as_str());
        cache::add_text(artifact_hash, context_key.as_str());
        cache::add_text(artifact_hash, command_key.as_str());
        cache::add_text(artifact_hash, scan_receipt);

        auto direct = JsonArray::make();
        for (const auto& dependency : dependencies) {
            auto value = JsonMap::make();
            value.insert(String::make("artifact"_str), cache_string(dependency.artifact.as_str()));
            value.insert(String::make("logical-name"_str),
                         cache_string(dependency.logical_name.as_str()));
            direct.push(Json::Object(rstd::move(value)));
            cache::add_text(artifact_hash, dependency.logical_name.as_str());
            cache::add_text(artifact_hash, dependency.artifact.as_str());
        }
        auto artifact = cache::hex(artifact_hash);

        auto outputs  = JsonMap::make();
        auto bmi_json = Json::Null();
        if (unit.unit.bmi.is_some()) {
            auto bmi = path_string(unit.unit.bmi->path.as_path());
            if (bmi.is_err()) return Err(rstd::move(bmi).unwrap_err());
            auto bmi_output = JsonMap::make();
            bmi_output.insert(String::make("format"_str),
                              cache_string(bmi_format_identity(unit.unit.bmi->format).as_str()));
            bmi_output.insert(String::make("kind"_str), cache_string("bmi"_str));
            bmi_output.insert(String::make("path"_str), cache_string(bmi->as_str()));
            bmi_output.insert(String::make("recipe"_str),
                              cache_string(unit.unit.bmi->key.value.as_str()));
            bmi_output.insert(
                String::make("representation"_str),
                cache_string(bmi_representation_name(unit.unit.bmi->request.representation)));
            bmi_output.insert(
                String::make("source-embedding"_str),
                cache_string(bmi_source_embedding_name(unit.unit.bmi->request.source_embedding)));
            bmi_json = Json::Object(rstd::move(bmi_output));
        }
        outputs.insert(String::make("bmi"_str), rstd::move(bmi_json));
        auto object_output = JsonMap::make();
        object_output.insert(String::make("kind"_str), cache_string("object"_str));
        object_output.insert(String::make("path"_str), cache_string(object->as_str()));
        object_output.insert(String::make("recipe"_str), cache_string(command_key.as_str()));
        outputs.insert(String::make("object"_str), Json::Object(rstd::move(object_output)));

        auto complete = JsonMap::make();
        complete.insert(String::make("artifact"_str), cache_string(artifact.as_str()));
        complete.insert(String::make("command"_str), cache_string(command_key.as_str()));
        complete.insert(String::make("context"_str), cache_string(context_key.as_str()));
        complete.insert(String::make("direct-modules"_str), Json::Array(rstd::move(direct)));
        complete.insert(String::make("environment"_str), cache_string(environment_.as_str()));
        complete.insert(String::make("outputs"_str), Json::Object(rstd::move(outputs)));
        complete.insert(String::make("scan-receipt"_str), cache_string(scan_receipt));
        complete.insert(String::make("source"_str), cache_string(relative->as_str()));
        complete.insert(String::make("source-path"_str), cache_string(source->as_str()));
        complete.insert(String::make("state"_str), cache_string("complete"_str));
        complete.insert(String::make("target"_str), cache_string(target));
        complete.insert(String::make("version"_str), cache_u64(CACHE_VERSION));
        auto complete_json = Json::Object(rstd::move(complete));

        auto building = JsonMap::make();
        building.insert(String::make("artifact"_str), cache_string(artifact.as_str()));
        building.insert(String::make("command"_str), cache_string(command_key.as_str()));
        building.insert(String::make("environment"_str), cache_string(environment_.as_str()));
        building.insert(String::make("source"_str), cache_string(relative->as_str()));
        building.insert(String::make("state"_str), cache_string("building"_str));
        building.insert(String::make("target"_str), cache_string(target));
        building.insert(String::make("version"_str), cache_u64(CACHE_VERSION));
        auto building_json = Json::Object(rstd::move(building));

        auto previous_outputs = read_receipt_output_paths(unit.unit.cache_record.as_path());
        if (previous_outputs.is_err()) return Err(rstd::move(previous_outputs).unwrap_err());
        auto stale_outputs = Vec<PathBuf>::make();
        for (auto& path : *previous_outputs) {
            auto current_output = path.as_path() == unit.unit.object.as_path();
            if (unit.unit.bmi.is_some()) {
                current_output = current_output || path.as_path() == unit.unit.bmi->path.as_path();
            }
            if (! current_output) stale_outputs.push(rstd::move(path));
        }

        auto current = record_current(unit, complete_json);
        if (current.is_err()) return Err(rstd::move(current).unwrap_err());
        return Ok(CacheDecision { *current,
                                  rstd::move(artifact),
                                  unit.unit.cache_record.clone(),
                                  rstd::move(building_json),
                                  rstd::move(complete_json),
                                  rstd::move(stale_outputs) });
    }

    auto begin_compile(const CacheDecision& decision) -> Result<empty> {
        return write_json(decision.record_.as_path(), decision.building_);
    }

    auto begin_compile_test(const CacheDecision&   decision,
                            ref<rstd::path::Path>  record,
                            const CompileTestCase& test) -> Result<empty> {
        auto root = JsonMap::make();
        root.insert(String::make("case"_str), cache_string(test.name.as_str()));
        root.insert(String::make("compile"_str), decision.complete_.clone());
        root.insert(String::make("expected"_str),
                    cache_string(test.outcome == CompileTestOutcome::Success ? "success"_str
                                                                             : "failure"_str));
        root.insert(String::make("state"_str), cache_string("running"_str));
        root.insert(String::make("version"_str), cache_u64(CACHE_VERSION));
        return write_json(record, Json::Object(rstd::move(root)));
    }

    auto record_compile_test(const CacheDecision&        decision,
                             ref<rstd::path::Path>       record,
                             const CompileTestExecution& execution) -> Result<empty> {
        auto mismatch = Json::Null();
        if (execution.mismatch.is_some()) {
            mismatch = cache_string(execution.mismatch->as_str());
        }
        auto result = JsonMap::make();
        result.insert(String::make("exit-code"_str),
                      cache_i64(rstd::as_cast<i64>(execution.exit_code)));
        result.insert(String::make("matched"_str), Json::Bool(execution.success()));
        result.insert(String::make("mismatch"_str), rstd::move(mismatch));
        result.insert(String::make("stderr-bytes"_str),
                      cache_u64(rstd::as_cast<u64>(execution.standard_error.len())));
        result.insert(String::make("stderr-fingerprint"_str),
                      cache_string(cache::text_identity("lito-compile-test-stderr-v1"_str,
                                                        execution.standard_error.as_str())
                                       .as_str()));
        result.insert(String::make("stdout-bytes"_str),
                      cache_u64(rstd::as_cast<u64>(execution.standard_output.len())));

        auto root = JsonMap::make();
        root.insert(String::make("case"_str), cache_string(execution.name.as_str()));
        root.insert(String::make("compile"_str), decision.complete_.clone());
        root.insert(String::make("expected"_str),
                    cache_string(execution.expected == CompileTestOutcome::Success
                                     ? "success"_str
                                     : "failure"_str));
        root.insert(String::make("result"_str), Json::Object(rstd::move(result)));
        root.insert(String::make("state"_str), cache_string("complete"_str));
        root.insert(String::make("version"_str), cache_u64(CACHE_VERSION));
        return write_json(record, Json::Object(rstd::move(root)));
    }

    auto commit_success(const PreparedUnit& unit, const CacheDecision& decision) -> Result<empty> {
        auto object = output_exists(unit.unit.object.as_path());
        if (object.is_err()) return Err(rstd::move(object).unwrap_err());
        if (! *object) {
            return cache_failure<empty>(
                rstd::format("compiler did not produce object '{}'", unit.unit.object.as_path()));
        }
        auto digests       = JsonMap::make();
        auto object_digest = output_content_digest(unit.unit.object.as_path());
        if (object_digest.is_err()) return Err(rstd::move(object_digest).unwrap_err());
        digests.insert(String::make("object"_str), cache_string(object_digest->as_str()));
        if (unit.unit.bmi.is_some()) {
            auto bmi = output_exists(unit.unit.bmi->path.as_path());
            if (bmi.is_err()) return Err(rstd::move(bmi).unwrap_err());
            if (! *bmi) {
                return cache_failure<empty>(rstd::format("compiler did not produce BMI '{}'",
                                                         unit.unit.bmi->path.as_path()));
            }
            auto bmi_digest = output_content_digest(unit.unit.bmi->path.as_path());
            if (bmi_digest.is_err()) return Err(rstd::move(bmi_digest).unwrap_err());
            digests.insert(String::make("bmi"_str), cache_string(bmi_digest->as_str()));
        }
        auto complete        = decision.complete_.clone();
        auto complete_object = complete.as_object_mut();
        if (complete_object.is_none()) {
            return cache_failure<empty>(String::make("compile cache receipt is not an object"_str));
        }
        (**complete_object)
            .insert(String::make("content-digests"_str), Json::Object(rstd::move(digests)));
        for (const auto& output : decision.stale_outputs_) {
            auto removed = remove_owned_output(output.as_path(), owner_root_.as_path());
            if (removed.is_err()) return removed;
        }
        return write_json(decision.record_.as_path(), complete);
    }

    auto finish_target(const BuildLayout&     layout,
                       const PackageTargetId& target,
                       const Vec<PathBuf>&    current_records) -> Result<empty> {
        auto directory = layout.cache_target_directory(target);
        return finish_directory(directory.as_path(), current_records);
    }

    auto finish_directory(ref<rstd::path::Path> directory, const Vec<PathBuf>& current_records)
        -> Result<empty> {
        auto current = rstd::collections::BTreeMap<String, empty>::make();
        for (const auto& record : current_records) {
            auto path = path_string(record.as_path());
            if (path.is_err()) return Err(rstd::move(path).unwrap_err());
            current.insert(rstd::move(path).unwrap(), empty {});
        }
        return collect_stale_records(directory, current, owner_root_.as_path());
    }
};

} // namespace lito
