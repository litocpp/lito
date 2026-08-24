export module lito.frontend:service;

import rstd;
import lito.frontend.lexical;
import lito.frontend.preprocessor;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::frontend
{

enum class FrontendActivity
{
    SourceResolve,
    SourceRead,
    Lex,
};

struct FrontendObserver {
    void* context {};
    void (*begin)(void*, FrontendActivity) noexcept {};
    void (*end)(void*, FrontendActivity) noexcept {};
};

class FrontendActivityGuard {
public:
    FrontendActivityGuard() = default;

    FrontendActivityGuard(FrontendObserver observer, FrontendActivity activity) noexcept
        : observer_(observer), activity_(activity), active_(true) {
        if (observer_.begin != nullptr) observer_.begin(observer_.context, activity_);
    }

    FrontendActivityGuard(const FrontendActivityGuard&)                    = delete;
    auto operator=(const FrontendActivityGuard&) -> FrontendActivityGuard& = delete;

    FrontendActivityGuard(FrontendActivityGuard&& other) noexcept
        : observer_(other.observer_), activity_(other.activity_), active_(other.active_) {
        other.active_ = false;
    }

    auto operator=(FrontendActivityGuard&& other) noexcept -> FrontendActivityGuard& {
        if (this == &other) return *this;
        finish();
        observer_     = other.observer_;
        activity_     = other.activity_;
        active_       = other.active_;
        other.active_ = false;
        return *this;
    }

    ~FrontendActivityGuard() { finish(); }

    auto finish() noexcept -> void {
        if (! active_) return;
        active_ = false;
        if (observer_.end != nullptr) observer_.end(observer_.context, activity_);
    }

private:
    FrontendObserver observer_ {};
    FrontendActivity activity_ { FrontendActivity::SourceResolve };
    bool             active_ {};
};

struct FrontendStatistics {
    usize                                source_requests {};
    usize                                source_hits {};
    usize                                source_stats {};
    usize                                source_reads {};
    usize                                source_bytes {};
    usize                                source_waits {};
    rstd::time::Duration                 source_wait;
    usize                                source_ready_entries {};
    usize                                source_ready_peak {};
    usize                                source_live_payloads {};
    usize                                source_live_payload_peak {};
    usize                                source_retained_bytes {};
    usize                                source_retained_bytes_peak {};
    usize                                source_storage_bytes {};
    usize                                source_storage_bytes_peak {};
    usize                                source_token_bytes {};
    usize                                source_token_bytes_peak {};
    usize                                source_arena_used_bytes {};
    usize                                source_arena_used_bytes_peak {};
    usize                                source_arena_reserved_bytes {};
    usize                                source_arena_reserved_bytes_peak {};
    usize                                source_metadata_reserved_bytes {};
    usize                                source_metadata_reserved_bytes_peak {};
    usize                                source_in_flight_entries {};
    usize                                source_in_flight_peak {};
    usize                                source_cache_hits {};
    usize                                source_flight_waits {};
    usize                                source_domain_releases {};
    usize                                lex_builds {};
    usize                                analyze_builds {};
    usize                                analyze_hits {};
    usize                                full_analyses {};
    usize                                full_analysis_peak {};
    usize                                compacted_analyses {};
    usize                                compacted_analysis_bytes {};
    usize                                persistent_scan_hits {};
    usize                                persistent_scan_misses {};
    usize                                persistent_scan_uncacheable {};
    usize                                persistent_scan_absent {};
    usize                                persistent_scan_refresh {};
    usize                                persistent_scan_version {};
    usize                                persistent_scan_recipe {};
    usize                                persistent_scan_corrupt {};
    usize                                persistent_scan_environment {};
    usize                                persistent_scan_context {};
    usize                                persistent_scan_source {};
    usize                                persistent_scan_file_dependency {};
    usize                                persistent_scan_include_lookup {};
    usize                                persistent_scan_embed_lookup {};
    usize                                persistent_scan_external_macro {};
    usize                                persistent_scan_receipt {};
    usize                                persistent_fingerprint_requests {};
    usize                                persistent_fingerprint_hits {};
    usize                                persistent_fingerprint_builds {};
    usize                                persistent_fingerprint_waits {};
    rstd::time::Duration                 persistent_fingerprint_wait;
    preprocessor::PreprocessorStatistics preprocessor;

    auto add(const FrontendStatistics& other) noexcept -> void {
        source_requests += other.source_requests;
        source_hits += other.source_hits;
        source_stats += other.source_stats;
        source_reads += other.source_reads;
        source_bytes += other.source_bytes;
        source_waits += other.source_waits;
        source_wait = source_wait.saturating_add(other.source_wait);
        source_ready_entries += other.source_ready_entries;
        if (other.source_ready_peak > source_ready_peak) {
            source_ready_peak = other.source_ready_peak;
        }
        source_live_payloads += other.source_live_payloads;
        if (other.source_live_payload_peak > source_live_payload_peak) {
            source_live_payload_peak = other.source_live_payload_peak;
        }
        source_retained_bytes += other.source_retained_bytes;
        if (other.source_retained_bytes_peak > source_retained_bytes_peak) {
            source_retained_bytes_peak = other.source_retained_bytes_peak;
        }
        source_storage_bytes += other.source_storage_bytes;
        if (other.source_storage_bytes_peak > source_storage_bytes_peak) {
            source_storage_bytes_peak = other.source_storage_bytes_peak;
        }
        source_token_bytes += other.source_token_bytes;
        if (other.source_token_bytes_peak > source_token_bytes_peak) {
            source_token_bytes_peak = other.source_token_bytes_peak;
        }
        source_arena_used_bytes += other.source_arena_used_bytes;
        if (other.source_arena_used_bytes_peak > source_arena_used_bytes_peak) {
            source_arena_used_bytes_peak = other.source_arena_used_bytes_peak;
        }
        source_arena_reserved_bytes += other.source_arena_reserved_bytes;
        if (other.source_arena_reserved_bytes_peak > source_arena_reserved_bytes_peak) {
            source_arena_reserved_bytes_peak = other.source_arena_reserved_bytes_peak;
        }
        source_metadata_reserved_bytes += other.source_metadata_reserved_bytes;
        if (other.source_metadata_reserved_bytes_peak > source_metadata_reserved_bytes_peak) {
            source_metadata_reserved_bytes_peak = other.source_metadata_reserved_bytes_peak;
        }
        source_in_flight_entries += other.source_in_flight_entries;
        if (other.source_in_flight_peak > source_in_flight_peak) {
            source_in_flight_peak = other.source_in_flight_peak;
        }
        source_cache_hits += other.source_cache_hits;
        source_flight_waits += other.source_flight_waits;
        source_domain_releases += other.source_domain_releases;
        lex_builds += other.lex_builds;
        analyze_builds += other.analyze_builds;
        analyze_hits += other.analyze_hits;
        full_analyses += other.full_analyses;
        if (other.full_analysis_peak > full_analysis_peak) {
            full_analysis_peak = other.full_analysis_peak;
        }
        compacted_analyses += other.compacted_analyses;
        compacted_analysis_bytes += other.compacted_analysis_bytes;
        persistent_scan_hits += other.persistent_scan_hits;
        persistent_scan_misses += other.persistent_scan_misses;
        persistent_scan_uncacheable += other.persistent_scan_uncacheable;
        persistent_scan_absent += other.persistent_scan_absent;
        persistent_scan_refresh += other.persistent_scan_refresh;
        persistent_scan_version += other.persistent_scan_version;
        persistent_scan_recipe += other.persistent_scan_recipe;
        persistent_scan_corrupt += other.persistent_scan_corrupt;
        persistent_scan_environment += other.persistent_scan_environment;
        persistent_scan_context += other.persistent_scan_context;
        persistent_scan_source += other.persistent_scan_source;
        persistent_scan_file_dependency += other.persistent_scan_file_dependency;
        persistent_scan_include_lookup += other.persistent_scan_include_lookup;
        persistent_scan_embed_lookup += other.persistent_scan_embed_lookup;
        persistent_scan_external_macro += other.persistent_scan_external_macro;
        persistent_scan_receipt += other.persistent_scan_receipt;
        persistent_fingerprint_requests += other.persistent_fingerprint_requests;
        persistent_fingerprint_hits += other.persistent_fingerprint_hits;
        persistent_fingerprint_builds += other.persistent_fingerprint_builds;
        persistent_fingerprint_waits += other.persistent_fingerprint_waits;
        persistent_fingerprint_wait =
            persistent_fingerprint_wait.saturating_add(other.persistent_fingerprint_wait);
        preprocessor.add(other.preprocessor);
    }
};

struct FrontendSourceStoreStatistics {
    usize ready_entries {};
    usize ready_peak {};
    usize live_payloads {};
    usize live_payload_peak {};
    usize retained_bytes {};
    usize retained_bytes_peak {};
    usize storage_bytes {};
    usize storage_bytes_peak {};
    usize token_bytes {};
    usize token_bytes_peak {};
    usize arena_used_bytes {};
    usize arena_used_bytes_peak {};
    usize arena_reserved_bytes {};
    usize arena_reserved_bytes_peak {};
    usize metadata_reserved_bytes {};
    usize metadata_reserved_bytes_peak {};
    usize in_flight_entries {};
    usize in_flight_peak {};
    usize cache_hits {};
    usize flight_waits {};
    usize domain_releases {};
};

struct HeaderCacheClassification {
    Option<String> retention_domain;
};

struct FrontendHeaderClassifier {
    const void* context {};
    HeaderCacheClassification (*classify)(const void*, ref<rstd::path::Path>) {};

    auto resolve(ref<rstd::path::Path> path) const -> HeaderCacheClassification {
        if (classify == nullptr) return HeaderCacheClassification {};
        return classify(context, path);
    }
};

class FrontendSourceStore {
    using SharedLoadError = rstd::sync::Arc<lexical::Error>;
    using LoadResult      = Result<lexical::SharedScanFileStorage, SharedLoadError>;
    using LoadCell        = rstd::sync::OnceLock<LoadResult>;
    using SharedLoadCell  = rstd::sync::Arc<LoadCell>;
    struct CacheRecord {
        String         domain;
        SharedLoadCell cell;
    };

    using CellMap = rstd::collections::HashMap<String, CacheRecord>;

    struct Fields {
        CellMap source_entries;
        CellMap identity_entries;
        usize   ready_entries {};
        usize   ready_peak {};
        usize   live_payloads {};
        usize   live_payload_peak {};
        usize   retained_bytes {};
        usize   retained_bytes_peak {};
        usize   storage_bytes {};
        usize   storage_bytes_peak {};
        usize   token_bytes {};
        usize   token_bytes_peak {};
        usize   arena_used_bytes {};
        usize   arena_used_bytes_peak {};
        usize   arena_reserved_bytes {};
        usize   arena_reserved_bytes_peak {};
        usize   metadata_reserved_bytes {};
        usize   metadata_reserved_bytes_peak {};
        usize   in_flight_entries {};
        usize   in_flight_peak {};
        usize   cache_hits {};
        usize   flight_waits {};
        usize   domain_releases {};

        Fields(): source_entries(CellMap::make()), identity_entries(CellMap::make()) {}
    };

    using State       = rstd::sync::Mutex<Fields>;
    using SharedState = rstd::sync::Arc<State>;

    struct Entry {
        SharedLoadCell cell;
        bool           existing {};
    };

public:
    static auto make() -> FrontendSourceStore {
        return FrontendSourceStore { SharedState::make(Fields {}) };
    }

    auto clone() const -> FrontendSourceStore { return FrontendSourceStore { state_.clone() }; }

    auto release_domain(ref<str> domain) const -> void {
        auto       fields                          = state_->lock().unwrap_unchecked();
        auto       removed_ready                   = usize {};
        auto       removed_flight                  = usize {};
        auto       removed_live                    = usize {};
        auto       removed_bytes                   = usize {};
        auto       removed_storage_bytes           = usize {};
        auto       removed_token_bytes             = usize {};
        auto       removed_arena_used_bytes        = usize {};
        auto       removed_arena_reserved_bytes    = usize {};
        auto       removed_metadata_reserved_bytes = usize {};
        const auto retain                          = [&](CacheRecord& record, bool payload_owner) {
            if (record.domain != domain) return true;
            auto stored = record.cell->get();
            if (stored.is_none()) {
                ++removed_flight;
                return false;
            }
            ++removed_ready;
            auto result = (**stored).as_ref();
            if (payload_owner && result.is_ok()) {
                ++removed_live;
                auto statistics = result.unwrap_unchecked()->statistics();
                removed_bytes += statistics.retained_bytes;
                removed_storage_bytes += statistics.source_reserved_bytes;
                removed_token_bytes += statistics.token_bytes;
                removed_arena_used_bytes += statistics.arena_used_bytes;
                removed_arena_reserved_bytes += statistics.arena_reserved_bytes;
                removed_metadata_reserved_bytes += statistics.metadata_reserved_bytes;
            }
            return false;
        };
        fields->source_entries.retain([&](const String&, CacheRecord& record) {
            return retain(record, false);
        });
        fields->identity_entries.retain([&](const String&, CacheRecord& record) {
            return retain(record, true);
        });
        fields->ready_entries -= removed_ready;
        fields->in_flight_entries -= removed_flight;
        fields->live_payloads -= removed_live;
        fields->retained_bytes -= removed_bytes;
        fields->storage_bytes -= removed_storage_bytes;
        fields->token_bytes -= removed_token_bytes;
        fields->arena_used_bytes -= removed_arena_used_bytes;
        fields->arena_reserved_bytes -= removed_arena_reserved_bytes;
        fields->metadata_reserved_bytes -= removed_metadata_reserved_bytes;
        if (removed_ready != usize {} || removed_flight != usize {}) {
            ++fields->domain_releases;
        }
    }

    auto release() const -> void {
        auto fields                     = state_->lock().unwrap_unchecked();
        fields->source_entries          = CellMap::make();
        fields->identity_entries        = CellMap::make();
        fields->ready_entries           = usize {};
        fields->live_payloads           = usize {};
        fields->retained_bytes          = usize {};
        fields->storage_bytes           = usize {};
        fields->token_bytes             = usize {};
        fields->arena_used_bytes        = usize {};
        fields->arena_reserved_bytes    = usize {};
        fields->metadata_reserved_bytes = usize {};
        fields->in_flight_entries       = usize {};
    }

    auto statistics() const -> FrontendSourceStoreStatistics {
        auto fields = state_->lock().unwrap_unchecked();
        return FrontendSourceStoreStatistics {
            .ready_entries                = fields->ready_entries,
            .ready_peak                   = fields->ready_peak,
            .live_payloads                = fields->live_payloads,
            .live_payload_peak            = fields->live_payload_peak,
            .retained_bytes               = fields->retained_bytes,
            .retained_bytes_peak          = fields->retained_bytes_peak,
            .storage_bytes                = fields->storage_bytes,
            .storage_bytes_peak           = fields->storage_bytes_peak,
            .token_bytes                  = fields->token_bytes,
            .token_bytes_peak             = fields->token_bytes_peak,
            .arena_used_bytes             = fields->arena_used_bytes,
            .arena_used_bytes_peak        = fields->arena_used_bytes_peak,
            .arena_reserved_bytes         = fields->arena_reserved_bytes,
            .arena_reserved_bytes_peak    = fields->arena_reserved_bytes_peak,
            .metadata_reserved_bytes      = fields->metadata_reserved_bytes,
            .metadata_reserved_bytes_peak = fields->metadata_reserved_bytes_peak,
            .in_flight_entries            = fields->in_flight_entries,
            .in_flight_peak               = fields->in_flight_peak,
            .cache_hits                   = fields->cache_hits,
            .flight_waits                 = fields->flight_waits,
            .domain_releases              = fields->domain_releases,
        };
    }

private:
    friend class FrontendService;

    explicit FrontendSourceStore(SharedState state): state_(rstd::move(state)) {}

    static auto cache_key(ref<str> domain, ref<str> key) -> String {
        return rstd::format("{}:{}{}", domain.len(), domain, key);
    }

    auto entry(Fields& fields, CellMap& entries, ref<str> domain, String resolved_key) const
        -> Entry {
        auto found = entries.get_mut(resolved_key.as_str());
        if (found.is_some()) {
            ++fields.cache_hits;
            if ((**found).cell->get().is_none()) ++fields.flight_waits;
            return Entry { .cell = (**found).cell.clone(), .existing = true };
        }
        auto cell = SharedLoadCell::make();
        entries.insert(rstd::move(resolved_key),
                       CacheRecord { .domain = String::make(domain), .cell = cell.clone() });
        ++fields.in_flight_entries;
        if (fields.in_flight_entries > fields.in_flight_peak) {
            fields.in_flight_peak = fields.in_flight_entries;
        }
        return Entry { .cell = rstd::move(cell) };
    }

    auto source(ref<str> domain, ref<str> key) const -> Entry {
        auto fields       = state_->lock().unwrap_unchecked();
        auto resolved_key = cache_key(domain, key);
        return entry(*fields, fields->source_entries, domain, rstd::move(resolved_key));
    }

    auto identity(ref<str> domain, ref<str> key) const -> Entry {
        auto fields = state_->lock().unwrap_unchecked();
        auto found  = fields->identity_entries.get_mut(key);
        if (found.is_some() && (**found).domain != domain) {
            (**found).domain = String::make(""_str);
        }
        return entry(*fields, fields->identity_entries, domain, String::make(key));
    }

    auto complete(Fields&               fields,
                  CellMap&              entries,
                  ref<str>              key,
                  const SharedLoadCell& cell,
                  const LoadResult&     result,
                  bool                  payload_owner) const -> void {
        auto current = entries.get(key);
        if (current.is_some() && SharedLoadCell::ptr_eq((**current).cell, cell)) {
            --fields.in_flight_entries;
            auto value = result.as_ref();
            if (value.is_err()) {
                (void)entries.remove(key);
                return;
            }
            ++fields.ready_entries;
            if (fields.ready_entries > fields.ready_peak) {
                fields.ready_peak = fields.ready_entries;
            }
            if (! payload_owner) return;
            ++fields.live_payloads;
            auto statistics = value.unwrap_unchecked()->statistics();
            fields.retained_bytes += statistics.retained_bytes;
            fields.storage_bytes += statistics.source_reserved_bytes;
            fields.token_bytes += statistics.token_bytes;
            fields.arena_used_bytes += statistics.arena_used_bytes;
            fields.arena_reserved_bytes += statistics.arena_reserved_bytes;
            fields.metadata_reserved_bytes += statistics.metadata_reserved_bytes;
            if (fields.live_payloads > fields.live_payload_peak) {
                fields.live_payload_peak = fields.live_payloads;
            }
            if (fields.retained_bytes > fields.retained_bytes_peak) {
                fields.retained_bytes_peak = fields.retained_bytes;
            }
            if (fields.storage_bytes > fields.storage_bytes_peak) {
                fields.storage_bytes_peak = fields.storage_bytes;
            }
            if (fields.token_bytes > fields.token_bytes_peak) {
                fields.token_bytes_peak = fields.token_bytes;
            }
            if (fields.arena_used_bytes > fields.arena_used_bytes_peak) {
                fields.arena_used_bytes_peak = fields.arena_used_bytes;
            }
            if (fields.arena_reserved_bytes > fields.arena_reserved_bytes_peak) {
                fields.arena_reserved_bytes_peak = fields.arena_reserved_bytes;
            }
            if (fields.metadata_reserved_bytes > fields.metadata_reserved_bytes_peak) {
                fields.metadata_reserved_bytes_peak = fields.metadata_reserved_bytes;
            }
        }
    }

    auto complete_source(ref<str>              domain,
                         ref<str>              key,
                         const SharedLoadCell& cell,
                         const LoadResult&     result) const -> void {
        auto fields       = state_->lock().unwrap_unchecked();
        auto resolved_key = cache_key(domain, key);
        complete(*fields, fields->source_entries, resolved_key.as_str(), cell, result, false);
    }

    auto complete_identity(ref<str> key, const SharedLoadCell& cell, const LoadResult& result) const
        -> void {
        auto fields = state_->lock().unwrap_unchecked();
        complete(*fields, fields->identity_entries, key, cell, result, true);
    }

    SharedState state_;
};

class FrontendService {
public:
    static auto make(Option<FrontendObserver> observer = None()) -> FrontendService {
        return FrontendService { FrontendSourceStore::make(), rstd::move(observer), None() };
    }

    static auto with_store(const FrontendSourceStore&       store,
                           Option<FrontendObserver>         observer   = None(),
                           Option<FrontendHeaderClassifier> classifier = None())
        -> FrontendService {
        return FrontendService { store.clone(), rstd::move(observer), rstd::move(classifier) };
    }

    auto load(ref<rstd::path::Path> path, preprocessor::SourceLoadRole role)
        -> lexical::Result<lexical::SharedScanFileStorage> {
        ++statistics_.source_requests;
        if (path.to_str().is_none()) {
            return Err(
                lexical::Error::make(rstd::format("source path '{}' is not valid UTF-8", path)));
        }

        auto source_activity = observe(FrontendActivity::SourceResolve);
        auto canonical       = rstd::fs::canonicalize(path);
        if (canonical.is_err()) {
            return Err(lexical::Error::make(rstd::format(
                "cannot resolve source '{}': {}", path, rstd::move(canonical).unwrap_err())));
        }
        auto canonical_path = rstd::move(canonical).unwrap();
        if (role == preprocessor::SourceLoadRole::Primary) {
            auto loaded = read_source(canonical_path.as_path());
            return lexical_result(loaded);
        }
        auto classification = classifier_.is_some() ? classifier_->resolve(canonical_path.as_path())
                                                    : HeaderCacheClassification {};
        auto domain         = classification.retention_domain.is_some()
                                  ? classification.retention_domain->as_str()
                                  : ""_str;
        auto canonical_text = canonical_path.as_path().to_str();
        if (canonical_text.is_none()) {
            return Err(lexical::Error::make(rstd::format(
                "canonical source path '{}' is not valid UTF-8", canonical_path.as_path())));
        }
        auto canonical_key = String::make(*canonical_text);
        auto source_entry  = store_.source(domain, canonical_key.as_str());
        if (source_entry.existing) ++statistics_.source_hits;
        auto source_cell    = rstd::move(source_entry.cell);
        auto source_waiting = source_entry.existing && source_cell->get().is_none();
        auto source_started = rstd::time::Instant::now();
        auto initialized    = false;
        auto stored         = source_cell->get_or_init([&]() -> FrontendSourceStore::LoadResult {
            initialized   = true;
            auto metadata = rstd::fs::metadata(canonical_path.as_path());
            if (metadata.is_err()) {
                return share_error(
                    lexical::Error::make(rstd::format("cannot inspect source '{}': {}",
                                                      canonical_path.as_path(),
                                                      rstd::move(metadata).unwrap_err())));
            }
            ++statistics_.source_stats;
            if (! metadata->is_file()) {
                return share_error(lexical::Error::make(
                    rstd::format("source '{}' is not a file", canonical_path.as_path())));
            }
            auto modified = metadata->modified();
            if (modified.is_err()) {
                return share_error(lexical::Error::make(
                    rstd::format("cannot read source modification time '{}': {}",
                                 canonical_path.as_path(),
                                 rstd::move(modified).unwrap_err())));
            }
            auto timestamp      = modified->as_unix_time();
            auto identity       = rstd::format("{}:{}:{}:{}:{}",
                                               metadata->dev(),
                                               metadata->ino(),
                                               metadata->size(),
                                               timestamp.seconds,
                                               timestamp.nanoseconds);
            auto identity_entry = store_.identity(domain, identity.as_str());
            if (identity_entry.existing) ++statistics_.source_hits;
            auto identity_cell        = rstd::move(identity_entry.cell);
            auto identity_waiting     = identity_entry.existing && identity_cell->get().is_none();
            auto identity_started     = rstd::time::Instant::now();
            auto identity_initialized = false;
            auto identity_result =
                identity_cell->get_or_init([&]() -> FrontendSourceStore::LoadResult {
                    identity_initialized = true;
                    return read_source(canonical_path.as_path());
                });
            if (identity_waiting && ! identity_initialized) {
                ++statistics_.source_waits;
                statistics_.source_wait =
                    statistics_.source_wait.saturating_add(identity_started.elapsed());
            }
            if (! identity_initialized && ! identity_entry.existing) ++statistics_.source_hits;
            if (! identity_entry.existing) {
                store_.complete_identity(identity.as_str(), identity_cell, *identity_result);
            }
            return clone_load_result(*identity_result);
        });
        if (source_waiting && ! initialized) {
            ++statistics_.source_waits;
            statistics_.source_wait =
                statistics_.source_wait.saturating_add(source_started.elapsed());
        }
        if (! initialized && ! source_entry.existing) ++statistics_.source_hits;
        if (! source_entry.existing) {
            store_.complete_source(domain, canonical_key.as_str(), source_cell, *stored);
        }
        return lexical_result(*stored);
    }

    auto statistics() const noexcept -> const FrontendStatistics& { return statistics_; }

    auto record_analysis_build() noexcept -> void { ++statistics_.analyze_builds; }

    auto record_analysis_hit() noexcept -> void { ++statistics_.analyze_hits; }

    auto
    record_preprocessor_statistics(const preprocessor::PreprocessorStatistics& statistics) noexcept
        -> void {
        statistics_.preprocessor.add(statistics);
    }

    auto release_source_cache() -> void { store_.release(); }

    auto source_store() const -> FrontendSourceStore { return store_.clone(); }

private:
    explicit FrontendService(FrontendSourceStore              store,
                             Option<FrontendObserver>         observer,
                             Option<FrontendHeaderClassifier> classifier)
        : store_(rstd::move(store)),
          observer_(rstd::move(observer)),
          classifier_(rstd::move(classifier)) {}

    static auto clone_error(const FrontendSourceStore::SharedLoadError& error) -> lexical::Error {
        return lexical::Error {
            .message  = error->message.clone(),
            .location = as<Clone>(error->location).clone(),
            .path     = as<Clone>(error->path).clone(),
        };
    }

    static auto share_error(lexical::Error error) -> FrontendSourceStore::LoadResult {
        return Err(rstd::sync::Arc<lexical::Error>::make(rstd::move(error)));
    }

    static auto clone_load_result(const FrontendSourceStore::LoadResult& value)
        -> FrontendSourceStore::LoadResult {
        auto borrowed = value.as_ref();
        if (borrowed.is_err()) return Err(borrowed.unwrap_err_unchecked().clone());
        return Ok(borrowed.unwrap_unchecked().clone());
    }

    static auto lexical_result(const FrontendSourceStore::LoadResult& value)
        -> lexical::Result<lexical::SharedScanFileStorage> {
        auto borrowed = value.as_ref();
        if (borrowed.is_err()) return Err(clone_error(borrowed.unwrap_err_unchecked()));
        return Ok(borrowed.unwrap_unchecked().clone());
    }

    auto read_source(ref<rstd::path::Path> path) -> FrontendSourceStore::LoadResult {
        auto contents = [&] {
            auto activity = observe(FrontendActivity::SourceRead);
            return rstd::fs::read_to_string(path);
        }();
        if (contents.is_err()) {
            return share_error(lexical::Error::make(rstd::format(
                "cannot read source '{}': {}", path, rstd::move(contents).unwrap_err())));
        }
        ++statistics_.source_reads;
        statistics_.source_bytes += contents->len();
        auto snapshot = lexical::make_source_snapshot(lexical::SourceBuffer {
            .path     = rstd::path::PathBuf::from(path),
            .contents = rstd::move(contents).unwrap(),
        });
        auto source   = lexical::SourceFile { .snapshot = snapshot.clone() };
        auto lexed    = [&] {
            auto activity = observe(FrontendActivity::Lex);
            return lexical::lex_scan_file(source);
        }();
        if (lexed.is_err()) return share_error(rstd::move(lexed).unwrap_err());
        ++statistics_.lex_builds;
        return Ok(rstd::sync::Arc<lexical::ScanFileStorage>::make(rstd::move(lexed).unwrap()));
    }

    auto observe(FrontendActivity activity) noexcept -> FrontendActivityGuard {
        if (observer_.is_none()) return FrontendActivityGuard {};
        return FrontendActivityGuard { *observer_, activity };
    }

    FrontendSourceStore              store_;
    Option<FrontendObserver>         observer_;
    Option<FrontendHeaderClassifier> classifier_;
    FrontendStatistics               statistics_;
};

} // namespace lito::frontend
