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
    usize                                source_in_flight_entries {};
    usize                                source_in_flight_peak {};
    usize                                source_weak_hits {};
    usize                                source_flight_waits {};
    usize                                source_expired_entries {};
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
        source_in_flight_entries += other.source_in_flight_entries;
        if (other.source_in_flight_peak > source_in_flight_peak) {
            source_in_flight_peak = other.source_in_flight_peak;
        }
        source_weak_hits += other.source_weak_hits;
        source_flight_waits += other.source_flight_waits;
        source_expired_entries += other.source_expired_entries;
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
    usize in_flight_entries {};
    usize in_flight_peak {};
    usize weak_hits {};
    usize flight_waits {};
    usize expired_entries {};
};

class FrontendSourceStore {
    using SharedLoadError = rstd::sync::Arc<lexical::Error>;
    using LoadResult      = Result<lexical::SharedLexedSource, SharedLoadError>;
    using LoadCell        = rstd::sync::OnceLock<LoadResult>;
    using SharedLoadCell  = rstd::sync::Arc<LoadCell>;
    using WeakLexedSource = rstd::sync::Weak<lexical::LexedSource>;
    using ReadyMap        = rstd::collections::HashMap<String, WeakLexedSource>;
    using FlightMap       = rstd::collections::HashMap<String, SharedLoadCell>;

    struct Fields {
        ReadyMap  source_ready;
        ReadyMap  identity_ready;
        FlightMap source_flights;
        FlightMap identity_flights;
        usize     ready_peak {};
        usize     live_payload_peak {};
        usize     retained_bytes_peak {};
        usize     in_flight_peak {};
        usize     weak_hits {};
        usize     flight_waits {};
        usize     expired_entries {};

        Fields()
            : source_ready(ReadyMap::make()),
              identity_ready(ReadyMap::make()),
              source_flights(FlightMap::make()),
              identity_flights(FlightMap::make()) {}
    };

    using State       = rstd::sync::Mutex<Fields>;
    using SharedState = rstd::sync::Arc<State>;

    struct Entry {
        Option<lexical::SharedLexedSource> ready;
        Option<SharedLoadCell>             cell;
        bool                               existing {};
    };

public:
    static auto make() -> FrontendSourceStore {
        return FrontendSourceStore { SharedState::make(Fields {}) };
    }

    auto clone() const -> FrontendSourceStore { return FrontendSourceStore { state_.clone() }; }

    auto release() const -> void {
        auto fields              = state_->lock().unwrap_unchecked();
        fields->source_ready     = ReadyMap::make();
        fields->identity_ready   = ReadyMap::make();
        fields->source_flights   = FlightMap::make();
        fields->identity_flights = FlightMap::make();
    }

    auto statistics() const -> FrontendSourceStoreStatistics {
        auto fields         = state_->lock().unwrap_unchecked();
        auto ready          = usize {};
        auto live_payloads  = usize {};
        auto retained_bytes = usize {};
        auto count_ready    = [&](const ReadyMap& entries, bool payload_owner) {
            auto values = entries.values();
            for (auto value = values.next(); value.is_some(); value = values.next()) {
                auto source = (*value)->upgrade();
                if (! source) continue;
                ++ready;
                if (! payload_owner) continue;
                ++live_payloads;
                retained_bytes += source->retained_bytes();
            }
        };
        count_ready(fields->source_ready, true);
        count_ready(fields->identity_ready, false);
        return FrontendSourceStoreStatistics {
            .ready_entries       = ready,
            .ready_peak          = fields->ready_peak,
            .live_payloads       = live_payloads,
            .live_payload_peak   = fields->live_payload_peak,
            .retained_bytes      = retained_bytes,
            .retained_bytes_peak = fields->retained_bytes_peak,
            .in_flight_entries   = fields->source_flights.len() + fields->identity_flights.len(),
            .in_flight_peak      = fields->in_flight_peak,
            .weak_hits           = fields->weak_hits,
            .flight_waits        = fields->flight_waits,
            .expired_entries     = fields->expired_entries,
        };
    }

private:
    friend class FrontendService;

    explicit FrontendSourceStore(SharedState state): state_(rstd::move(state)) {}

