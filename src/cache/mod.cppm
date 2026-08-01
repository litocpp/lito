export module tenon.cache;

import rstd;
import rstd.json;
import tenon.model;
import tenon.build_layout;
import :hash;

using namespace rstd::prelude;
using namespace rstd::literals;
using Json      = rstd::json::Value;
using JsonMap   = rstd::json::Map;
using JsonArray = rstd::json::Array;

namespace tenon
{

inline constexpr auto CACHE_VERSION  = u64(1);
inline constexpr auto SCAN_RECIPE    = "tenon-native-preprocess-v1"_str;
inline constexpr auto COMPILE_RECIPE = "clang-cxx-compile-v1"_str;

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

auto collect_stale_records(ref<rstd::path::Path>                             directory,
                           const rstd::collections::BTreeMap<String, empty>& current)
    -> Result<empty> {
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
            auto nested = collect_stale_records(path.as_path(), current);
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
        auto removed = rstd::fs::remove_file(path.as_path());
        if (removed.is_err()) {
            return cache_failure<empty>(rstd::format("cannot remove stale cache record '{}': {}",
                                                     path.as_path(),
                                                     rstd::move(removed).unwrap_err()));
        }
    }
    return Ok(empty {});
}

} // namespace tenon

export namespace tenon
{

struct DependencyArtifact {
    String logical_name;
    String artifact;
};

class CacheDecision {
    bool    current_ { false };
    String  artifact_;
    PathBuf record_;
    Json    building_;
    Json    complete_;

    CacheDecision(bool current, String artifact, PathBuf record, Json building, Json complete)
        : current_(current),
          artifact_(rstd::move(artifact)),
          record_(rstd::move(record)),
          building_(rstd::move(building)),
          complete_(rstd::move(complete)) {}

    friend class CompileCacheSession;

public:
    CacheDecision(CacheDecision&&) noexcept                    = default;
    auto operator=(CacheDecision&&) noexcept -> CacheDecision& = default;

    auto current() const noexcept -> bool { return current_; }
    auto artifact() const -> ref<str> { return artifact_.as_str(); }
    auto record() const -> ref<rstd::path::Path> { return record_.as_path(); }
};

class CompileCacheSession {
    String                                               environment_;
    bool                                                 force_refresh_ { false };
    rstd::collections::BTreeMap<String, FileFingerprint> file_fingerprints_;

