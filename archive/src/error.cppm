export module lito.archive:error;

import rstd;

using namespace rstd::prelude;

export namespace lito::archive
{

enum class ArchiveErrorKind : rstd::uint8_t
{
    Io,
    Zstd,
    Tar,
    State,
    Limit,
};

struct ArchiveError {
    ArchiveErrorKind kind {};
    String           message;
};

template<typename T>
using ArchiveResult = Result<T, ArchiveError>;

} // namespace lito::archive