    auto entry(Fields& fields, ReadyMap& ready, FlightMap& flights, ref<str> key) const -> Entry {
        auto found_ready = ready.get(key);
        if (found_ready.is_some()) {
            auto value = (**found_ready).upgrade();
            if (value) {
                ++fields.weak_hits;
                return Entry { .ready = Some(rstd::move(value)) };
            }
            (void)ready.remove(key);
            ++fields.expired_entries;
        }
        auto found_flight = flights.get(key);
        if (found_flight.is_some()) {
            ++fields.flight_waits;
            return Entry { .cell = Some((**found_flight).clone()), .existing = true };
        }
        auto cell = SharedLoadCell::make();
        flights.insert(String::make(key), cell.clone());
        auto in_flight = fields.source_flights.len() + fields.identity_flights.len();
        if (in_flight > fields.in_flight_peak) fields.in_flight_peak = in_flight;
        return Entry { .cell = Some(rstd::move(cell)) };
    }

    auto source(ref<str> key) const -> Entry {
        auto fields = state_->lock().unwrap_unchecked();
        return entry(*fields, fields->source_ready, fields->source_flights, key);
    }

    auto identity(ref<str> key) const -> Entry {
        auto fields = state_->lock().unwrap_unchecked();
        return entry(*fields, fields->identity_ready, fields->identity_flights, key);
    }

    auto prune_ready(Fields& fields) const -> void {
        auto limit = fields.in_flight_peak * usize(2) + usize(64);
        if (fields.source_ready.len() + fields.identity_ready.len() <= limit) return;
        fields.source_ready.retain([&](const String&, WeakLexedSource& value) {
            if (! value.expired()) return true;
            ++fields.expired_entries;
            return false;
        });
        fields.identity_ready.retain([&](const String&, WeakLexedSource& value) {
            if (! value.expired()) return true;
            ++fields.expired_entries;
            return false;
        });
    }

    auto update_live_peak(Fields& fields) const -> void {
        auto live_payloads  = usize {};
        auto retained_bytes = usize {};
        auto values         = fields.source_ready.values();
        for (auto value = values.next(); value.is_some(); value = values.next()) {
            auto source = (*value)->upgrade();
            if (! source) continue;
            ++live_payloads;
            retained_bytes += source->retained_bytes();
        }
        if (live_payloads > fields.live_payload_peak) fields.live_payload_peak = live_payloads;
        if (retained_bytes > fields.retained_bytes_peak) {
            fields.retained_bytes_peak = retained_bytes;
        }
    }

    auto complete(Fields&               fields,
                  ReadyMap&             ready,
                  FlightMap&            flights,
                  ref<str>              key,
                  const SharedLoadCell& cell,
                  const LoadResult&     result) const -> void {
        auto current = flights.get(key);
        if (current.is_some() && SharedLoadCell::ptr_eq(**current, cell)) {
            auto value = result.as_ref();
            if (value.is_ok()) {
                auto source = value.unwrap_unchecked().clone();
                ready.insert(String::make(key), source.downgrade());
                auto ready_entries = fields.source_ready.len() + fields.identity_ready.len();
                if (ready_entries > fields.ready_peak) fields.ready_peak = ready_entries;
            }
            (void)flights.remove(key);
            prune_ready(fields);
            update_live_peak(fields);
        }
    }

    auto complete_source(ref<str> key, const SharedLoadCell& cell, const LoadResult& result) const
        -> void {
        auto fields = state_->lock().unwrap_unchecked();
        complete(*fields, fields->source_ready, fields->source_flights, key, cell, result);
    }

    auto complete_identity(ref<str> key, const SharedLoadCell& cell, const LoadResult& result) const
        -> void {
        auto fields = state_->lock().unwrap_unchecked();
        complete(*fields, fields->identity_ready, fields->identity_flights, key, cell, result);
    }

    SharedState state_;
};

class FrontendService {
public:
    static auto make(Option<FrontendObserver> observer = None()) -> FrontendService {
        return FrontendService { FrontendSourceStore::make(), rstd::move(observer) };
    }

    static auto with_store(const FrontendSourceStore& store,
                           Option<FrontendObserver>   observer = None()) -> FrontendService {
        return FrontendService { store.clone(), rstd::move(observer) };
    }

