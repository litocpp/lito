export module lito.frontend.lexical:source;

import rstd;
import :token;

using namespace rstd::prelude;

export namespace lito::frontend::lexical
{

struct SourceBuffer {
    rstd::path::PathBuf path;
    String              contents;
};

struct SourceSnapshot {
    rstd::path::PathBuf path;
    String              contents;
};

using SharedSourceSnapshot = rstd::sync::Arc<SourceSnapshot>;

auto make_source_snapshot(SourceBuffer buffer) -> SharedSourceSnapshot {
    return rstd::sync::Arc<SourceSnapshot>::make(SourceSnapshot {
        .path     = rstd::move(buffer.path),
        .contents = rstd::move(buffer.contents),
    });
}

struct SourceFile {
    SourceId             id {};
    SharedSourceSnapshot snapshot;

    static auto make(SourceId id, SourceBuffer buffer) -> SourceFile {
        return SourceFile { .id = id, .snapshot = make_source_snapshot(rstd::move(buffer)) };
    }

    auto path() const -> ref<rstd::path::Path> { return snapshot->path.as_path(); }

    auto contents() const -> ref<str> { return snapshot->contents.as_str(); }
};

enum class CommentKind
{
    Ordinary,
    OuterDocumentation,
    InnerDocumentation,
};

enum class CommentStyle
{
    Line,
    Block,
};

struct CommentTrivia {
    CommentKind    kind { CommentKind::Ordinary };
    CommentStyle   style { CommentStyle::Line };
    TokenText      text;
    SourceLocation begin;
    SourceLocation end;
    bool           start_of_line { false };

    auto clone() const -> CommentTrivia {
        return CommentTrivia {
            .kind          = kind,
            .style         = style,
            .text          = text.shared_clone(),
            .begin         = begin,
            .end           = end,
            .start_of_line = start_of_line,
        };
    }
};

struct LexedFile {
    Vec<Token>         tokens;
    Vec<CommentTrivia> comments;
};

struct LexedSource {
    SharedSourceSnapshot snapshot;
    Vec<Token>           tokens;
    Vec<CommentTrivia>   comments;

    auto retained_bytes() const noexcept -> usize {
        return snapshot->contents.capacity() + tokens.capacity() * usize(sizeof(Token)) +
               comments.capacity() * usize(sizeof(CommentTrivia));
    }
};

using SharedLexedSource = rstd::sync::Arc<LexedSource>;

class SourceManager {
public:
    static auto make() -> SourceManager { return SourceManager {}; }

    auto add(SourceBuffer buffer) -> SourceId {
        return add(make_source_snapshot(rstd::move(buffer)));
    }

    auto add(SharedSourceSnapshot snapshot) -> SourceId {
        auto id = files_.len();
        files_.push(SourceFile {
            .id       = id,
            .snapshot = rstd::move(snapshot),
        });
        return id;
    }

    auto file(SourceId id) const -> const SourceFile& { return files_[id]; }
    auto snapshot(SourceId id) const -> SharedSourceSnapshot { return files_[id].snapshot.clone(); }
    auto path(SourceId id) const -> ref<rstd::path::Path> { return files_[id].path(); }
    auto len() const noexcept -> usize { return files_.len(); }

private:
    Vec<SourceFile> files_;
};

} // namespace lito::frontend::lexical
