module lito.driver:cache.archive;

import rstd;
import rstd.json;
import lito.core;
import lito.toolchain.common;
import :cache.hash;
import :cache.common;

using namespace rstd::prelude;
using namespace rstd::literals;
using Json    = rstd::json::Value;
using JsonMap = rstd::json::Map;

namespace lito
{

struct ArchiveCacheReceipt {
    String                      artifact;
    String                      command;
    String                      environment;
    Vec<CachedArtifactIdentity> inputs;
    String                      output;
    String                      target;
};

auto archive_receipt_json(const ArchiveCacheReceipt& receipt) -> Json {
    auto inputs = JsonArray::with_capacity(receipt.inputs.len());
    for (const auto& input : receipt.inputs) {
        auto value = JsonMap::make();
        value.insert(String::make("content"_str), cache_string(input.content.as_str()));
        value.insert(String::make("recipe"_str), cache_string(input.recipe.as_str()));
        inputs.push(Json::Object(rstd::move(value)));
    }
    auto output = JsonMap::make();
    output.insert(String::make("kind"_str), cache_string("archive"_str));
    output.insert(String::make("path"_str), cache_string(receipt.output.as_str()));
    output.insert(String::make("recipe"_str), cache_string(receipt.artifact.as_str()));
    auto outputs = JsonMap::make();
    outputs.insert(String::make("archive"_str), Json::Object(rstd::move(output)));

    auto complete = JsonMap::make();
    complete.insert(String::make("artifact"_str), cache_string(receipt.artifact.as_str()));
    complete.insert(String::make("command"_str), cache_string(receipt.command.as_str()));
    complete.insert(String::make("environment"_str), cache_string(receipt.environment.as_str()));
    complete.insert(String::make("inputs"_str), Json::Array(rstd::move(inputs)));
    complete.insert(String::make("outputs"_str), Json::Object(rstd::move(outputs)));
    complete.insert(String::make("state"_str), cache_string("complete"_str));
    complete.insert(String::make("target"_str), cache_string(receipt.target.as_str()));
    complete.insert(String::make("version"_str), cache_u64(CACHE_VERSION));
    return Json::Object(rstd::move(complete));
}

auto archive_building_receipt_json(const ArchiveCacheReceipt& receipt) -> Json {
    auto building = JsonMap::make();
    building.insert(String::make("artifact"_str), cache_string(receipt.artifact.as_str()));
    building.insert(String::make("command"_str), cache_string(receipt.command.as_str()));
    building.insert(String::make("environment"_str), cache_string(receipt.environment.as_str()));
    building.insert(String::make("state"_str), cache_string("building"_str));
    building.insert(String::make("target"_str), cache_string(receipt.target.as_str()));
    building.insert(String::make("version"_str), cache_u64(CACHE_VERSION));
    return Json::Object(rstd::move(building));
}

struct CurrentArchiveRecord {
    bool                           current {};
    Option<CachedArtifactIdentity> identity;
};

class ArchiveCacheDecision {
    bool                           current_ {};
    ArchiveCacheReceipt            receipt_;
    PathBuf                        record_;
    PathBuf                        output_;
    Option<CachedArtifactIdentity> identity_;
    Vec<PathBuf>                   stale_outputs_;

    ArchiveCacheDecision(bool                           current,
                         ArchiveCacheReceipt            receipt,
                         PathBuf                        record,
                         PathBuf                        output,
                         Option<CachedArtifactIdentity> identity,
                         Vec<PathBuf>                   stale_outputs)
        : current_(current),
          receipt_(rstd::move(receipt)),
          record_(rstd::move(record)),
          output_(rstd::move(output)),
          identity_(rstd::move(identity)),
          stale_outputs_(rstd::move(stale_outputs)) {}

    friend class ArchiveCacheSession;

public:
    ArchiveCacheDecision(ArchiveCacheDecision&&) noexcept = default;

    auto current() const noexcept -> bool { return current_; }
    auto identity() const -> Option<ref<CachedArtifactIdentity>> {
        return identity_.is_some()
                   ? Some(ref<CachedArtifactIdentity>::from_raw_parts(rstd::addressof(*identity_)))
                   : None<ref<CachedArtifactIdentity>>();
    }
};

class ArchiveCacheSession {
    String  environment_;
    PathBuf owner_root_;
    bool    force_refresh_ {};

