module lito.driver:cache.compile;

import rstd;
import rstd.json;
import lito.core;
import lito.cpp;
import :build.artifact;
import lito.toolchain.common;
import :build.layout;
import :cache.hash;
import :cache.common;

using namespace rstd::prelude;
using namespace rstd::literals;
using Json    = rstd::json::Value;
using JsonMap = rstd::json::Map;

namespace lito
{

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

    auto record_current(const cpp::PreparedUnit& unit, const Json& complete) const
        -> CacheResult<bool> {
        if (force_refresh_) return Ok(false);
        auto exists = rstd::fs::exists(unit.unit.cache_record.as_path());
        if (exists.is_err()) {
            return cache_io_failure<bool>("inspect compile record"_str,
                                          unit.unit.cache_record.as_path(),
                                          rstd::move(exists).unwrap_err());
        }
        if (! *exists) return Ok(false);
        auto contents = rstd::fs::read_to_string(unit.unit.cache_record.as_path());
        if (contents.is_err()) {
            return cache_io_failure<bool>("read compile record"_str,
                                          unit.unit.cache_record.as_path(),
                                          rstd::move(contents).unwrap_err());
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
        const auto* bmi_artifact = cpp::unit_bmi(unit.unit);
        if (bmi_artifact != nullptr) {
            auto bmi = output_exists(bmi_artifact->path.as_path());
            if (bmi.is_err()) return bmi;
            if (! *bmi) return Ok(false);
            auto stored_bmi_digest = json_text(*stored_digests, "bmi"_str);
            if (stored_bmi_digest.is_none()) return Ok(false);
            auto bmi_digest = output_content_digest(bmi_artifact->path.as_path());
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
                  const cpp::PreparedUnit&       unit,
                  ref<str>                       scan_receipt,
                  const CompileInvocation&       invocation,
                  const Vec<DependencyArtifact>& dependencies) -> CacheResult<CacheDecision> {
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
        cache::add_text(artifact_hash, unit.unit.source_origin_identity.as_str());

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

        auto        outputs      = JsonMap::make();
        auto        bmi_json     = Json::Null();
        const auto* bmi_artifact = cpp::unit_bmi(unit.unit);
        if (bmi_artifact != nullptr) {
            auto bmi = path_string(bmi_artifact->path.as_path());
            if (bmi.is_err()) return Err(rstd::move(bmi).unwrap_err());
            auto bmi_output = JsonMap::make();
            bmi_output.insert(
                String::make("format"_str),
                cache_string(cpp::bmi_format_identity(bmi_artifact->format).as_str()));
            bmi_output.insert(String::make("kind"_str), cache_string("bmi"_str));
            bmi_output.insert(String::make("path"_str), cache_string(bmi->as_str()));
            bmi_output.insert(String::make("recipe"_str),
                              cache_string(bmi_artifact->key.value.as_str()));
            bmi_output.insert(
                String::make("representation"_str),
                cache_string(cpp::bmi_representation_name(bmi_artifact->request.representation)));
            bmi_output.insert(String::make("source-embedding"_str),
                              cache_string(cpp::bmi_source_embedding_name(
                                  bmi_artifact->request.source_embedding)));
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
        complete.insert(String::make("source-origin"_str),
                        cache_string(unit.unit.source_origin_identity.as_str()));
        complete.insert(String::make("state"_str), cache_string("complete"_str));
        complete.insert(String::make("target"_str), cache_string(target));
        complete.insert(String::make("version"_str), cache_u64(CACHE_VERSION));
        auto complete_json = Json::Object(rstd::move(complete));

        auto building = JsonMap::make();
        building.insert(String::make("artifact"_str), cache_string(artifact.as_str()));
        building.insert(String::make("command"_str), cache_string(command_key.as_str()));
        building.insert(String::make("environment"_str), cache_string(environment_.as_str()));
        building.insert(String::make("source"_str), cache_string(relative->as_str()));
        building.insert(String::make("source-origin"_str),
                        cache_string(unit.unit.source_origin_identity.as_str()));
        building.insert(String::make("state"_str), cache_string("building"_str));
        building.insert(String::make("target"_str), cache_string(target));
        building.insert(String::make("version"_str), cache_u64(CACHE_VERSION));
        auto building_json = Json::Object(rstd::move(building));

        auto previous_outputs = read_receipt_output_paths(unit.unit.cache_record.as_path());
        if (previous_outputs.is_err()) return Err(rstd::move(previous_outputs).unwrap_err());
        auto stale_outputs = Vec<PathBuf>::make();
        for (auto& path : *previous_outputs) {
            auto        current_output = path.as_path() == unit.unit.object.as_path();
            const auto* bmi_artifact   = cpp::unit_bmi(unit.unit);
            if (bmi_artifact != nullptr)
                current_output = current_output || path.as_path() == bmi_artifact->path.as_path();
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

    auto begin_compile(const CacheDecision& decision) -> CacheResult<empty> {
        return write_json(decision.record_.as_path(), decision.building_);
    }

    auto begin_compile_test(const CacheDecision&                decision,
                            ref<rstd::path::Path>               record,
                            const cpp::ResolvedCompileTestCase& test) -> CacheResult<empty> {
        auto root = JsonMap::make();
        root.insert(String::make("case"_str), cache_string(test.name.as_str()));
        root.insert(String::make("compile"_str), decision.complete_.clone());
        root.insert(String::make("expected"_str),
                    cache_string(test.outcome == lito::manifest::CompileTestOutcome::Success
                                     ? "success"_str
                                     : "failure"_str));
        root.insert(String::make("state"_str), cache_string("running"_str));
        root.insert(String::make("version"_str), cache_u64(CACHE_VERSION));
        return write_json(record, Json::Object(rstd::move(root)));
    }

    auto record_compile_test(const CacheDecision&        decision,
                             ref<rstd::path::Path>       record,
                             const CompileTestExecution& execution) -> CacheResult<empty> {
        auto mismatch = Json::Null();
        if (execution.mismatch.is_some()) {
            mismatch = cache_string(execution.mismatch->as_str());
        }
        auto result = JsonMap::make();
        result.insert(String::make("exit-code"_str), cache_i64(as_cast<i64>(execution.exit_code)));
        result.insert(String::make("matched"_str), Json::Bool(execution.success()));
        result.insert(String::make("mismatch"_str), rstd::move(mismatch));
        result.insert(String::make("stderr-bytes"_str),
                      cache_u64(as_cast<u64>(execution.standard_error.len())));
        result.insert(String::make("stderr-fingerprint"_str),
                      cache_string(cache::text_identity("lito-compile-test-stderr-v1"_str,
                                                        execution.standard_error.as_str())
                                       .as_str()));
        result.insert(String::make("stdout-bytes"_str),
                      cache_u64(as_cast<u64>(execution.standard_output.len())));

        auto root = JsonMap::make();
        root.insert(String::make("case"_str), cache_string(execution.name.as_str()));
        root.insert(String::make("compile"_str), decision.complete_.clone());
        root.insert(String::make("expected"_str),
                    cache_string(execution.expected == lito::manifest::CompileTestOutcome::Success
                                     ? "success"_str
                                     : "failure"_str));
        root.insert(String::make("result"_str), Json::Object(rstd::move(result)));
        root.insert(String::make("state"_str), cache_string("complete"_str));
        root.insert(String::make("version"_str), cache_u64(CACHE_VERSION));
        return write_json(record, Json::Object(rstd::move(root)));
    }

    auto commit_success(const cpp::PreparedUnit& unit, const CacheDecision& decision)
        -> CacheResult<empty> {
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
        const auto* bmi_artifact = cpp::unit_bmi(unit.unit);
        if (bmi_artifact != nullptr) {
            auto bmi = output_exists(bmi_artifact->path.as_path());
            if (bmi.is_err()) return Err(rstd::move(bmi).unwrap_err());
            if (! *bmi) {
                return cache_failure<empty>(rstd::format("compiler did not produce BMI '{}'",
                                                         bmi_artifact->path.as_path()));
            }
            auto bmi_digest = output_content_digest(bmi_artifact->path.as_path());
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

    auto finish_target(const BuildLayout&                    layout,
                       const lito::package::PackageTargetId& target,
                       const Vec<PathBuf>& current_records) -> CacheResult<empty> {
        auto directory = layout.cache_target_directory(target);
        return finish_directory(directory.as_path(), current_records);
    }

    auto finish_directory(ref<rstd::path::Path> directory, const Vec<PathBuf>& current_records)
        -> CacheResult<empty> {
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
