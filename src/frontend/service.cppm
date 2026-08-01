export module tenon.frontend:service;

import rstd;
import tenon.frontend.lexical;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace tenon::frontend {

struct FrontendStatistics {
  usize source_requests{};
  usize source_hits{};
  usize source_stats{};
  usize source_reads{};
  usize source_bytes{};
  usize lex_builds{};
  usize analyze_builds{};
  usize analyze_hits{};
};

class FrontendService {
public:
  static auto make() -> FrontendService { return FrontendService{}; }

  auto load(ref<rstd::path::Path> path)
      -> lexical::Result<lexical::SharedLexedSource> {
    ++statistics_.source_requests;
    auto requested = path.to_str();
    if (requested.is_none()) {
      return Err(lexical::Error::make(
          rstd::format("source path '{}' is not valid UTF-8", path)));
    }
    auto cached = sources_.get(*requested);
    if (cached.is_some()) {
      ++statistics_.source_hits;
      return Ok((**cached).clone());
    }

    auto canonical = rstd::fs::canonicalize(path);
    if (canonical.is_err()) {
      return Err(lexical::Error::make(
          rstd::format("cannot resolve source '{}': {}", path,
                       rstd::move(canonical).unwrap_err())));
    }
    auto canonical_text = canonical->as_path().to_str();
    if (canonical_text.is_none()) {
      return Err(lexical::Error::make(rstd::format(
          "canonical source path '{}' is not valid UTF-8",
          canonical->as_path())));
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
      return Err(lexical::Error::make(rstd::format(
          "cannot inspect source '{}': {}", canonical->as_path(),
          rstd::move(metadata).unwrap_err())));
    }
    ++statistics_.source_stats;
    if (!metadata->is_file()) {
      return Err(lexical::Error::make(
          rstd::format("source '{}' is not a file", canonical->as_path())));
    }
    auto modified = metadata->modified();
    if (modified.is_err()) {
      return Err(lexical::Error::make(rstd::format(
          "cannot read source modification time '{}': {}",
          canonical->as_path(), rstd::move(modified).unwrap_err())));
    }
    auto timestamp = modified->as_unix_time();
    auto identity = rstd::format("{}:{}:{}:{}:{}", metadata->dev(),
                                 metadata->ino(), metadata->size(),
                                 timestamp.seconds, timestamp.nanoseconds);
    auto same_file = identities_.get(identity.as_str());
    if (same_file.is_some()) {
      ++statistics_.source_hits;
      auto shared = (**same_file).clone();
      sources_.insert(String::make(*canonical_text), shared.clone());
      if (*requested != *canonical_text)
        sources_.insert(String::make(*requested), shared.clone());
      return Ok(rstd::move(shared));
    }

    auto contents = rstd::fs::read_to_string(canonical->as_path());
    if (contents.is_err()) {
      return Err(lexical::Error::make(rstd::format(
          "cannot read source '{}': {}", canonical->as_path(),
          rstd::move(contents).unwrap_err())));
    }
    ++statistics_.source_reads;
    statistics_.source_bytes += contents->len();
    auto snapshot = lexical::make_source_snapshot(lexical::SourceBuffer{
        .path = rstd::move(canonical).unwrap(),
        .contents = rstd::move(contents).unwrap(),
    });
    auto source = lexical::SourceFile{.snapshot = snapshot.clone()};
    auto tokens = lexical::lex(source, true);
    if (tokens.is_err())
      return Err(rstd::move(tokens).unwrap_err());
    ++statistics_.lex_builds;
    auto shared = rstd::rc::make_rc<lexical::LexedSource>(
                      lexical::LexedSource{
                          .snapshot = rstd::move(snapshot),
                          .tokens = rstd::move(tokens).unwrap(),
                      })
                      .to_const();
    auto stored_path = shared.get()->snapshot.get()->path.as_path().to_str();
    if (stored_path.is_none()) {
      return Err(lexical::Error::make(
          "cached source path is not valid UTF-8"_str));
    }
    sources_.insert(String::make(*stored_path), shared.clone());
    identities_.insert(rstd::move(identity), shared.clone());
    if (*requested != *stored_path)
      sources_.insert(String::make(*requested), shared.clone());
    return Ok(rstd::move(shared));
  }

  auto statistics() const noexcept -> const FrontendStatistics & {
    return statistics_;
  }

  auto record_analysis_build() noexcept -> void {
    ++statistics_.analyze_builds;
  }

  auto record_analysis_hit() noexcept -> void { ++statistics_.analyze_hits; }

private:
  rstd::collections::HashMap<String, lexical::SharedLexedSource> sources_;
  rstd::collections::HashMap<String, lexical::SharedLexedSource> identities_;
  FrontendStatistics statistics_;
};

} // namespace tenon::frontend
