export module lito.frontend.lexical:source;

import rstd;
import lito.frontend.memory;
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

enum class CommentKind : uint8_t
{
    Ordinary,
    OuterDocumentation,
    InnerDocumentation,
};

enum class CommentStyle : uint8_t
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

enum class SourceTokenFlag : uint8_t
{
    StartOfLine  = 1,
    LeadingSpace = 2,
};

struct CompactSourceToken {
    uint32_t  offset;
    uint32_t  length;
    uint64_t  comparable_hash;
    TokenKind kind;
    uint8_t   flags;
};

static_assert(sizeof(CompactSourceToken) <= 24);
static_assert(rstd::mtp::triv_drop<CompactSourceToken>);

struct CompactSourceComment {
    uint32_t     begin;
    uint32_t     end;
    CommentKind  kind;
    CommentStyle style;
    bool         start_of_line;
};

class SourcePositionIndex {
public:
    explicit SourcePositionIndex(ref<str> contents) {
        line_starts_.push(uint32_t {});
        auto bytes = contents.as_bytes();
        for (auto index = usize {}; index < bytes.len(); ++index) {
            if (bytes[index] != u8('\r') && bytes[index] != u8('\n')) continue;
            if (bytes[index] == u8('\r') && index + usize(1) < bytes.len() &&
                bytes[index + usize(1)] == u8('\n')) {
                ++index;
            }
            line_starts_.push(static_cast<uint32_t>((index + usize(1)).to_primitive()));
        }
    }

    auto location(SourceId source, uint32_t offset) const noexcept -> SourceLocation {
        auto lower = usize {};
        auto upper = line_starts_.len();
        while (lower < upper) {
            auto middle = lower + (upper - lower) / usize(2);
            if (line_starts_[middle] <= offset) {
                lower = middle + usize(1);
            } else {
                upper = middle;
            }
        }
        auto line_index = lower - usize(1);
        auto line_start = line_starts_[line_index];
        return SourceLocation {
            .source = source,
            .offset = usize(static_cast<rstd::size_t>(offset)),
            .line   = line_index + usize(1),
            .column = usize(static_cast<rstd::size_t>(offset - line_start)) + usize(1),
        };
    }

    auto retained_bytes() const noexcept -> usize {
        return line_starts_.capacity() * usize(sizeof(uint32_t));
    }

    auto used_bytes() const noexcept -> usize {
        return line_starts_.len() * usize(sizeof(uint32_t));
    }

private:
    Vec<uint32_t> line_starts_;
};

class SourceTokenStorage {
    static constexpr usize BLOCK_CAPACITY = usize(128);
    using Arena                           = alloc::BumpArena<ScanMemoryAllocator>;

    struct Block {
        mut_ptr<CompactSourceToken> pointer;
        usize                       length;
    };

public:
    void push(CompactSourceToken token, Arena& arena) {
        if (blocks_.is_empty() || blocks_[blocks_.len() - usize(1)].length == BLOCK_CAPACITY) {
            auto allocator = arena.allocator();
            auto layout = rstd::alloc::Layout::array<CompactSourceToken>(BLOCK_CAPACITY).unwrap();
            auto allocation = as<rstd::alloc::Allocator>(allocator).allocate(layout);
            if (allocation.is_err()) alloc::handle_alloc_error(layout);
            blocks_.push(Block {
                .pointer = allocation.unwrap_unchecked().as_mut_ptr<CompactSourceToken>(),
                .length  = usize {},
            });
        }

        auto& block = blocks_[blocks_.len() - usize(1)];
        rstd::ptr_::construct(block.pointer.add(block.length), rstd::move(token));
        ++block.length;
        ++length_;
    }

    auto operator[](usize index) const noexcept -> const CompactSourceToken& {
        auto block = index / BLOCK_CAPACITY;
        auto slot  = index % BLOCK_CAPACITY;
        return blocks_[block].pointer.as_ptr().add(slot).get();
    }

    auto len() const noexcept -> usize { return length_; }
    auto is_empty() const noexcept -> bool { return length_ == usize {}; }

    auto used_bytes() const noexcept -> usize { return blocks_.len() * usize(sizeof(Block)); }

    auto retained_bytes() const noexcept -> usize {
        return blocks_.capacity() * usize(sizeof(Block));
    }

private:
    Vec<Block> blocks_;
    usize      length_ {};
};

struct ScanFileStorageStatistics {
    usize source_bytes;
    usize source_reserved_bytes;
    usize token_count;
    usize token_bytes;
    usize arena_used_bytes;
    usize arena_reserved_bytes;
    usize metadata_used_bytes;
    usize metadata_reserved_bytes;
    usize retained_bytes;
};

