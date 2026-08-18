module;
#include <rstd/enum.hpp>

export module lito.toolchain.common:link;

import rstd;
import lito.core;
import lito.cpp;

using namespace rstd::prelude;

export namespace lito
{

enum class LinkArchiveMode
{
    Normal,
    Whole,
};

struct LinkArchive {
    PathBuf         path;
    LinkArchiveMode mode { LinkArchiveMode::Normal };
};

class ResolvedLinkInput {
    RSTD_ENUM(ResolvedLinkInput,
              (Archive, (LinkArchive archive;)),
              (External, (lito::link::ArgumentSequence arguments;)))
};

} // namespace lito
