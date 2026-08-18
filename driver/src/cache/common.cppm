module lito.driver:cache.common;

import rstd;
import rstd.json;
import lito.core;
import lito.cpp;
import lito.toolchain.common;
import lito.frontend;
import :build.layout;
import :cache.hash;
import :cache.error;

using namespace rstd::prelude;
using namespace rstd::literals;
using Json      = rstd::json::Value;
using JsonMap   = rstd::json::Map;
using JsonArray = rstd::json::Array;

namespace lito
{

inline constexpr auto CACHE_VERSION  = u64(5);
inline constexpr auto SCAN_RECIPE    = "lito-native-frontend-v9"_str;
inline constexpr auto COMPILE_RECIPE = "clang-compile-v6"_str;

template<typename T>
auto cache_failure(String message) -> CacheResult<T> {
    return Err(CacheError::Record(rstd::move(message)));
}

template<typename T>
auto cache_io_failure(ref<str> operation, ref<rstd::path::Path> path, rstd::io::error::Error source)
    -> CacheResult<T> {
    return Err(CacheError::Io(String::make(operation), PathBuf::from(path), rstd::move(source)));
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

auto path_string(ref<rstd::path::Path> path) -> CacheResult<String> {
    auto value = path.to_str();
    if (value.is_none()) {
        return cache_failure<String>(rstd::format("cache path '{}' is not valid UTF-8", path));
    }
    return Ok(String::make(*value));
}

auto write_json(ref<rstd::path::Path> path, const Json& document) -> CacheResult<empty> {
    auto parent = path.parent();
    if (parent.is_none()) {
        return cache_failure<empty>(rstd::format("cache path '{}' has no parent", path));
    }
    auto created = rstd::fs::create_dir_all(*parent);
    if (created.is_err()) {
        return cache_io_failure<empty>(
            "create directory"_str, *parent, rstd::move(created).unwrap_err());
    }
    auto text = rstd::json::to_string(
        document, rstd::json::FormatOptions { .pretty = true, .indent = usize(2) });
    text.push_ascii('\n');
    auto written = rstd::fs::write_atomic(path, text.as_str().as_bytes());
    if (written.is_err()) {
        return cache_io_failure<empty>("write record"_str, path, rstd::move(written).unwrap_err());
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

auto file_json(const FileFingerprint& file) -> CacheResult<Json> {
    auto path = path_string(file.path.as_path());
    if (path.is_err()) return Err(rstd::move(path).unwrap_err());
    auto object = JsonMap::make();
    object.insert(String::make("fingerprint"_str), cache_string(file.fingerprint.as_str()));
    object.insert(String::make("path"_str), cache_string(path->as_str()));
    object.insert(String::make("size"_str), cache_u64(file.size));
    return Ok(Json::Object(rstd::move(object)));
}

auto output_exists(ref<rstd::path::Path> path) -> CacheResult<bool> {
    auto exists = rstd::fs::exists(path);
    if (exists.is_err()) {
        return cache_io_failure<bool>("inspect output"_str, path, rstd::move(exists).unwrap_err());
    }
    return Ok(*exists);
}

auto output_content_digest(ref<rstd::path::Path> path) -> CacheResult<String> {
    auto contents = rstd::fs::read(path);
    if (contents.is_err()) {
        return cache_io_failure<String>(
            "read compiler output"_str, path, rstd::move(contents).unwrap_err());
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

auto read_receipt_output_paths(ref<rstd::path::Path> path) -> CacheResult<Vec<PathBuf>> {
    auto exists = rstd::fs::exists(path);
    if (exists.is_err()) {
        return cache_io_failure<Vec<PathBuf>>("inspect record"_str, path, exists.unwrap_err());
    }
    if (! *exists) return Ok(Vec<PathBuf>::make());
    auto contents = rstd::fs::read_to_string(path);
    if (contents.is_err()) {
        return cache_io_failure<Vec<PathBuf>>("read record"_str, path, contents.unwrap_err());
    }
    auto parsed = rstd::json::from_str(contents->as_str());
    if (parsed.is_err()) return Ok(Vec<PathBuf>::make());
    return Ok(receipt_output_paths(*parsed));
}

auto remove_owned_output(ref<rstd::path::Path> path, ref<rstd::path::Path> owner_root)
    -> CacheResult<empty> {
    if (path.strip_prefix(owner_root).is_none()) {
        return cache_failure<empty>(
            rstd::format("cache output '{}' is outside build root '{}'", path, owner_root));
    }
    auto exists = rstd::fs::exists(path);
    if (exists.is_err()) {
        return cache_io_failure<empty>("inspect output"_str, path, exists.unwrap_err());
    }
    if (! *exists) return Ok(empty {});
    auto removed = rstd::fs::remove_file(path);
    if (removed.is_err()) {
        return cache_io_failure<empty>("remove output"_str, path, removed.unwrap_err());
    }
    return Ok(empty {});
}

auto collect_stale_records(ref<rstd::path::Path>                             directory,
                           const rstd::collections::BTreeMap<String, empty>& current,
                           ref<rstd::path::Path> owner_root) -> CacheResult<empty> {
    auto exists = rstd::fs::exists(directory);
    if (exists.is_err()) {
        return cache_io_failure<empty>(
            "inspect directory"_str, directory, rstd::move(exists).unwrap_err());
    }
    if (! *exists) return Ok(empty {});
    auto opened = rstd::fs::read_dir(directory);
    if (opened.is_err()) {
        return cache_io_failure<empty>(
            "enumerate directory"_str, directory, rstd::move(opened).unwrap_err());
    }
    auto entries = rstd::move(opened).unwrap();
    for (auto next = entries.next(); next.is_some(); next = entries.next()) {
        auto item = rstd::move(next).unwrap();
        if (item.is_err()) {
            return cache_io_failure<empty>(
                "enumerate directory"_str, directory, rstd::move(item).unwrap_err());
        }
        auto entry = rstd::move(item).unwrap();
        auto type  = entry.file_type();
        if (type.is_err()) {
            auto path = entry.path();
            return cache_io_failure<empty>(
                "inspect entry"_str, path.as_path(), rstd::move(type).unwrap_err());
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
            return cache_io_failure<empty>(
                "remove stale record"_str, path.as_path(), rstd::move(removed).unwrap_err());
        }
    }
    return Ok(empty {});
}

} // namespace lito

namespace lito
{

struct DependencyArtifact {
    String  logical_name;
    String  artifact;
    PathBuf path;
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
                       const CompilerIdentity& compiler) -> CacheResult<CacheEnvironment> {
        auto owner           = path_string(owner_root);
        auto compiler_path   = path_string(compiler.path.as_path());
        auto c_compiler_path = path_string(compiler.c_path.as_path());
        auto resource        = path_string(compiler.resource_directory.as_path());
        if (owner.is_err()) return Err(rstd::move(owner).unwrap_err());
        if (compiler_path.is_err()) return Err(rstd::move(compiler_path).unwrap_err());
        if (c_compiler_path.is_err()) return Err(rstd::move(c_compiler_path).unwrap_err());
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
        identity.push_str(c_compiler_path->as_str());
        identity.push_ascii('\n');
        identity.push_str(compiler.c_version.as_str());
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
        identity.push_ascii('\n');
        identity.push_str(rstd::format("{}\n{}\n{}",
                                       compiler.c_size,
                                       compiler.c_modified_seconds,
                                       compiler.c_modified_nanoseconds)
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
        scan_identity.push_str(c_compiler_path->as_str());
        scan_identity.push_ascii('\n');
        scan_identity.push_str(compiler.c_version.as_str());
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
        scan_identity.push_ascii('\n');
        scan_identity.push_str(rstd::format("{}\n{}\n{}",
                                            compiler.c_size,
                                            compiler.c_modified_seconds,
                                            compiler.c_modified_nanoseconds)
                                   .as_str());
        auto scan_key =
            cache::text_identity("lito-scan-cache-environment-key-v1"_str, scan_identity.as_str());

        auto compiler_json = JsonMap::make();
        compiler_json.insert(String::make("modified-nanoseconds"_str),
                             cache_u64(as_cast<u64>(compiler.modified_nanoseconds)));
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
        compiler_json.insert(String::make("c-path"_str), cache_string(c_compiler_path->as_str()));
        compiler_json.insert(String::make("c-version"_str),
                             cache_string(compiler.c_version.as_str()));
        compiler_json.insert(String::make("c-size"_str), cache_u64(compiler.c_size));
        compiler_json.insert(String::make("c-modified-seconds"_str),
                             cache_i64(compiler.c_modified_seconds));
        compiler_json.insert(String::make("c-modified-nanoseconds"_str),
                             cache_u64(as_cast<u64>(compiler.c_modified_nanoseconds)));

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
            return cache_io_failure<CacheEnvironment>(
                "inspect environment"_str, path.as_path(), rstd::move(exists).unwrap_err());
        }
        if (*exists) {
            auto contents = rstd::fs::read_to_string(path.as_path());
            if (contents.is_err()) {
                return cache_io_failure<CacheEnvironment>(
                    "read environment"_str, path.as_path(), rstd::move(contents).unwrap_err());
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

} // namespace lito
