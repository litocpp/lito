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
    usize                                lex_builds {};
    usize                                analyze_builds {};
    usize                                analyze_hits {};
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
        lex_builds += other.lex_builds;
        analyze_builds += other.analyze_builds;
        analyze_hits += other.analyze_hits;
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

class FrontendSourceStore {
    using SharedLoadError = rstd::sync::Arc<lexical::Error>;
    using LoadResult      = Result<lexical::SharedLexedSource, SharedLoadError>;
    using LoadCell        = rstd::sync::OnceLock<LoadResult>;
    using SharedLoadCell  = rstd::sync::Arc<LoadCell>;

    struct Fields {
        rstd::collections::HashMap<String, SharedLoadCell> sources;
        rstd::collections::HashMap<String, SharedLoadCell> identities;

        Fields()
            : sources(rstd::collections::HashMap<String, SharedLoadCell>::make()),
              identities(rstd::collections::HashMap<String, SharedLoadCell>::make()) {}
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

    auto release() const -> void {
        auto fields        = state_->lock().unwrap_unchecked();
        fields->sources    = rstd::collections::HashMap<String, SharedLoadCell>::make();
        fields->identities = rstd::collections::HashMap<String, SharedLoadCell>::make();
    }

private:
    friend class FrontendService;

    explicit FrontendSourceStore(SharedState state): state_(rstd::move(state)) {}

    auto entry(rstd::collections::HashMap<String, SharedLoadCell>& entries, ref<str> key) const
        -> Entry {
        auto found = entries.get(key);
        if (found.is_some()) return Entry { .cell = (**found).clone(), .existing = true };
        auto cell = SharedLoadCell::make();
        entries.insert(String::make(key), cell.clone());
        return Entry { .cell = rstd::move(cell) };
    }

    auto source(ref<str> key) const -> Entry {
        auto fields = state_->lock().unwrap_unchecked();
        return entry(fields->sources, key);
    }

    auto identity(ref<str> key) const -> Entry {
        auto fields = state_->lock().unwrap_unchecked();
        return entry(fields->identities, key);
    }

    auto remove_failed(rstd::collections::HashMap<String, SharedLoadCell>& entries,
                       ref<str>                                            key,
                       const SharedLoadCell&                               cell) const -> void {
        auto current = entries.get(key);
        if (current.is_some() && SharedLoadCell::ptr_eq(**current, cell)) {
            (void)entries.remove(key);
        }
    }

    auto remove_failed_source(ref<str> key, const SharedLoadCell& cell) const -> void {
        auto fields = state_->lock().unwrap_unchecked();
        remove_failed(fields->sources, key, cell);
    }

    auto remove_failed_identity(ref<str> key, const SharedLoadCell& cell) const -> void {
        auto fields = state_->lock().unwrap_unchecked();
        remove_failed(fields->identities, key, cell);
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
        if (source_entry.existing) ++statistics_.source_hits;
        auto source_waiting = source_entry.existing && source_entry.cell->get().is_none();
        auto source_started = rstd::time::Instant::now();
        auto initialized    = false;
        auto stored         = source_entry.cell->get_or_init([&] {
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
            if (identity_entry.existing) ++statistics_.source_hits;
            auto identity_waiting = identity_entry.existing && identity_entry.cell->get().is_none();
            auto identity_started = rstd::time::Instant::now();
            auto identity_initialized = false;
            auto identity_result =
                identity_entry.cell->get_or_init([&]() -> FrontendSourceStore::LoadResult {
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
            if (identity_result->is_err()) {
                store_.remove_failed_identity(identity.as_str(), identity_entry.cell);
            }
            return clone_load_result(*identity_result);
        });
        if (source_waiting && ! initialized) {
            ++statistics_.source_waits;
            statistics_.source_wait =
                statistics_.source_wait.saturating_add(source_started.elapsed());
        }
        if (! initialized && ! source_entry.existing) ++statistics_.source_hits;
        if (stored->is_err()) {
            store_.remove_failed_source(canonical_key.as_str(), source_entry.cell);
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
