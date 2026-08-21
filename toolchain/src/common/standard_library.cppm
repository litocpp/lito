export module lito.toolchain.common:standard_library;

import rstd;
import lito.core;
import :error;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito
{

auto resolve_standard_library_selection(lito::config::StandardLibrarySelection selection,
                                        const lito::system::TargetInfo&        target)
    -> ToolchainResult<lito::config::StandardLibrary> {
    auto explicit_family = lito::config::explicit_standard_library(selection);
    if (explicit_family.is_some()) return Ok(*explicit_family);
    if (target.os.as_str() == "android"_str || target.os.as_str() == "macos"_str) {
        return Ok(lito::config::StandardLibrary::Libcxx);
    }
    if (target.os.as_str() == "linux"_str) {
        return Ok(lito::config::StandardLibrary::Libstdcxx);
    }
    if (target.os.as_str() == "windows"_str &&
        target.environment == lito::system::TargetEnvironment::Msvc) {
        return Ok(lito::config::StandardLibrary::Msvc);
    }
    return Err(ToolchainError::Message(rstd::format(
        "cannot automatically select a C++ standard library for target '{}'; configure "
        "toolchain.stdlib explicitly",
        target.triple.as_str())));
}

} // namespace lito