class SourceTokenView {
public:
    auto kind() const noexcept -> TokenKind { return token_->kind; }
    auto offset() const noexcept -> usize {
        return usize(static_cast<rstd::size_t>(token_->offset));
    }
    auto text() const noexcept -> ref<str> {
        auto begin = usize(static_cast<rstd::size_t>(token_->offset));
        auto end   = begin + usize(static_cast<rstd::size_t>(token_->length));
        return contents_.get(begin, end).unwrap();
    }
    auto location() const noexcept -> SourceLocation {
        return positions_->location(source_, token_->offset);
    }
    auto start_of_line() const noexcept -> bool {
        return (token_->flags & static_cast<uint8_t>(SourceTokenFlag::StartOfLine)) != 0;
    }
    auto leading_space() const noexcept -> bool {
        return (token_->flags & static_cast<uint8_t>(SourceTokenFlag::LeadingSpace)) != 0;
    }
    auto materialize() const -> Token {
        auto origin = location();
        return Token {
            .kind          = kind(),
            .text          = TokenText::borrowed(text(), token_->comparable_hash),
            .spelling      = origin,
            .expansion     = origin,
            .start_of_line = start_of_line(),
            .leading_space = leading_space(),
        };
    }

private:
    SourceTokenView(const CompactSourceToken&  token,
                    ref<str>                   contents,
                    const SourcePositionIndex& positions,
                    SourceId                   source) noexcept
        : token_(&token), contents_(contents), positions_(&positions), source_(source) {}

    const CompactSourceToken*  token_;
    ref<str>                   contents_;
    const SourcePositionIndex* positions_;
    SourceId                   source_;

    friend struct ScanFileStorage;
};

struct ScanFileStorage {
    using Arena = alloc::BumpArena<ScanMemoryAllocator>;

    SharedSourceSnapshot      snapshot;
    Box<Arena>                arena;
    SourceTokenStorage        tokens;
    Vec<CompactSourceComment> comments;
    SourcePositionIndex       positions;

    ScanFileStorage(SharedSourceSnapshot snapshot, ScanMemoryAllocator allocator)
        : snapshot(rstd::move(snapshot)),
          arena(Box<Arena>::make(usize(16 * 1024), rstd::move(allocator))),
          tokens(),
          comments(),
          positions(this->snapshot->contents.as_str()) {}

    auto token(SourceId source, usize index) const noexcept -> SourceTokenView {
        return SourceTokenView(tokens[index], snapshot->contents.as_str(), positions, source);
    }

    auto comment(SourceId source, usize index) const -> CommentTrivia {
        const auto& value = comments[index];
        auto        begin = usize(static_cast<rstd::size_t>(value.begin));
        auto        end   = usize(static_cast<rstd::size_t>(value.end));
        return CommentTrivia {
            .kind  = value.kind,
            .style = value.style,
            .text  = TokenText::borrowed(snapshot->contents.as_str().get(begin, end).unwrap()),
            .begin = positions.location(source, value.begin),
            .end   = positions.location(source, value.end),
            .start_of_line = value.start_of_line,
        };
    }

    auto statistics() const noexcept -> ScanFileStorageStatistics {
        auto arena_statistics  = arena->stats();
        auto metadata_used     = tokens.used_bytes() +
                                 comments.len() * usize(sizeof(CompactSourceComment)) +
                                 positions.used_bytes();
        auto metadata_reserved = tokens.retained_bytes() +
                                 comments.capacity() * usize(sizeof(CompactSourceComment)) +
                                 positions.retained_bytes();
        auto source_reserved   = snapshot->contents.capacity();
        return ScanFileStorageStatistics {
            .source_bytes            = snapshot->contents.len(),
            .source_reserved_bytes   = source_reserved,
            .token_count             = tokens.len(),
            .token_bytes             = tokens.len() * usize(sizeof(CompactSourceToken)),
            .arena_used_bytes        = arena_statistics.used_bytes,
            .arena_reserved_bytes    = arena_statistics.reserved_bytes,
            .metadata_used_bytes     = metadata_used,
            .metadata_reserved_bytes = metadata_reserved,
            .retained_bytes = source_reserved + arena_statistics.reserved_bytes + metadata_reserved,
        };
    }

    auto retained_bytes() const noexcept -> usize { return statistics().retained_bytes; }
};

using SharedScanFileStorage = rstd::sync::Arc<ScanFileStorage>;

class ScanFileStorageBuilder {
public:
    ScanFileStorageBuilder(SharedSourceSnapshot snapshot, ScanMemoryAllocator allocator)
        : storage_(rstd::move(snapshot), rstd::move(allocator)) {}

    void
    push_token(TokenKind kind, usize offset, usize length, bool start_of_line, bool leading_space) {
        auto text  = storage_.snapshot->contents.as_str().get(offset, offset + length).unwrap();
        auto flags = uint8_t {};
        if (start_of_line) flags |= static_cast<uint8_t>(SourceTokenFlag::StartOfLine);
        if (leading_space) flags |= static_cast<uint8_t>(SourceTokenFlag::LeadingSpace);
        storage_.tokens.push(
            CompactSourceToken {
                .offset          = static_cast<uint32_t>(offset.to_primitive()),
                .length          = static_cast<uint32_t>(length.to_primitive()),
                .comparable_hash = comparable_name_hash(text),
                .kind            = kind,
                .flags           = flags,
            },
            *storage_.arena);
    }

    void
    push_comment(CommentKind kind, CommentStyle style, usize begin, usize end, bool start_of_line) {
        storage_.comments.push(CompactSourceComment {
            .begin         = static_cast<uint32_t>(begin.to_primitive()),
            .end           = static_cast<uint32_t>(end.to_primitive()),
            .kind          = kind,
            .style         = style,
            .start_of_line = start_of_line,
        });
    }

    auto finish() -> ScanFileStorage { return rstd::move(storage_); }

private:
    ScanFileStorage storage_;
};

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
