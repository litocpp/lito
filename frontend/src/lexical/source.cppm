export module tenon.frontend.lexical:source;

import rstd;
import :token;

using namespace rstd::prelude;

export namespace tenon::frontend::lexical {

struct SourceBuffer {
  rstd::path::PathBuf path;
  String contents;
};

struct SourceSnapshot {
  rstd::path::PathBuf path;
  String contents;
};

using SharedSourceSnapshot = rstd::rc::Rc<const SourceSnapshot>;

auto make_source_snapshot(SourceBuffer buffer) -> SharedSourceSnapshot {
  return rstd::rc::make_rc<SourceSnapshot>(SourceSnapshot{
             .path = rstd::move(buffer.path),
             .contents = rstd::move(buffer.contents),
         })
      .to_const();
}

struct SourceFile {
  SourceId id{};
  SharedSourceSnapshot snapshot;

  static auto make(SourceId id, SourceBuffer buffer) -> SourceFile {
    return SourceFile{.id = id,
                      .snapshot = make_source_snapshot(rstd::move(buffer))};
  }

  auto path() const -> ref<rstd::path::Path> {
    return snapshot.get()->path.as_path();
  }

  auto contents() const -> ref<str> { return snapshot.get()->contents.as_str(); }
};

struct LexedSource {
  SharedSourceSnapshot snapshot;
  Vec<Token> tokens;
};

using SharedLexedSource = rstd::rc::Rc<const LexedSource>;

class SourceManager {
public:
  static auto make() -> SourceManager { return SourceManager{}; }

  auto add(SourceBuffer buffer) -> SourceId {
    return add(make_source_snapshot(rstd::move(buffer)));
  }

  auto add(SharedSourceSnapshot snapshot) -> SourceId {
    auto id = files_.len();
    files_.push(SourceFile{
        .id = id,
        .snapshot = rstd::move(snapshot),
    });
    return id;
  }

  auto file(SourceId id) const -> const SourceFile & { return files_[id]; }
  auto path(SourceId id) const -> ref<rstd::path::Path> {
    return files_[id].path();
  }
  auto len() const noexcept -> usize { return files_.len(); }

private:
  Vec<SourceFile> files_;
};

} // namespace tenon::frontend::lexical
