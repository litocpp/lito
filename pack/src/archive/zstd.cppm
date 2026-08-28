export module lito.pack:archive.zstd;

import rstd;
import :archive.error;

using namespace rstd::prelude;

namespace lito::archive
{

class ZstdReader {
    rstd::fs::File    file_;
    void*             context_ {};
    array<u8, 131072> input_ {};
    usize             input_position_ {};
    usize             input_size_ {};
    u64               decoded_size_ {};
    u64               maximum_decoded_size_ {};
    bool              source_eof_ {};
    bool              frame_finished_ {};

    ZstdReader(rstd::fs::File file, void* context, u64 maximum_decoded_size) noexcept;
    auto verify_frame_end() -> ArchiveResult<empty>;

public:
    ZstdReader(const ZstdReader&)                    = delete;
    auto operator=(const ZstdReader&) -> ZstdReader& = delete;
    ZstdReader(ZstdReader&& other) noexcept;
    auto operator=(ZstdReader&& other) noexcept -> ZstdReader&;
    ~ZstdReader();

    static auto open(ref<rstd::path::Path> path, u64 maximum_decoded_size)
        -> ArchiveResult<ZstdReader>;
    auto read(mut_ref<u8[]> output) -> ArchiveResult<usize>;
    auto finish() -> ArchiveResult<empty>;
};

class ZstdWriter {
    rstd::fs::File file_;
    void*          context_ {};
    bool           finished_ {};

    ZstdWriter(rstd::fs::File file, void* context) noexcept;

public:
    ZstdWriter(const ZstdWriter&)                    = delete;
    auto operator=(const ZstdWriter&) -> ZstdWriter& = delete;
    ZstdWriter(ZstdWriter&& other) noexcept;
    auto operator=(ZstdWriter&& other) noexcept -> ZstdWriter&;
    ~ZstdWriter();

    static auto create(ref<rstd::path::Path> path) -> ArchiveResult<ZstdWriter>;
    auto        write(slice<u8> input) -> ArchiveResult<empty>;
    auto        finish() -> ArchiveResult<empty>;
};

} // namespace lito::archive
