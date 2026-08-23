export module lito.archive:tar;

import rstd;
import :entry;
import :error;
import :zstd;

using namespace rstd::prelude;

namespace lito::archive
{

class TarReader {
    ZstdReader*     source_ {};
    u64             remaining_ {};
    usize           padding_ {};
    bool            entry_active_ {};
    bool            ended_ {};
    bool            header_is_pax_ {};
    bool            pax_pending_ {};
    Option<Vec<u8>> pax_path_;

    auto finish_entry() -> ArchiveResult<empty>;
    auto read_exact(mut_ref<u8[]> output) -> ArchiveResult<empty>;
    auto read_header() -> ArchiveResult<Option<TarEntryHeader>>;
    auto consume_pax(const TarEntryHeader& header) -> ArchiveResult<empty>;

public:
    explicit TarReader(ZstdReader& source) noexcept: source_(&source) {}

    auto next_entry() -> ArchiveResult<Option<TarEntryHeader>>;
    auto read_entry_data(mut_ref<u8[]> output) -> ArchiveResult<usize>;
    auto skip_entry_data() -> ArchiveResult<empty>;
    auto finish() -> ArchiveResult<empty>;
};

class TarWriter {
    ZstdWriter* sink_ {};
    bool        finished_ {};

    auto write_entry(slice<u8> path, TarEntryKind kind, u32 mode, slice<u8> contents)
        -> ArchiveResult<empty>;

public:
    explicit TarWriter(ZstdWriter& sink) noexcept: sink_(&sink) {}

    auto write_directory(slice<u8> path, u32 mode) -> ArchiveResult<empty>;
    auto write_file(slice<u8> path, u32 mode, slice<u8> contents) -> ArchiveResult<empty>;
    auto finish() -> ArchiveResult<empty>;
};

} // namespace lito::archive