    auto file_fingerprint(ref<rstd::path::Path> path) -> Result<FileFingerprint> {
        auto text = path_string(path);
        if (text.is_err()) return Err(rstd::move(text).unwrap_err());
        auto cached = file_fingerprints_.get(text->as_str());
        if (cached.is_some()) return Ok((**cached).clone());

        auto metadata = rstd::fs::metadata(path);
        if (metadata.is_err()) {
            return cache_failure<FileFingerprint>(rstd::format(
                "cannot inspect cache input '{}': {}", path, rstd::move(metadata).unwrap_err()));
        }
        if (! metadata->is_file()) {
            return cache_failure<FileFingerprint>(
                rstd::format("cache input '{}' is not a file", path));
        }
        auto contents = rstd::fs::read(path);
        if (contents.is_err()) {
            return cache_failure<FileFingerprint>(rstd::format(
                "cannot hash cache input '{}': {}", path, rstd::move(contents).unwrap_err()));
        }
        auto hash = cache::FNV_OFFSET;
        cache::add_text(hash, "tenon-file-content-v1"_str);
        cache::add_bytes(hash, contents->as_slice());
        auto fingerprint = FileFingerprint {
            .path        = PathBuf::from(path),
            .size        = metadata->size(),
            .fingerprint = cache::hex(hash),
        };
        file_fingerprints_.insert(rstd::move(text).unwrap(), fingerprint.clone());
        return Ok(rstd::move(fingerprint));
    }

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
        if (parsed.is_err() || *parsed != complete) return Ok(false);
        auto object = output_exists(unit.unit.object.as_path());
        if (object.is_err()) return object;
        if (! *object) return Ok(false);
        if (unit.unit.bmi.is_some()) {
            auto bmi = output_exists((*unit.unit.bmi).as_path());
            if (bmi.is_err()) return bmi;
            if (! *bmi) return Ok(false);
        }
        return Ok(true);
    }

public:
    static auto create(const BuildLayout&      layout,
                       ref<rstd::path::Path>   owner_root,
                       ref<str>                profile,
                       const CompilerIdentity& compiler) -> Result<CompileCacheSession> {
        auto owner         = path_string(owner_root);
        auto compiler_path = path_string(compiler.path.as_path());
        auto resource      = path_string(compiler.resource_directory.as_path());
        if (owner.is_err()) return Err(rstd::move(owner).unwrap_err());
        if (compiler_path.is_err()) return Err(rstd::move(compiler_path).unwrap_err());
        if (resource.is_err()) return Err(rstd::move(resource).unwrap_err());

        auto identity = String::make("tenon-cache-environment-v1\n"_str);
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
        identity.push_str(resource->as_str());
        identity.push_ascii('\n');
        identity.push_str(rstd::format("{}\n{}\n{}",
                                       compiler.size,
                                       compiler.modified_seconds,
                                       compiler.modified_nanoseconds)
                              .as_str());
        auto key = cache::text_identity("tenon-cache-environment-key-v1"_str, identity.as_str());

        auto compiler_json = JsonMap::make();
        compiler_json.insert(String::make("modified-nanoseconds"_str),
                             cache_u64(rstd::as_cast<u64>(compiler.modified_nanoseconds)));
        compiler_json.insert(String::make("modified-seconds"_str),
                             cache_i64(compiler.modified_seconds));
        compiler_json.insert(String::make("path"_str), cache_string(compiler_path->as_str()));
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
            return cache_failure<CompileCacheSession>(
                rstd::format("cannot inspect cache environment '{}': {}",
                             path.as_path(),
                             rstd::move(exists).unwrap_err()));
        }
        if (*exists) {
            auto contents = rstd::fs::read_to_string(path.as_path());
            if (contents.is_err()) {
                return cache_failure<CompileCacheSession>(
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
                    return cache_failure<CompileCacheSession>(
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
        auto session           = CompileCacheSession {};
        session.environment_   = rstd::move(key);
        session.force_refresh_ = refresh;
        return Ok(rstd::move(session));
    }

    auto evaluate(ref<str>                       target,
                  const PreparedUnit&            unit,
                  const ScanResult&              scan,
                  const CompileInvocation&       invocation,
                  const Vec<DependencyArtifact>& dependencies) -> Result<CacheDecision> {
        auto source   = path_string(unit.unit.source.as_path());
        auto relative = path_string(unit.unit.relative_source.as_path());
        auto object   = path_string(unit.unit.object.as_path());
        if (source.is_err()) return Err(rstd::move(source).unwrap_err());
        if (relative.is_err()) return Err(rstd::move(relative).unwrap_err());
        if (object.is_err()) return Err(rstd::move(object).unwrap_err());

        auto files       = rstd::collections::BTreeMap<String, FileFingerprint>::make();
        auto source_file = file_fingerprint(unit.unit.source.as_path());
        if (source_file.is_err()) return Err(rstd::move(source_file).unwrap_err());
        files.insert(source->clone(), rstd::move(source_file).unwrap());
        for (const auto& header : scan.header_inputs) {
            auto header_path = path_string(header.as_path());
            if (header_path.is_err()) return Err(rstd::move(header_path).unwrap_err());
            if (files.contains_key(header_path->as_str())) continue;
            auto header_file = file_fingerprint(header.as_path());
            if (header_file.is_err()) return Err(rstd::move(header_file).unwrap_err());
            files.insert(rstd::move(header_path).unwrap(), rstd::move(header_file).unwrap());
        }

        auto context_key =
            cache::text_identity("tenon-context-key-v1"_str, unit.unit.context->id.as_str());
        auto command_key =
            cache::text_identity("tenon-command-key-v1"_str, invocation.identity.as_str());
        auto artifact_hash = cache::FNV_OFFSET;
        cache::add_text(artifact_hash, "tenon-artifact-v2"_str);
        cache::add_text(artifact_hash, environment_.as_str());
        cache::add_text(artifact_hash, context_key.as_str());
        cache::add_text(artifact_hash, command_key.as_str());
        cache::add_text(artifact_hash, scan.preprocessor_environment.as_str());

        auto files_json = JsonArray::make();
        auto file_iter  = files.iter();
        for (auto item = file_iter.next(); item.is_some(); item = file_iter.next()) {
            const auto& path = *(*item).template get<0>();
            const auto& file = *(*item).template get<1>();
            cache::add_text(artifact_hash, path.as_str());
            cache::add_text(artifact_hash, file.fingerprint.as_str());
            auto encoded = file_json(file);
            if (encoded.is_err()) return Err(rstd::move(encoded).unwrap_err());
            files_json.push(rstd::move(encoded).unwrap());
        }

        auto provided = Json::Null();
        if (scan.provided.is_some()) {
            auto value = JsonMap::make();
            value.insert(
                String::make("kind"_str),
                cache_string(scan.provided->is_interface ? "interface"_str : "internal"_str));
            value.insert(String::make("logical-name"_str),
                         cache_string(scan.provided->logical_name.as_str()));
            cache::add_text(artifact_hash, scan.provided->logical_name.as_str());
            cache::add_text(artifact_hash,
                            scan.provided->is_interface ? "interface"_str : "internal"_str);
            provided = Json::Object(rstd::move(value));
        }
        auto implementation = Json::Null();
        if (scan.implementation_module.is_some()) {
            implementation = cache_string(scan.implementation_module->as_str());
            cache::add_text(artifact_hash, scan.implementation_module->as_str());
        }
        auto required_json = JsonArray::make();
        for (const auto& required : scan.required_modules) {
            required_json.push(cache_string(required.as_str()));
            cache::add_text(artifact_hash, required.as_str());
        }

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

        auto scan_json = JsonMap::make();
        scan_json.insert(String::make("implementation-module"_str), rstd::move(implementation));
        scan_json.insert(String::make("preprocessor-environment"_str),
                         cache_string(scan.preprocessor_environment.as_str()));
        scan_json.insert(String::make("provided"_str), rstd::move(provided));
        scan_json.insert(String::make("requires"_str), Json::Array(rstd::move(required_json)));

        auto outputs  = JsonMap::make();
        auto bmi_json = Json::Null();
        if (unit.unit.bmi.is_some()) {
            auto bmi = path_string((*unit.unit.bmi).as_path());
            if (bmi.is_err()) return Err(rstd::move(bmi).unwrap_err());
            bmi_json = cache_string(bmi->as_str());
        }
        outputs.insert(String::make("bmi"_str), rstd::move(bmi_json));
        outputs.insert(String::make("object"_str), cache_string(object->as_str()));

        auto complete = JsonMap::make();
        complete.insert(String::make("artifact"_str), cache_string(artifact.as_str()));
        complete.insert(String::make("command"_str), cache_string(command_key.as_str()));
        complete.insert(String::make("context"_str), cache_string(context_key.as_str()));
        complete.insert(String::make("direct-modules"_str), Json::Array(rstd::move(direct)));
        complete.insert(String::make("environment"_str), cache_string(environment_.as_str()));
        complete.insert(String::make("files"_str), Json::Array(rstd::move(files_json)));
        complete.insert(String::make("outputs"_str), Json::Object(rstd::move(outputs)));
        complete.insert(String::make("scan"_str), Json::Object(rstd::move(scan_json)));
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

        auto current = record_current(unit, complete_json);
        if (current.is_err()) return Err(rstd::move(current).unwrap_err());
        return Ok(CacheDecision { *current,
                                  rstd::move(artifact),
                                  unit.unit.cache_record.clone(),
                                  rstd::move(building_json),
                                  rstd::move(complete_json) });
    }

    auto begin_compile(const CacheDecision& decision) -> Result<empty> {
        return write_json(decision.record_.as_path(), decision.building_);
    }

    auto commit_success(const PreparedUnit& unit, const CacheDecision& decision) -> Result<empty> {
        auto object = output_exists(unit.unit.object.as_path());
        if (object.is_err()) return Err(rstd::move(object).unwrap_err());
        if (! *object) {
            return cache_failure<empty>(
                rstd::format("compiler did not produce object '{}'", unit.unit.object.as_path()));
        }
        if (unit.unit.bmi.is_some()) {
            auto bmi = output_exists((*unit.unit.bmi).as_path());
            if (bmi.is_err()) return Err(rstd::move(bmi).unwrap_err());
            if (! *bmi) {
                return cache_failure<empty>(
                    rstd::format("compiler did not produce BMI '{}'", (*unit.unit.bmi).as_path()));
            }
        }
        return write_json(decision.record_.as_path(), decision.complete_);
    }

    auto finish_target(const BuildLayout&  layout,
                       ref<str>            target,
                       const Vec<PathBuf>& current_records) -> Result<empty> {
        auto current = rstd::collections::BTreeMap<String, empty>::make();
        for (const auto& record : current_records) {
            auto path = path_string(record.as_path());
            if (path.is_err()) return Err(rstd::move(path).unwrap_err());
            current.insert(rstd::move(path).unwrap(), empty {});
        }
        auto directory = layout.cache_target_directory(target);
        return collect_stale_records(directory.as_path(), current);
    }
};

} // namespace tenon
