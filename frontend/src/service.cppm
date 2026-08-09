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
    usize                                lex_builds {};
    usize                                analyze_builds {};
    usize                                analyze_hits {};
    usize                                documentation_builds {};
    usize                                documentation_declarations {};
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
    preprocessor::PreprocessorStatistics preprocessor;
};

class FrontendService {
public:
    static auto make(Option<FrontendObserver> observer = None()) -> FrontendService {
        return FrontendService { rstd::move(observer) };
    }

    auto load(ref<rstd::path::Path> path) -> lexical::Result<lexical::SharedLexedSource> {
        ++statistics_.source_requests;
        auto requested = path.to_str();
        if (requested.is_none()) {
            return Err(
                lexical::Error::make(rstd::format("source path '{}' is not valid UTF-8", path)));
        }
        auto cached = sources_.get(*requested);
        if (cached.is_some()) {
            ++statistics_.source_hits;
            return Ok((**cached).clone());
        }

        auto source_activity = observe(FrontendActivity::SourceResolve);

        auto canonical = rstd::fs::canonicalize(path);
        if (canonical.is_err()) {
            return Err(lexical::Error::make(rstd::format(
                "cannot resolve source '{}': {}", path, rstd::move(canonical).unwrap_err())));
        }
        auto canonical_text = canonical->as_path().to_str();
        if (canonical_text.is_none()) {
            return Err(lexical::Error::make(rstd::format(
                "canonical source path '{}' is not valid UTF-8", canonical->as_path())));
        }
        cached = sources_.get(*canonical_text);
        if (cached.is_some()) {
            ++statistics_.source_hits;
            auto shared = (**cached).clone();
            if (*requested != *canonical_text)
                sources_.insert(String::make(*requested), shared.clone());
            return Ok(rstd::move(shared));
        }

        auto metadata = rstd::fs::metadata(canonical->as_path());
        if (metadata.is_err()) {
            return Err(lexical::Error::make(rstd::format("cannot inspect source '{}': {}",
                                                         canonical->as_path(),
                                                         rstd::move(metadata).unwrap_err())));
        }
        ++statistics_.source_stats;
        if (! metadata->is_file()) {
            return Err(lexical::Error::make(
                rstd::format("source '{}' is not a file", canonical->as_path())));
        }
        auto modified = metadata->modified();
        if (modified.is_err()) {
            return Err(
                lexical::Error::make(rstd::format("cannot read source modification time '{}': {}",
                                                  canonical->as_path(),
                                                  rstd::move(modified).unwrap_err())));
        }
        auto timestamp = modified->as_unix_time();
        auto identity  = rstd::format("{}:{}:{}:{}:{}",
                                      metadata->dev(),
                                      metadata->ino(),
                                      metadata->size(),
                                      timestamp.seconds,
                                      timestamp.nanoseconds);
        auto same_file = identities_.get(identity.as_str());
        if (same_file.is_some()) {
            ++statistics_.source_hits;
            auto shared = (**same_file).clone();
            sources_.insert(String::make(*canonical_text), shared.clone());
            if (*requested != *canonical_text)
                sources_.insert(String::make(*requested), shared.clone());
            return Ok(rstd::move(shared));
        }

        auto contents = [&] {
            auto activity = observe(FrontendActivity::SourceRead);
            return rstd::fs::read_to_string(canonical->as_path());
        }();
        if (contents.is_err()) {
            return Err(lexical::Error::make(rstd::format("cannot read source '{}': {}",
                                                         canonical->as_path(),
                                                         rstd::move(contents).unwrap_err())));
        }
        ++statistics_.source_reads;
        statistics_.source_bytes += contents->len();
        auto snapshot = lexical::make_source_snapshot(lexical::SourceBuffer {
            .path     = rstd::move(canonical).unwrap(),
            .contents = rstd::move(contents).unwrap(),
        });
        auto source   = lexical::SourceFile { .snapshot = snapshot.clone() };
        auto lexed    = [&] {
            auto activity = observe(FrontendActivity::Lex);
            return lexical::lex_with_comments(source, true);
        }();
        if (lexed.is_err()) return Err(rstd::move(lexed).unwrap_err());
        ++statistics_.lex_builds;
        auto lexed_file = rstd::move(lexed).unwrap();
        auto shared =
            rstd::rc::make_rc<lexical::LexedSource>(lexical::LexedSource {
                                                        .snapshot = rstd::move(snapshot),
                                                        .tokens   = rstd::move(lexed_file.tokens),
                                                        .comments = rstd::move(lexed_file.comments),
                                                    })
                .to_const();
        auto stored_path = shared.get()->snapshot.get()->path.as_path().to_str();
        if (stored_path.is_none()) {
            return Err(lexical::Error::make("cached source path is not valid UTF-8"_str));
        }
        sources_.insert(String::make(*stored_path), shared.clone());
        identities_.insert(rstd::move(identity), shared.clone());
        if (*requested != *stored_path) sources_.insert(String::make(*requested), shared.clone());
        return Ok(rstd::move(shared));
    }

    auto statistics() const noexcept -> const FrontendStatistics& { return statistics_; }

    auto record_analysis_build() noexcept -> void { ++statistics_.analyze_builds; }

    auto record_analysis_hit() noexcept -> void { ++statistics_.analyze_hits; }

    auto record_documentation_build(usize declarations) noexcept -> void {
        ++statistics_.documentation_builds;
        statistics_.documentation_declarations += declarations;
    }

    auto
    record_preprocessor_statistics(const preprocessor::PreprocessorStatistics& statistics) noexcept
        -> void {
        statistics_.preprocessor.add(statistics);
    }

    auto release_source_cache() -> void {
        sources_    = rstd::collections::HashMap<String, lexical::SharedLexedSource>::make();
        identities_ = rstd::collections::HashMap<String, lexical::SharedLexedSource>::make();
    }

private:
    explicit FrontendService(Option<FrontendObserver> observer): observer_(rstd::move(observer)) {}

    auto observe(FrontendActivity activity) noexcept -> FrontendActivityGuard {
        if (observer_.is_none()) return FrontendActivityGuard {};
        return FrontendActivityGuard { *observer_, activity };
    }

    Option<FrontendObserver>                                       observer_;
    rstd::collections::HashMap<String, lexical::SharedLexedSource> sources_;
    rstd::collections::HashMap<String, lexical::SharedLexedSource> identities_;
    FrontendStatistics                                             statistics_;
};

} // namespace lito::frontend