    auto record_current(ref<rstd::path::Path>      record,
                        ref<rstd::path::Path>      output,
                        const ArchiveCacheReceipt& receipt) const
        -> CacheResult<CurrentArchiveRecord> {
        if (force_refresh_) return Ok(CurrentArchiveRecord {});
        auto exists = rstd::fs::exists(record);
        if (exists.is_err()) {
            return cache_io_failure<CurrentArchiveRecord>(
                "inspect archive record"_str, record, rstd::move(exists).unwrap_err());
        }
        if (! *exists) return Ok(CurrentArchiveRecord {});
        auto contents = rstd::fs::read_to_string(record);
        if (contents.is_err()) {
            return cache_io_failure<CurrentArchiveRecord>(
                "read archive record"_str, record, rstd::move(contents).unwrap_err());
        }
        auto parsed = rstd::json::from_str(contents->as_str());
        if (parsed.is_err()) return Ok(CurrentArchiveRecord {});
        auto comparable        = parsed->clone();
        auto comparable_object = comparable.as_object_mut();
        if (comparable_object.is_none()) return Ok(CurrentArchiveRecord {});
        (**comparable_object).remove("content-digests"_str);
        if (comparable != archive_receipt_json(receipt)) return Ok(CurrentArchiveRecord {});
        auto stored_digests = parsed->get("content-digests"_str);
        if (stored_digests.is_none()) return Ok(CurrentArchiveRecord {});
        auto stored_archive_digest = json_text(*stored_digests, "archive"_str);
        if (stored_archive_digest.is_none()) return Ok(CurrentArchiveRecord {});
        auto output_present = output_exists(output);
        if (output_present.is_err()) return Err(rstd::move(output_present).unwrap_err());
        if (! *output_present) return Ok(CurrentArchiveRecord {});
        auto digest = output_content_digest(output);
        if (digest.is_err()) return Err(rstd::move(digest).unwrap_err());
        if (digest->as_str() != *stored_archive_digest) return Ok(CurrentArchiveRecord {});
        return Ok(CurrentArchiveRecord {
            .current  = true,
            .identity = Some(CachedArtifactIdentity {
                .recipe  = receipt.artifact.clone(),
                .content = rstd::move(digest).unwrap(),
            }),
        });
    }

public:
    static auto create(const CacheEnvironment& environment, ref<rstd::path::Path> owner_root)
        -> ArchiveCacheSession {
        auto session           = ArchiveCacheSession {};
        session.environment_   = environment.key_.clone();
        session.owner_root_    = PathBuf::from(owner_root);
        session.force_refresh_ = environment.force_refresh_;
        return session;
    }

    auto evaluate(ref<str>                           target,
                  ref<rstd::path::Path>              record,
                  const ArchiveInvocation&           invocation,
                  const Vec<CachedArtifactIdentity>& inputs) -> CacheResult<ArchiveCacheDecision> {
        auto output = path_string(invocation.output.as_path());
        if (output.is_err()) return Err(rstd::move(output).unwrap_err());
        auto command_identity = invocation.identity();
        auto command =
            cache::text_identity("lito-archive-command-v1"_str, command_identity.as_str());
        auto artifact_hash = cache::FNV_OFFSET;
        cache::add_text(artifact_hash, "lito-archive-artifact-v1"_str);
        cache::add_text(artifact_hash, environment_.as_str());
        cache::add_text(artifact_hash, command.as_str());
        cache::add_text(artifact_hash, target);
        auto input_receipts = Vec<CachedArtifactIdentity>::with_capacity(inputs.len());
        for (const auto& input : inputs) {
            input_receipts.push(input.clone());
            cache::add_text(artifact_hash, input.recipe.as_str());
            cache::add_text(artifact_hash, input.content.as_str());
        }
        auto receipt = ArchiveCacheReceipt {
            .artifact    = cache::hex(artifact_hash),
            .command     = rstd::move(command),
            .environment = environment_.clone(),
            .inputs      = rstd::move(input_receipts),
            .output      = rstd::move(output).unwrap(),
            .target      = String::make(target),
        };
        auto previous_outputs = read_receipt_output_paths(record);
        if (previous_outputs.is_err()) return Err(rstd::move(previous_outputs).unwrap_err());
        auto stale_outputs = Vec<PathBuf>::make();
        for (auto& previous : *previous_outputs) {
            if (previous.as_path() != invocation.output.as_path()) {
                stale_outputs.push(rstd::move(previous));
            }
        }
        auto current = record_current(record, invocation.output.as_path(), receipt);
        if (current.is_err()) return Err(rstd::move(current).unwrap_err());
        return Ok(ArchiveCacheDecision { current->current,
                                         rstd::move(receipt),
                                         PathBuf::from(record),
                                         invocation.output.clone(),
                                         rstd::move(current->identity),
                                         rstd::move(stale_outputs) });
    }

    auto begin_archive(const ArchiveCacheDecision& decision) -> CacheResult<empty> {
        return write_json(decision.record_.as_path(),
                          archive_building_receipt_json(decision.receipt_));
    }

    auto commit_success(const ArchiveCacheDecision& decision)
        -> CacheResult<CachedArtifactIdentity> {
        auto present = output_exists(decision.output_.as_path());
        if (present.is_err()) return Err(rstd::move(present).unwrap_err());
        if (! *present) {
            return cache_failure<CachedArtifactIdentity>(
                rstd::format("archiver did not produce output '{}'", decision.output_.as_path()));
        }
        auto digest = output_content_digest(decision.output_.as_path());
        if (digest.is_err()) return Err(rstd::move(digest).unwrap_err());
        auto complete        = archive_receipt_json(decision.receipt_);
        auto complete_object = complete.as_object_mut();
        if (complete_object.is_none()) {
            return cache_failure<CachedArtifactIdentity>(
                String::make("archive cache receipt is not an object"_str));
        }
        auto digests = JsonMap::make();
        digests.insert(String::make("archive"_str), cache_string(digest->as_str()));
        (**complete_object)
            .insert(String::make("content-digests"_str), Json::Object(rstd::move(digests)));
        for (const auto& output : decision.stale_outputs_) {
            auto removed = remove_owned_output(output.as_path(), owner_root_.as_path());
            if (removed.is_err()) return Err(rstd::move(removed).unwrap_err());
        }
        auto written = write_json(decision.record_.as_path(), complete);
        if (written.is_err()) return Err(rstd::move(written).unwrap_err());
        return Ok(CachedArtifactIdentity {
            .recipe  = decision.receipt_.artifact.clone(),
            .content = rstd::move(digest).unwrap(),
        });
    }
};

} // namespace lito
