export module lito.toolchain.spec;

import rstd;
import lito.error;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito
{

struct ToolchainSpec {
    PathBuf compiler;
    PathBuf c_compiler { PathBuf::from("clang"_str) };
    PathBuf linker { PathBuf::from("ld.lld"_str) };
    PathBuf archiver;
    PathBuf formatter;
    PathBuf stripper;

    auto clone() const -> ToolchainSpec {
        return ToolchainSpec {
            .compiler   = compiler.clone(),
            .c_compiler = c_compiler.clone(),
            .linker     = linker.clone(),
            .archiver   = archiver.clone(),
            .formatter  = formatter.clone(),
            .stripper   = stripper.clone(),
        };
    }
};

} // namespace lito
