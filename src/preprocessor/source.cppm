export module tenon.preprocessor:source;

import rstd;
import :token;

using namespace rstd::prelude;

export namespace tenon::preprocessor {

struct SourceBuffer {
  rstd::path::PathBuf path;
  String contents;
};

struct SourceFile {
  SourceId id{};
  rstd::path::PathBuf path;
  String contents;
};

class SourceManager {
public:
  static auto make() -> SourceManager { return SourceManager{}; }

  auto add(SourceBuffer buffer) -> SourceId {
    auto id = files_.len();
    files_.push(SourceFile{
        .id = id,
        .path = rstd::move(buffer.path),
        .contents = rstd::move(buffer.contents),
    });
    return id;
  }

  auto file(SourceId id) const -> const SourceFile & { return files_[id]; }
  auto path(SourceId id) const -> ref<rstd::path::Path> {
    return files_[id].path.as_path();
  }
  auto len() const noexcept -> usize { return files_.len(); }

private:
  Vec<SourceFile> files_;
};

} // namespace tenon::preprocessor
