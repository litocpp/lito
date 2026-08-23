export module lito.archive:tar_zstd;

import rstd;
import :entry;
import :error;

using namespace rstd::prelude;

export namespace lito::archive
{

class TarZstdReader {
    void* implementation_ {};

    explicit TarZstdReader(void* implementation) noexcept: implementation_(implementation) {}

public:
    TarZstdReader(const TarZstdReader&)                    = delete;
    auto operator=(const TarZstdReader&) -> TarZstdReader& = delete;
    TarZstdReader(TarZstdReader&& other) noexcept;
    auto operator=(TarZstdReader&& other) noexcept -> TarZstdReader&;
    ~TarZstdReader();

    static auto open(ref<rstd::path::Path> path, u64 maximum_decoded_size)
        -> ArchiveResult<TarZstdReader>;
    auto next_entry() -> ArchiveResult<Option<TarEntryHeader>>;
    auto read_entry_data(mut_ref<u8[]> output) -> ArchiveResult<usize>;
    auto skip_entry_data() -> ArchiveResult<empty>;
    auto finish() -> ArchiveResult<empty>;
};

class TarZstdWriter {
    void* implementation_ {};

    explicit TarZstdWriter(void* implementation) noexcept: implementation_(implementation) {}

public:
    TarZstdWriter(const TarZstdWriter&)                    = delete;
    auto operator=(const TarZstdWriter&) -> TarZstdWriter& = delete;
    TarZstdWriter(TarZstdWriter&& other) noexcept;
    auto operator=(TarZstdWriter&& other) noexcept -> TarZstdWriter&;
    ~TarZstdWriter();

    static auto create(ref<rstd::path::Path> path) -> ArchiveResult<TarZstdWriter>;
    auto        write_directory(slice<u8> path, u32 mode) -> ArchiveResult<empty>;
    auto        write_file(slice<u8> path, u32 mode, slice<u8> contents) -> ArchiveResult<empty>;
    auto        finish() -> ArchiveResult<empty>;
};

} // namespace lito::archive
