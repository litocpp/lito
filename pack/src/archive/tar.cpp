module;
#include <rstd/macro.hpp>

module lito.pack;

import rstd;
import :archive.tar;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace lito::archive
{

inline constexpr usize TAR_BLOCK_SIZE { 512 };
inline constexpr usize TAR_NAME_SIZE { 100 };
inline constexpr usize TAR_PREFIX_SIZE { 155 };
inline constexpr usize TAR_PAX_SIZE_LIMIT { 4096 };

auto tar_error(ref<str> message, ArchiveErrorKind kind = ArchiveErrorKind::Tar) -> ArchiveError {
    return ArchiveError {
        .kind    = kind,
        .message = String::make(message),
    };
}

auto tar_error(String message, ArchiveErrorKind kind = ArchiveErrorKind::Tar) -> ArchiveError {
    return ArchiveError { .kind = kind, .message = rstd::move(message) };
}

auto all_zero(slice<u8> bytes) noexcept -> bool {
    for (auto byte : bytes) {
        if (byte != u8 {}) return false;
    }
    return true;
}

auto field_bytes(slice<u8> block, usize offset, usize length) noexcept -> slice<u8> {
    return slice<u8>::from_raw_parts(block.as_raw_ptr() + offset.to_primitive(), length);
}

auto subslice(slice<u8> bytes, usize begin, usize end) noexcept -> slice<u8> {
    return slice<u8>::from_raw_parts(bytes.as_raw_ptr() + begin.to_primitive(), end - begin);
}

auto parse_octal(slice<u8> field, ref<str> name) -> ArchiveResult<u64> {
    auto value  = u64 {};
    auto digits = usize {};
    auto ended  = false;
    for (auto byte : field) {
        if (byte == u8 {} || byte == static_cast<u8>(' ')) {
            ended = digits != usize {};
            continue;
        }
        if (ended || byte < static_cast<u8>('0') || byte > static_cast<u8>('7')) {
            return Err(tar_error(rstd::format("tar {} field is not canonical octal", name)));
        }
        auto digit = u64((byte - u8('0')).to_primitive());
        if (value > (u64::MAX - digit) / u64(8)) {
            return Err(tar_error(rstd::format("tar {} field overflows", name)));
        }
        value = value * u64(8) + digit;
        ++digits;
    }
    if (digits == usize {}) {
        return Err(tar_error(rstd::format("tar {} field is empty", name)));
    }
    return Ok(value);
}

auto write_octal(mut_ref<u8[]> field, u64 value) noexcept -> bool {
    if (field.len() < usize(2)) return false;
    for (auto byte : field) byte = u8('0');
    field[field.len() - usize(1)] = u8 {};
    auto position                 = field.len() - usize(2);
    while (value != u64 {}) {
        field[position] = static_cast<u8>('0' + (value % u64(8)).to_primitive());
        value /= u64(8);
        if (value == u64 {}) break;
        if (position == usize {}) return false;
        --position;
    }
    return true;
}

auto write_checksum(mut_ref<u8[]> field, u64 value) noexcept -> bool {
    if (field.len() != usize(8)) return false;
    for (auto byte : field) byte = u8('0');
    field[usize(6)] = u8 {};
    field[usize(7)] = static_cast<u8>(' ');
    auto position   = usize(5);
    while (value != u64 {}) {
        field[position] = static_cast<u8>('0' + (value % u64(8)).to_primitive());
        value /= u64(8);
        if (value == u64 {}) break;
        if (position == usize {}) return false;
        --position;
    }
    return true;
}

auto copy_field(mut_ref<u8[]> destination, slice<u8> source) noexcept -> bool {
    if (source.len() > destination.len()) return false;
    auto index = usize {};
    for (auto byte : source) {
        destination[index] = byte;
        ++index;
    }
    return true;
}

auto field_value(slice<u8> field) -> Vec<u8> {
    auto length = usize {};
    while (length < field.len() && field[length] != u8 {}) ++length;
    return Vec<u8>::from(subslice(field, usize {}, length));
}

struct TarPathFields {
    Vec<u8> name;
    Vec<u8> prefix;
};

auto split_ustar_path(slice<u8> path) -> Option<TarPathFields> {
    if (path.len() <= TAR_NAME_SIZE) {
        return Some(TarPathFields { .name = Vec<u8>::from(path) });
    }
    auto split = path.len();
    while (split != usize {}) {
        --split;
        if (path[split] != static_cast<u8>('/')) continue;
        auto prefix_size = split;
        auto name_size   = path.len() - split - usize(1);
        if (prefix_size <= TAR_PREFIX_SIZE && name_size != usize {} && name_size <= TAR_NAME_SIZE) {
            return Some(TarPathFields {
                .name   = Vec<u8>::from(subslice(path, split + usize(1), path.len())),
                .prefix = Vec<u8>::from(subslice(path, usize {}, split)),
            });
        }
    }
    return None();
}

auto decimal_digits(usize value) noexcept -> usize {
    auto digits = usize(1);
    while (value >= usize(10)) {
        value /= usize(10);
        ++digits;
    }
    return digits;
}

auto append_decimal(Vec<u8>& output, usize value) -> void {
    auto divisor = usize(1);
    while (value / divisor >= usize(10)) divisor *= usize(10);
    while (true) {
        output.push(static_cast<u8>('0' + (value / divisor % usize(10)).to_primitive()));
        if (divisor == usize(1)) break;
        divisor /= usize(10);
    }
}

auto pax_path_record(slice<u8> path) -> Vec<u8> {
    auto length = path.len() + usize(7);
    while (true) {
        auto resolved = path.len() + usize(7) + decimal_digits(length);
        if (resolved == length) break;
        length = resolved;
    }
    auto record = Vec<u8>::with_capacity(length);
    append_decimal(record, length);
    record.push(static_cast<u8>(' '));
    record.extend_from_slice("path="_str.as_bytes());
    record.extend_from_slice(path);
    record.push(static_cast<u8>('\n'));
    return record;
}

auto zero_time_value(slice<u8> value) noexcept -> bool {
    if (value.is_empty()) return false;
    auto dot = false;
    for (auto byte : value) {
        if (byte == static_cast<u8>('.') && ! dot) {
            dot = true;
            continue;
        }
        if (byte != static_cast<u8>('0')) return false;
    }
    return true;
}

auto TarReader::read_exact(mut_ref<u8[]> output) -> ArchiveResult<empty> {
    auto offset = usize {};
    while (offset < output.len()) {
        auto remaining = mut_ref<u8[]>::from_raw_parts(output.as_raw_ptr() + offset.to_primitive(),
                                                       output.len() - offset);
        auto read      = source_->read(remaining);
        if (read.is_err()) return Err(rstd::move(read).unwrap_err());
        if (*read == usize {}) return Err(tar_error("tar stream is truncated"_str));
        offset += *read;
    }
    return Ok(empty {});
}

auto TarReader::finish_entry() -> ArchiveResult<empty> {
    if (! entry_active_) return Ok(empty {});
    if (remaining_ != u64 {}) {
        return Err(tar_error("tar entry data was not fully consumed"_str, ArchiveErrorKind::State));
    }
    if (padding_ != usize {}) {
        auto padding = array<u8, 512> {};
        auto target  = mut_ref<u8[]>::from_raw_parts(padding.as_mut_ptr().as_raw_ptr(), padding_);
        auto read    = read_exact(target);
        if (read.is_err()) return Err(rstd::move(read).unwrap_err());
        if (! all_zero(slice<u8>::from_raw_parts(padding.as_ptr().as_raw_ptr(), padding_))) {
            return Err(tar_error("tar entry padding is not zero"_str));
        }
    }
    padding_      = usize {};
    entry_active_ = false;
    return Ok(empty {});
}

auto TarReader::read_header() -> ArchiveResult<Option<TarEntryHeader>> {
    auto block = array<u8, 512> {};
    auto read  = read_exact(block.as_mut_slice());
    if (read.is_err()) return Err(rstd::move(read).unwrap_err());
    if (all_zero(block.as_slice())) {
        auto second = array<u8, 512> {};
        auto next   = read_exact(second.as_mut_slice());
        if (next.is_err()) return Err(rstd::move(next).unwrap_err());
        if (! all_zero(second.as_slice())) {
            return Err(tar_error("tar archive has only one end block"_str));
        }
        auto trailing = array<u8, 8192> {};
        while (true) {
            auto extra = source_->read(trailing.as_mut_slice());
            if (extra.is_err()) return Err(rstd::move(extra).unwrap_err());
            if (*extra == usize {}) break;
            if (! all_zero(slice<u8>::from_raw_parts(trailing.as_ptr().as_raw_ptr(), *extra))) {
                return Err(tar_error("tar archive has nonzero data after its end blocks"_str));
            }
        }
        if (pax_pending_) return Err(tar_error("tar archive ends after a local PAX header"_str));
        ended_ = true;
        return Ok(Option<TarEntryHeader> {});
    }

    auto magic            = field_bytes(block.as_slice(), usize(257), usize(6));
    auto version          = field_bytes(block.as_slice(), usize(263), usize(2));
    auto expected_magic   = "ustar\0"_str.as_bytes();
    auto expected_version = "00"_str.as_bytes();
    if (magic != expected_magic || version != expected_version) {
        return Err(tar_error("tar header is not POSIX ustar"_str));
    }

    auto stored_checksum =
        parse_octal(field_bytes(block.as_slice(), usize(148), usize(8)), "checksum"_str);
    if (stored_checksum.is_err()) return Err(rstd::move(stored_checksum).unwrap_err());
    auto checksum = u64 {};
    for (auto index = usize {}; index < block.len(); ++index) {
        checksum += index >= usize(148) && index < usize(156)
                        ? u64(static_cast<unsigned long long>(' '))
                        : u64(u8(block[index]).to_primitive());
    }
    if (checksum != *stored_checksum) return Err(tar_error("tar header checksum mismatch"_str));

    auto mode = parse_octal(field_bytes(block.as_slice(), usize(100), usize(8)), "mode"_str);
    if (mode.is_err()) return Err(rstd::move(mode).unwrap_err());
    if (*mode > u64(u32::MAX.to_primitive())) {
        return Err(tar_error("tar mode field overflows"_str));
    }
    auto uid = parse_octal(field_bytes(block.as_slice(), usize(108), usize(8)), "uid"_str);
    if (uid.is_err()) return Err(rstd::move(uid).unwrap_err());
    auto gid = parse_octal(field_bytes(block.as_slice(), usize(116), usize(8)), "gid"_str);
    if (gid.is_err()) return Err(rstd::move(gid).unwrap_err());
    auto size = parse_octal(field_bytes(block.as_slice(), usize(124), usize(12)), "size"_str);
    if (size.is_err()) return Err(rstd::move(size).unwrap_err());
    auto mtime = parse_octal(field_bytes(block.as_slice(), usize(136), usize(12)), "mtime"_str);
    if (mtime.is_err()) return Err(rstd::move(mtime).unwrap_err());

    auto type      = block[usize(156)];
    header_is_pax_ = type == static_cast<u8>('x');
    auto kind      = TarEntryKind::Regular;
    if (type == static_cast<u8>('5')) {
        kind = TarEntryKind::Directory;
    } else if (type != u8 {} && type != static_cast<u8>('0') && ! header_is_pax_) {
        return Err(tar_error("tar entry type is unsupported"_str));
    }
    if (! all_zero(field_bytes(block.as_slice(), usize(157), usize(100)))) {
        return Err(tar_error("tar links are unsupported"_str));
    }

    auto name   = field_value(field_bytes(block.as_slice(), usize {}, TAR_NAME_SIZE));
    auto prefix = field_value(field_bytes(block.as_slice(), usize(345), TAR_PREFIX_SIZE));
    auto path   = Vec<u8>::with_capacity(prefix.len() + name.len() + usize(1));
    if (! prefix.is_empty()) {
        path.extend_from_slice(prefix.as_slice());
        path.push(static_cast<u8>('/'));
    }
    path.extend_from_slice(name.as_slice());
    if (! header_is_pax_ && pax_path_.is_some()) path = rstd::move(pax_path_).unwrap();
    if (! header_is_pax_) {
        pax_path_    = None();
        pax_pending_ = false;
    }
    remaining_    = *size;
    padding_      = usize(((u64(512) - (*size % u64(512))) % u64(512)).to_primitive());
    entry_active_ = true;
    return Ok(Some(TarEntryHeader {
        .path  = rstd::move(path),
        .kind  = kind,
        .mode  = u32(mode->to_primitive()),
        .uid   = *uid,
        .gid   = *gid,
        .mtime = *mtime,
        .size  = *size,
    }));
}

auto TarReader::consume_pax(const TarEntryHeader& header) -> ArchiveResult<empty> {
    if (pax_pending_) return Err(tar_error("tar contains consecutive local PAX headers"_str));
    if (header.size > u64(TAR_PAX_SIZE_LIMIT.to_primitive())) {
        return Err(tar_error("tar local PAX header exceeds the metadata limit"_str,
                             ArchiveErrorKind::Limit));
    }
    auto contents = Vec<u8>::with_capacity(usize(header.size.to_primitive()));
    auto buffer   = array<u8, 4096> {};
    while (remaining_ != u64 {}) {
        auto wanted = remaining_ < u64(buffer.len().to_primitive())
                          ? usize(remaining_.to_primitive())
                          : buffer.len();
        auto target = mut_ref<u8[]>::from_raw_parts(buffer.as_mut_ptr().as_raw_ptr(), wanted);
        auto read   = read_entry_data(target);
        if (read.is_err()) return Err(rstd::move(read).unwrap_err());
        contents.extend_from_slice(slice<u8>::from_raw_parts(buffer.as_ptr().as_raw_ptr(), *read));
    }
    auto finished = finish_entry();
    if (finished.is_err()) return Err(rstd::move(finished).unwrap_err());

    auto offset     = usize {};
    auto atime_seen = false;
    auto ctime_seen = false;
    auto mtime_seen = false;
    while (offset < contents.len()) {
        auto length_end = offset;
        while (length_end < contents.len() && contents[length_end] >= static_cast<u8>('0') &&
               contents[length_end] <= static_cast<u8>('9')) {
            ++length_end;
        }
        if (length_end == offset || length_end >= contents.len() ||
            contents[length_end] != static_cast<u8>(' ')) {
            return Err(tar_error("tar local PAX record has an invalid length"_str));
        }
        auto record_length = usize {};
        for (auto index = offset; index < length_end; ++index) {
            auto digit = usize((u8(contents[index]) - u8('0')).to_primitive());
            if (record_length > (usize::MAX - digit) / usize(10)) {
                return Err(tar_error("tar local PAX record length overflows"_str));
            }
            record_length = record_length * usize(10) + digit;
        }
        if (record_length == usize {} || record_length > contents.len() - offset) {
            return Err(tar_error("tar local PAX record length is out of bounds"_str));
        }
        auto record_end = offset + record_length;
        if (contents[record_end - usize(1)] != static_cast<u8>('\n')) {
            return Err(tar_error("tar local PAX record is not newline terminated"_str));
        }
        auto payload_start = length_end + usize(1);
        auto equals        = payload_start;
        while (equals < record_end - usize(1) && contents[equals] != static_cast<u8>('=')) {
            ++equals;
        }
        if (equals == payload_start || equals == record_end - usize(1)) {
            return Err(tar_error("tar local PAX record has no key/value separator"_str));
        }
        auto bytes     = contents.as_slice();
        auto key       = subslice(bytes, payload_start, equals);
        auto value     = subslice(bytes, equals + usize(1), record_end - usize(1));
        auto path_key  = "path"_str.as_bytes();
        auto atime_key = "atime"_str.as_bytes();
        auto ctime_key = "ctime"_str.as_bytes();
        auto mtime_key = "mtime"_str.as_bytes();
        if (key == path_key) {
            if (pax_path_.is_some() || value.is_empty()) {
                return Err(tar_error("tar local PAX path is empty or repeated"_str));
            }
            pax_path_ = Some(Vec<u8>::from(value));
        } else if (key == atime_key || key == ctime_key || key == mtime_key) {
            auto repeated = key == atime_key   ? atime_seen
                            : key == ctime_key ? ctime_seen
                                               : mtime_seen;
            if (repeated) return Err(tar_error("tar local PAX time key is repeated"_str));
            if (! zero_time_value(value)) {
                return Err(tar_error("tar local PAX time must be zero"_str));
            }
            if (key == atime_key) atime_seen = true;
            if (key == ctime_key) ctime_seen = true;
            if (key == mtime_key) mtime_seen = true;
        } else {
            return Err(tar_error("tar local PAX key is unsupported"_str));
        }
        offset = record_end;
    }
    pax_pending_ = true;
    return Ok(empty {});
}

auto TarReader::next_entry() -> ArchiveResult<Option<TarEntryHeader>> {
    if (ended_) return Ok(Option<TarEntryHeader> {});
    auto completed = finish_entry();
    if (completed.is_err()) return Err(rstd::move(completed).unwrap_err());
    while (true) {
        auto header = read_header();
        if (header.is_err()) return Err(rstd::move(header).unwrap_err());
        if (header->is_none()) return Ok(Option<TarEntryHeader> {});
        if (! header_is_pax_) return Ok(Some(rstd::move(**header)));
        auto consumed = consume_pax(**header);
        if (consumed.is_err()) return Err(rstd::move(consumed).unwrap_err());
    }
}

auto TarReader::read_entry_data(mut_ref<u8[]> output) -> ArchiveResult<usize> {
    if (! entry_active_) {
        return Err(tar_error("tar reader has no active entry"_str, ArchiveErrorKind::State));
    }
    if (remaining_ == u64 {} || output.is_empty()) return Ok(usize {});
    auto wanted = remaining_ < u64(output.len().to_primitive()) ? usize(remaining_.to_primitive())
                                                                : output.len();
    auto target = mut_ref<u8[]>::from_raw_parts(output.as_raw_ptr(), wanted);
    auto read   = source_->read(target);
    if (read.is_err()) return Err(rstd::move(read).unwrap_err());
    if (*read == usize {}) return Err(tar_error("tar entry data is truncated"_str));
    remaining_ -= u64(read->to_primitive());
    return read;
}

auto TarReader::skip_entry_data() -> ArchiveResult<empty> {
    auto buffer = array<u8, 8192> {};
    while (remaining_ != u64 {}) {
        auto read = read_entry_data(buffer.as_mut_slice());
        if (read.is_err()) return Err(rstd::move(read).unwrap_err());
    }
    return finish_entry();
}

auto TarReader::finish() -> ArchiveResult<empty> {
    auto completed = finish_entry();
    if (completed.is_err()) return Err(rstd::move(completed).unwrap_err());
    if (! ended_) {
        auto next = next_entry();
        if (next.is_err()) return Err(rstd::move(next).unwrap_err());
        if (next->is_some()) {
            return Err(tar_error("tar reader was finished before all entries were consumed"_str,
                                 ArchiveErrorKind::State));
        }
    }
    return source_->finish();
}

auto write_tar_header(ZstdWriter&  sink,
                      slice<u8>    name,
                      slice<u8>    prefix,
                      TarEntryKind kind,
                      u32          mode,
                      u64          size,
                      u8           type_override = u8 {}) -> ArchiveResult<empty> {
    auto block = array<u8, 512> {};
    if (! copy_field(mut_ref<u8[]>::from_raw_parts(block.as_mut_ptr().as_raw_ptr(), TAR_NAME_SIZE),
                     name) ||
        ! copy_field(mut_ref<u8[]>::from_raw_parts(block.as_mut_ptr().as_raw_ptr() +
                                                       usize(345).to_primitive(),
                                                   TAR_PREFIX_SIZE),
                     prefix) ||
        ! write_octal(mut_ref<u8[]>::from_raw_parts(
                          block.as_mut_ptr().as_raw_ptr() + usize(100).to_primitive(), usize(8)),
                      u64(mode.to_primitive())) ||
        ! write_octal(mut_ref<u8[]>::from_raw_parts(
                          block.as_mut_ptr().as_raw_ptr() + usize(108).to_primitive(), usize(8)),
                      u64 {}) ||
        ! write_octal(mut_ref<u8[]>::from_raw_parts(
                          block.as_mut_ptr().as_raw_ptr() + usize(116).to_primitive(), usize(8)),
                      u64 {}) ||
        ! write_octal(mut_ref<u8[]>::from_raw_parts(
                          block.as_mut_ptr().as_raw_ptr() + usize(124).to_primitive(), usize(12)),
                      size) ||
        ! write_octal(mut_ref<u8[]>::from_raw_parts(
                          block.as_mut_ptr().as_raw_ptr() + usize(136).to_primitive(), usize(12)),
                      u64 {})) {
        return Err(tar_error("tar entry metadata cannot be represented by ustar"_str));
    }
    for (auto index = usize(148); index < usize(156); ++index) {
        block[index] = static_cast<u8>(' ');
    }
    block[usize(156)] = type_override != u8 {}            ? type_override
                        : kind == TarEntryKind::Directory ? static_cast<u8>('5')
                                                          : static_cast<u8>('0');
    auto magic        = "ustar\0"_str.as_bytes();
    auto version      = "00"_str.as_bytes();
    copy_field(mut_ref<u8[]>::from_raw_parts(
                   block.as_mut_ptr().as_raw_ptr() + usize(257).to_primitive(), usize(6)),
               magic);
    copy_field(mut_ref<u8[]>::from_raw_parts(
                   block.as_mut_ptr().as_raw_ptr() + usize(263).to_primitive(), usize(2)),
               version);
    auto checksum = u64 {};
    for (auto byte : block) checksum += u64(u8(byte).to_primitive());
    if (! write_checksum(mut_ref<u8[]>::from_raw_parts(
                             block.as_mut_ptr().as_raw_ptr() + usize(148).to_primitive(), usize(8)),
                         checksum)) {
        return Err(tar_error("tar header checksum cannot be represented"_str));
    }
    return sink.write(block.as_slice());
}

auto write_padding(ZstdWriter& sink, usize length) -> ArchiveResult<empty> {
    if (length == usize {}) return Ok(empty {});
    auto zero = array<u8, 512> {};
    return sink.write(slice<u8>::from_raw_parts(zero.as_ptr().as_raw_ptr(), length));
}

auto TarWriter::write_entry(slice<u8> path, TarEntryKind kind, u32 mode, slice<u8> contents)
    -> ArchiveResult<empty> {
    if (finished_) {
        return Err(tar_error("cannot write a finished tar archive"_str, ArchiveErrorKind::State));
    }
    if (path.is_empty()) return Err(tar_error("tar entry path is empty"_str));
    for (auto byte : path) {
        if (byte == u8 {}) return Err(tar_error("tar entry path contains a nul byte"_str));
    }
    auto fields = split_ustar_path(path);
    if (fields.is_none()) {
        auto record     = pax_path_record(path);
        auto pax_name   = "PaxHeaders/lito"_str.as_bytes();
        auto pax_header = write_tar_header(*sink_,
                                           pax_name,
                                           {},
                                           TarEntryKind::Regular,
                                           u32(0644),
                                           u64(record.len().to_primitive()),
                                           static_cast<u8>('x'));
        if (pax_header.is_err()) return Err(rstd::move(pax_header).unwrap_err());
        auto pax_data = sink_->write(record.as_slice());
        if (pax_data.is_err()) return Err(rstd::move(pax_data).unwrap_err());
        auto pax_padding =
            write_padding(*sink_, usize((usize(512) - record.len() % usize(512)) % usize(512)));
        if (pax_padding.is_err()) return Err(rstd::move(pax_padding).unwrap_err());
        fields = Some(TarPathFields {
            .name = Vec<u8>::from("PaxPath/lito"_str.as_bytes()),
        });
    }
    auto header = write_tar_header(
        *sink_,
        fields->name.as_slice(),
        fields->prefix.as_slice(),
        kind,
        mode,
        kind == TarEntryKind::Directory ? u64 {} : u64(contents.len().to_primitive()));
    if (header.is_err()) return Err(rstd::move(header).unwrap_err());
    if (kind == TarEntryKind::Regular && ! contents.is_empty()) {
        auto data = sink_->write(contents);
        if (data.is_err()) return Err(rstd::move(data).unwrap_err());
    }
    auto size = kind == TarEntryKind::Directory ? usize {} : contents.len();
    return write_padding(*sink_, (usize(512) - size % usize(512)) % usize(512));
}

auto TarWriter::write_directory(slice<u8> path, u32 mode) -> ArchiveResult<empty> {
    return write_entry(path, TarEntryKind::Directory, mode, {});
}

auto TarWriter::write_file(slice<u8> path, u32 mode, slice<u8> contents) -> ArchiveResult<empty> {
    return write_entry(path, TarEntryKind::Regular, mode, contents);
}

auto TarWriter::finish() -> ArchiveResult<empty> {
    if (finished_) return Ok(empty {});
    auto zero  = array<u8, 1024> {};
    auto wrote = sink_->write(zero.as_slice());
    if (wrote.is_err()) return Err(rstd::move(wrote).unwrap_err());
    finished_ = true;
    return Ok(empty {});
}

} // namespace lito::archive
