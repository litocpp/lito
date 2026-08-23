module lito.archive;

import rstd;
import :tar;
import :tar_zstd;
import :zstd;

using namespace rstd::prelude;

namespace lito::archive
{

struct TarZstdReaderImplementation {
    ZstdReader source;
    TarReader  tar;

    explicit TarZstdReaderImplementation(ZstdReader source)
        : source(rstd::move(source)), tar(this->source) {}
};

struct TarZstdWriterImplementation {
    ZstdWriter sink;
    TarWriter  tar;

    explicit TarZstdWriterImplementation(ZstdWriter sink)
        : sink(rstd::move(sink)), tar(this->sink) {}
};

TarZstdReader::TarZstdReader(TarZstdReader&& other) noexcept
    : implementation_(other.implementation_) {
    other.implementation_ = nullptr;
}

auto TarZstdReader::operator=(TarZstdReader&& other) noexcept -> TarZstdReader& {
    if (this == &other) return *this;
    delete static_cast<TarZstdReaderImplementation*>(implementation_);
    implementation_       = other.implementation_;
    other.implementation_ = nullptr;
    return *this;
}

TarZstdReader::~TarZstdReader() {
    delete static_cast<TarZstdReaderImplementation*>(implementation_);
}

auto TarZstdReader::open(ref<rstd::path::Path> path, u64 maximum_decoded_size)
    -> ArchiveResult<TarZstdReader> {
    auto source = ZstdReader::open(path, maximum_decoded_size);
    if (source.is_err()) return Err(rstd::move(source).unwrap_err());
    auto implementation = new TarZstdReaderImplementation(rstd::move(source).unwrap());
    return Ok(TarZstdReader(implementation));
}

auto TarZstdReader::next_entry() -> ArchiveResult<Option<TarEntryHeader>> {
    return static_cast<TarZstdReaderImplementation*>(implementation_)->tar.next_entry();
}

auto TarZstdReader::read_entry_data(mut_ref<u8[]> output) -> ArchiveResult<usize> {
    return static_cast<TarZstdReaderImplementation*>(implementation_)->tar.read_entry_data(output);
}

auto TarZstdReader::skip_entry_data() -> ArchiveResult<empty> {
    return static_cast<TarZstdReaderImplementation*>(implementation_)->tar.skip_entry_data();
}

auto TarZstdReader::finish() -> ArchiveResult<empty> {
    return static_cast<TarZstdReaderImplementation*>(implementation_)->tar.finish();
}

TarZstdWriter::TarZstdWriter(TarZstdWriter&& other) noexcept
    : implementation_(other.implementation_) {
    other.implementation_ = nullptr;
}

auto TarZstdWriter::operator=(TarZstdWriter&& other) noexcept -> TarZstdWriter& {
    if (this == &other) return *this;
    delete static_cast<TarZstdWriterImplementation*>(implementation_);
    implementation_       = other.implementation_;
    other.implementation_ = nullptr;
    return *this;
}

TarZstdWriter::~TarZstdWriter() {
    delete static_cast<TarZstdWriterImplementation*>(implementation_);
}

auto TarZstdWriter::create(ref<rstd::path::Path> path) -> ArchiveResult<TarZstdWriter> {
    auto sink = ZstdWriter::create(path);
    if (sink.is_err()) return Err(rstd::move(sink).unwrap_err());
    auto implementation = new TarZstdWriterImplementation(rstd::move(sink).unwrap());
    return Ok(TarZstdWriter(implementation));
}

auto TarZstdWriter::write_directory(slice<u8> path, u32 mode) -> ArchiveResult<empty> {
    return static_cast<TarZstdWriterImplementation*>(implementation_)
        ->tar.write_directory(path, mode);
}

auto TarZstdWriter::write_file(slice<u8> path, u32 mode, slice<u8> contents)
    -> ArchiveResult<empty> {
    return static_cast<TarZstdWriterImplementation*>(implementation_)
        ->tar.write_file(path, mode, contents);
}

auto TarZstdWriter::finish() -> ArchiveResult<empty> {
    auto implementation = static_cast<TarZstdWriterImplementation*>(implementation_);
    auto tar_finished   = implementation->tar.finish();
    if (tar_finished.is_err()) return Err(rstd::move(tar_finished).unwrap_err());
    return implementation->sink.finish();
}

} // namespace lito::archive
