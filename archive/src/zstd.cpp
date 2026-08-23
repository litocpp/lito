module;
#include <zstd.h>

module lito.archive;

import rstd;
import :zstd;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace lito::archive
{

auto zstd_error(ref<str> operation, size_t status) -> ArchiveError {
    return ArchiveError {
        .kind    = ArchiveErrorKind::Zstd,
        .message = rstd::format("{}: {}", operation, ZSTD_getErrorName(status)),
    };
}

auto io_error(ref<str> operation, const auto& error) -> ArchiveError {
    return ArchiveError {
        .kind    = ArchiveErrorKind::Io,
        .message = rstd::format("{}: {}", operation, error),
    };
}

auto state_error(ref<str> message) -> ArchiveError {
    return ArchiveError {
        .kind    = ArchiveErrorKind::State,
        .message = String::make(message),
    };
}

ZstdReader::ZstdReader(rstd::fs::File file, void* context, u64 maximum_decoded_size) noexcept
    : file_(rstd::move(file)), context_(context), maximum_decoded_size_(maximum_decoded_size) {
}

ZstdReader::ZstdReader(ZstdReader&& other) noexcept
    : file_(rstd::move(other.file_)),
      context_(other.context_),
      input_(other.input_),
      input_position_(other.input_position_),
      input_size_(other.input_size_),
      decoded_size_(other.decoded_size_),
      maximum_decoded_size_(other.maximum_decoded_size_),
      source_eof_(other.source_eof_),
      frame_finished_(other.frame_finished_) {
    other.context_        = nullptr;
    other.frame_finished_ = true;
}

auto ZstdReader::operator=(ZstdReader&& other) noexcept -> ZstdReader& {
    if (this == &other) return *this;
    if (context_ != nullptr) ZSTD_freeDCtx(static_cast<ZSTD_DCtx*>(context_));
    file_                 = rstd::move(other.file_);
    context_              = other.context_;
    input_                = other.input_;
    input_position_       = other.input_position_;
    input_size_           = other.input_size_;
    decoded_size_         = other.decoded_size_;
    maximum_decoded_size_ = other.maximum_decoded_size_;
    source_eof_           = other.source_eof_;
    frame_finished_       = other.frame_finished_;
    other.context_        = nullptr;
    other.frame_finished_ = true;
    return *this;
}

ZstdReader::~ZstdReader() {
    if (context_ != nullptr) ZSTD_freeDCtx(static_cast<ZSTD_DCtx*>(context_));
}

auto ZstdReader::open(ref<rstd::path::Path> path, u64 maximum_decoded_size)
    -> ArchiveResult<ZstdReader> {
    if (maximum_decoded_size == u64 {}) {
        return Err(state_error("zstd decoded size limit must be greater than zero"_str));
    }
    auto opened = rstd::fs::File::open(path);
    if (opened.is_err()) {
        return Err(io_error("cannot open zstd input"_str, opened.as_ref().unwrap_err()));
    }
    auto context = ZSTD_createDCtx();
    if (context == nullptr) {
        return Err(ArchiveError {
            .kind    = ArchiveErrorKind::Zstd,
            .message = String::make("cannot allocate zstd decoder"_str),
        });
    }
    auto configured = ZSTD_DCtx_setParameter(context, ZSTD_d_windowLogMax, 27);
    if (ZSTD_isError(configured)) {
        auto error = zstd_error("cannot configure zstd decoder"_str, configured);
        ZSTD_freeDCtx(context);
        return Err(rstd::move(error));
    }
    return Ok(ZstdReader(rstd::move(opened).unwrap(), context, maximum_decoded_size));
}

auto ZstdReader::verify_frame_end() -> ArchiveResult<empty> {
    if (input_position_ != input_size_) {
        return Err(ArchiveError {
            .kind    = ArchiveErrorKind::Zstd,
            .message = String::make("zstd input contains a second frame or trailing bytes"_str),
        });
    }
    auto trailing = array<u8, 1> {};
    auto read     = file_.read(trailing.as_mut_slice());
    if (read.is_err()) {
        return Err(io_error("cannot verify zstd input end"_str, read.as_ref().unwrap_err()));
    }
    if (*read != usize {}) {
        return Err(ArchiveError {
            .kind    = ArchiveErrorKind::Zstd,
            .message = String::make("zstd input contains a second frame or trailing bytes"_str),
        });
    }
    source_eof_ = true;
    return Ok(empty {});
}

auto ZstdReader::read(mut_ref<u8[]> output) -> ArchiveResult<usize> {
    if (output.is_empty() || frame_finished_) return Ok(usize {});
    auto decoded = ZSTD_outBuffer {
        .dst  = output.as_raw_ptr(),
        .size = output.len().to_primitive(),
        .pos  = 0,
    };
    while (decoded.pos != decoded.size) {
        if (input_position_ == input_size_ && ! source_eof_) {
            auto read = file_.read(input_.as_mut_slice());
            if (read.is_err()) {
                return Err(io_error("cannot read zstd input"_str, read.as_ref().unwrap_err()));
            }
            input_position_ = usize {};
            input_size_     = *read;
            source_eof_     = *read == usize {};
        }
        if (input_position_ == input_size_ && source_eof_) {
            return Err(ArchiveError {
                .kind    = ArchiveErrorKind::Zstd,
                .message = String::make("zstd frame is truncated"_str),
            });
        }
        auto encoded = ZSTD_inBuffer {
            .src  = input_.as_ptr().as_raw_ptr() + input_position_.to_primitive(),
            .size = (input_size_ - input_position_).to_primitive(),
            .pos  = 0,
        };
        auto before = decoded.pos;
        auto status = ZSTD_decompressStream(static_cast<ZSTD_DCtx*>(context_), &decoded, &encoded);
        if (ZSTD_isError(status)) {
            return Err(zstd_error("cannot decompress zstd frame"_str, status));
        }
        input_position_ += usize(encoded.pos);
        if (status == 0) {
            frame_finished_ = true;
            auto verified   = verify_frame_end();
            if (verified.is_err()) return Err(rstd::move(verified).unwrap_err());
            break;
        }
        if (decoded.pos == before && encoded.pos == 0 && source_eof_) {
            return Err(ArchiveError {
                .kind    = ArchiveErrorKind::Zstd,
                .message = String::make("zstd decoder made no progress"_str),
            });
        }
    }
    auto produced = u64(static_cast<unsigned long long>(decoded.pos));
    if (produced > maximum_decoded_size_ - decoded_size_) {
        return Err(ArchiveError {
            .kind    = ArchiveErrorKind::Limit,
            .message = String::make("zstd stream exceeds the decoded size limit"_str),
        });
    }
    decoded_size_ += produced;
    return Ok(usize(decoded.pos));
}

auto ZstdReader::finish() -> ArchiveResult<empty> {
    auto discard = array<u8, 8192> {};
    while (! frame_finished_) {
        auto read = this->read(discard.as_mut_slice());
        if (read.is_err()) return Err(rstd::move(read).unwrap_err());
        if (*read == usize {} && ! frame_finished_) {
            return Err(state_error("zstd reader did not reach frame end"_str));
        }
    }
    return Ok(empty {});
}

ZstdWriter::ZstdWriter(rstd::fs::File file, void* context) noexcept
    : file_(rstd::move(file)), context_(context) {
}

ZstdWriter::ZstdWriter(ZstdWriter&& other) noexcept
    : file_(rstd::move(other.file_)), context_(other.context_), finished_(other.finished_) {
    other.context_  = nullptr;
    other.finished_ = true;
}

auto ZstdWriter::operator=(ZstdWriter&& other) noexcept -> ZstdWriter& {
    if (this == &other) return *this;
    if (context_ != nullptr) ZSTD_freeCCtx(static_cast<ZSTD_CCtx*>(context_));
    file_           = rstd::move(other.file_);
    context_        = other.context_;
    finished_       = other.finished_;
    other.context_  = nullptr;
    other.finished_ = true;
    return *this;
}

ZstdWriter::~ZstdWriter() {
    if (context_ != nullptr) ZSTD_freeCCtx(static_cast<ZSTD_CCtx*>(context_));
}

auto ZstdWriter::create(ref<rstd::path::Path> path) -> ArchiveResult<ZstdWriter> {
    auto opened = rstd::fs::File::create(path);
    if (opened.is_err()) {
        return Err(io_error("cannot create zstd output"_str, opened.as_ref().unwrap_err()));
    }
    auto context = ZSTD_createCCtx();
    if (context == nullptr) {
        return Err(ArchiveError {
            .kind    = ArchiveErrorKind::Zstd,
            .message = String::make("cannot allocate zstd encoder"_str),
        });
    }
    auto configure = [&](ZSTD_cParameter parameter, int value) -> ArchiveResult<empty> {
        auto status = ZSTD_CCtx_setParameter(context, parameter, value);
        if (ZSTD_isError(status)) {
            return Err(zstd_error("cannot configure zstd encoder"_str, status));
        }
        return Ok(empty {});
    };
    auto level = configure(ZSTD_c_compressionLevel, 19);
    if (level.is_err()) {
        auto error = rstd::move(level).unwrap_err();
        ZSTD_freeCCtx(context);
        return Err(rstd::move(error));
    }
    auto workers = configure(ZSTD_c_nbWorkers, 1);
    if (workers.is_err()) {
        auto error = rstd::move(workers).unwrap_err();
        ZSTD_freeCCtx(context);
        return Err(rstd::move(error));
    }
    auto checksum = configure(ZSTD_c_checksumFlag, 1);
    if (checksum.is_err()) {
        auto error = rstd::move(checksum).unwrap_err();
        ZSTD_freeCCtx(context);
        return Err(rstd::move(error));
    }
    return Ok(ZstdWriter(rstd::move(opened).unwrap(), context));
}

auto ZstdWriter::write(slice<u8> input) -> ArchiveResult<empty> {
    if (finished_) return Err(state_error("cannot write a finished zstd stream"_str));
    auto encoded = ZSTD_inBuffer {
        .src  = input.as_raw_ptr(),
        .size = input.len().to_primitive(),
        .pos  = 0,
    };
    auto output = array<u8, 131072> {};
    while (encoded.pos != encoded.size) {
        auto compressed = ZSTD_outBuffer {
            .dst  = output.as_mut_ptr().as_raw_ptr(),
            .size = output.len().to_primitive(),
            .pos  = 0,
        };
        auto before = encoded.pos;
        auto status = ZSTD_compressStream2(
            static_cast<ZSTD_CCtx*>(context_), &compressed, &encoded, ZSTD_e_continue);
        if (ZSTD_isError(status)) {
            return Err(zstd_error("cannot compress zstd frame"_str, status));
        }
        if (compressed.pos != 0) {
            auto written = file_.write_all(
                slice<u8>::from_raw_parts(output.as_ptr().as_raw_ptr(), usize(compressed.pos)));
            if (written.is_err()) {
                return Err(io_error("cannot write zstd output"_str, written.as_ref().unwrap_err()));
            }
        }
        if (encoded.pos == before && compressed.pos == 0) {
            return Err(state_error("zstd encoder made no progress"_str));
        }
    }
    return Ok(empty {});
}

auto ZstdWriter::finish() -> ArchiveResult<empty> {
    if (finished_) return Ok(empty {});
    auto output = array<u8, 131072> {};
    auto input  = ZSTD_inBuffer { .src = nullptr, .size = 0, .pos = 0 };
    while (true) {
        auto compressed = ZSTD_outBuffer {
            .dst  = output.as_mut_ptr().as_raw_ptr(),
            .size = output.len().to_primitive(),
            .pos  = 0,
        };
        auto status = ZSTD_compressStream2(
            static_cast<ZSTD_CCtx*>(context_), &compressed, &input, ZSTD_e_end);
        if (ZSTD_isError(status)) {
            return Err(zstd_error("cannot finish zstd frame"_str, status));
        }
        if (compressed.pos != 0) {
            auto written = file_.write_all(
                slice<u8>::from_raw_parts(output.as_ptr().as_raw_ptr(), usize(compressed.pos)));
            if (written.is_err()) {
                return Err(
                    io_error("cannot write zstd trailer"_str, written.as_ref().unwrap_err()));
            }
        }
        if (status == 0) break;
        if (compressed.pos == 0) {
            return Err(state_error("zstd encoder made no progress while finishing"_str));
        }
    }
    auto flushed = file_.flush();
    if (flushed.is_err()) {
        return Err(io_error("cannot flush zstd output"_str, flushed.as_ref().unwrap_err()));
    }
    finished_ = true;
    file_     = rstd::fs::File {};
    return Ok(empty {});
}

} // namespace lito::archive
