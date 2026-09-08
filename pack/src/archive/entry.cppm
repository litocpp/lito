export module lito.pack:archive.entry;

import rstd;

using namespace rstd::prelude;

export namespace lito::archive
{

enum class TarEntryKind : rstd::uint8_t
{
    Regular,
    Directory,
};

enum class TarFileMode : rstd::uint8_t
{
    Regular,
    Executable,
};

struct TarEntryHeader {
    Vec<u8>      path;
    TarEntryKind kind {};
    u32          mode {};
    u64          uid {};
    u64          gid {};
    u64          mtime {};
    u64          size {};
};

} // namespace lito::archive