    auto load(ref<rstd::path::Path> path) -> lexical::Result<lexical::SharedLexedSource> {
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
        auto canonical_text = canonical->as_path().to_str();
        if (canonical_text.is_none()) {
            return Err(lexical::Error::make(rstd::format(
                "canonical source path '{}' is not valid UTF-8", canonical->as_path())));
        }
        auto canonical_key  = String::make(*canonical_text);
        auto canonical_path = rstd::move(canonical).unwrap();
        auto source_entry   = store_.source(canonical_key.as_str());
        if (source_entry.ready.is_some()) {
            ++statistics_.source_hits;
            return Ok(rstd::move(source_entry.ready).unwrap());
        }
        if (source_entry.existing) ++statistics_.source_hits;
        auto source_cell    = rstd::move(source_entry.cell).unwrap();
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
            auto identity_entry = store_.identity(identity.as_str());
            if (identity_entry.ready.is_some()) {
                ++statistics_.source_hits;
                return Ok(rstd::move(identity_entry.ready).unwrap());
            }
            if (identity_entry.existing) ++statistics_.source_hits;
            auto identity_cell        = rstd::move(identity_entry.cell).unwrap();
            auto identity_waiting     = identity_entry.existing && identity_cell->get().is_none();
            auto identity_started     = rstd::time::Instant::now();
            auto identity_initialized = false;
            auto identity_result =
                identity_cell->get_or_init([&]() -> FrontendSourceStore::LoadResult {
                    identity_initialized = true;
                    auto contents        = [&] {
                        auto activity = observe(FrontendActivity::SourceRead);
                        return rstd::fs::read_to_string(canonical_path.as_path());
                    }();
                    if (contents.is_err()) {
                        return share_error(
                            lexical::Error::make(rstd::format("cannot read source '{}': {}",
                                                              canonical_path.as_path(),
                                                              rstd::move(contents).unwrap_err())));
                    }
                    ++statistics_.source_reads;
                    statistics_.source_bytes += contents->len();
                    auto snapshot = lexical::make_source_snapshot(lexical::SourceBuffer {
                        .path     = canonical_path.clone(),
                        .contents = rstd::move(contents).unwrap(),
                    });
                    auto source   = lexical::SourceFile { .snapshot = snapshot.clone() };
                    auto lexed    = [&] {
                        auto activity = observe(FrontendActivity::Lex);
                        return lexical::lex_with_comments(source, true);
                    }();
                    if (lexed.is_err()) return share_error(rstd::move(lexed).unwrap_err());
                    ++statistics_.lex_builds;
                    auto file = rstd::move(lexed).unwrap();
                    return Ok(rstd::sync::Arc<lexical::LexedSource>::make(lexical::LexedSource {
                        .snapshot = rstd::move(snapshot),
                        .tokens   = rstd::move(file.tokens),
                        .comments = rstd::move(file.comments),
                    }));
                });
            if (identity_waiting && ! identity_initialized) {
                ++statistics_.source_waits;
                statistics_.source_wait =
                    statistics_.source_wait.saturating_add(identity_started.elapsed());
            }
            if (! identity_initialized && ! identity_entry.existing) ++statistics_.source_hits;
            store_.complete_identity(identity.as_str(), identity_cell, *identity_result);
            return clone_load_result(*identity_result);
        });
        if (source_waiting && ! initialized) {
            ++statistics_.source_waits;
            statistics_.source_wait =
                statistics_.source_wait.saturating_add(source_started.elapsed());
        }
        if (! initialized && ! source_entry.existing) ++statistics_.source_hits;
        store_.complete_source(canonical_key.as_str(), source_cell, *stored);
        if (stored->is_err()) {
            auto value = stored->as_ref();
            return Err(clone_error(value.unwrap_err_unchecked()));
        }
        auto value = stored->as_ref();
        return Ok(value.unwrap_unchecked().clone());
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
    explicit FrontendService(FrontendSourceStore store, Option<FrontendObserver> observer)
        : store_(rstd::move(store)), observer_(rstd::move(observer)) {}

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

    auto observe(FrontendActivity activity) noexcept -> FrontendActivityGuard {
        if (observer_.is_none()) return FrontendActivityGuard {};
        return FrontendActivityGuard { *observer_, activity };
    }

    FrontendSourceStore      store_;
    Option<FrontendObserver> observer_;
    FrontendStatistics       statistics_;
};

} // namespace lito::frontend
