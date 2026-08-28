#include <rstd/test/gtest.hpp>
#include <rstd/macro.hpp>

import rstd;
import rstd.test;
import lito.pack;

using namespace rstd::prelude;
using namespace rstd::literals;
using PathBuf = rstd::path::PathBuf;

namespace
{

auto fixture_path(const rstd::test::TempDir& directory, ref<str> name) -> PathBuf {
    return PathBuf::from(directory.path()).join(PathBuf::from(name).as_path());
}

auto make_long_path() -> String {
    auto path = String::make();
    for (auto component = usize {}; component < usize(4); ++component) {
        for (auto character = usize {}; character < usize(60); ++character) {
            path.push_ascii(static_cast<char>('a' + component.to_primitive()));
        }
        path.push_ascii('/');
    }
    path.push_str("caf\u00e9.cpp"_str);
    return path;
}

auto make_large_contents() -> Vec<u8> {
    auto contents = Vec<u8>::with_capacity(usize(200000));
    for (auto index = usize {}; index < usize(200000); ++index) {
        contents.push(u8(index.to_primitive() % 251));
    }
    return contents;
}

auto write_fixture(ref<rstd::path::Path> path, ref<str> long_path, slice<u8> contents)
    -> lito::archive::ArchiveResult<empty> {
    auto writer = rstd_try(lito::archive::TarZstdWriter::create(path));
    rstd_try(writer.write_directory("package"_str.as_bytes(), u32(0755)));
    rstd_try(writer.write_file("package/empty"_str.as_bytes(), u32(0644), {}));
    rstd_try(writer.write_file(long_path.as_bytes(), u32(0755), contents));
    return writer.finish();
}

} // namespace

TEST(Archive, StreamsUstarPaxAndZstdRoundTrip) {
    auto temporary = rstd::test::TempDir::make();
    ASSERT_TRUE(temporary.is_ok());
    auto owner     = rstd::move(temporary).unwrap();
    auto archive   = fixture_path(owner, "fixture.tar.zst"_str);
    auto long_path = make_long_path();
    auto contents  = make_large_contents();
    ASSERT_TRUE(write_fixture(archive.as_path(), long_path.as_str(), contents.as_slice()).is_ok());

    auto opened = lito::archive::TarZstdReader::open(archive.as_path(), u64(300000));
    ASSERT_TRUE(opened.is_ok());
    auto reader = rstd::move(opened).unwrap();

    auto directory = reader.next_entry();
    ASSERT_TRUE(directory.is_ok());
    ASSERT_TRUE(directory->is_some());
    EXPECT_EQ((**directory).kind, lito::archive::TarEntryKind::Directory);
    EXPECT_EQ((**directory).path.as_slice(), "package"_str.as_bytes());
    ASSERT_TRUE(reader.skip_entry_data().is_ok());

    auto empty = reader.next_entry();
    ASSERT_TRUE(empty.is_ok());
    ASSERT_TRUE(empty->is_some());
    EXPECT_EQ((**empty).size, u64 {});
    EXPECT_EQ((**empty).path.as_slice(), "package/empty"_str.as_bytes());
    ASSERT_TRUE(reader.skip_entry_data().is_ok());

    auto file = reader.next_entry();
    ASSERT_TRUE(file.is_ok());
    ASSERT_TRUE(file->is_some());
    EXPECT_EQ((**file).path.as_slice(), long_path.as_str().as_bytes());
    EXPECT_EQ((**file).mode, u32(0755));
    EXPECT_EQ((**file).size, u64(contents.len().to_primitive()));
    EXPECT_TRUE(reader.next_entry().is_err());

    auto decoded = Vec<u8>::with_capacity(contents.len());
    auto buffer  = array<u8, 7000> {};
    while (decoded.len() < contents.len()) {
        auto read = reader.read_entry_data(buffer.as_mut_slice());
        ASSERT_TRUE(read.is_ok());
        ASSERT_NE(*read, usize {});
        decoded.extend_from_slice(slice<u8>::from_raw_parts(buffer.as_ptr().as_raw_ptr(), *read));
    }
    EXPECT_EQ(decoded.as_slice(), contents.as_slice());
    auto end = reader.next_entry();
    ASSERT_TRUE(end.is_ok());
    EXPECT_TRUE(end->is_none());
    EXPECT_TRUE(reader.finish().is_ok());
}

TEST(Archive, RejectsTrailingCompressedBytesAndDecodedLimit) {
    auto temporary = rstd::test::TempDir::make();
    ASSERT_TRUE(temporary.is_ok());
    auto owner     = rstd::move(temporary).unwrap();
    auto archive   = fixture_path(owner, "fixture.tar.zst"_str);
    auto long_path = make_long_path();
    auto contents  = make_large_contents();
    ASSERT_TRUE(write_fixture(archive.as_path(), long_path.as_str(), contents.as_slice()).is_ok());

    auto limited = lito::archive::TarZstdReader::open(archive.as_path(), u64(511));
    ASSERT_TRUE(limited.is_ok());
    EXPECT_TRUE(limited->next_entry().is_err());

    auto bytes = rstd::fs::read(archive.as_path());
    ASSERT_TRUE(bytes.is_ok());
    bytes->push(u8 {});
    auto trailing = fixture_path(owner, "trailing.tar.zst"_str);
    ASSERT_TRUE(rstd::fs::write(trailing.as_path(), bytes->as_slice()).is_ok());

    auto opened = lito::archive::TarZstdReader::open(trailing.as_path(), u64(300000));
    ASSERT_TRUE(opened.is_ok());
    auto reader = rstd::move(opened).unwrap();
    auto failed = false;
    while (! failed) {
        auto entry = reader.next_entry();
        if (entry.is_err()) {
            failed = true;
            break;
        }
        if (entry->is_none()) break;
        auto skipped = reader.skip_entry_data();
        if (skipped.is_err()) failed = true;
    }
    EXPECT_TRUE(failed);
}
