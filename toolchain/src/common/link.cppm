module;
#include <rstd/enum.hpp>

export module lito.toolchain.common:link;

import rstd;
import lito.core;
import lito.cpp;
import lito.system;
import :command;
import :error;

using namespace rstd::prelude;
using namespace rstd::literals;

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

enum class LinkOutputKind
{
    Executable,
    SharedLibrary,
};

struct LinkTargetContext {
    lito::system::BuildPlatform          platform;
    lito::manifest::PackageLanguage      language { lito::manifest::PackageLanguage::Cpp };
    lito::config::StandardLibrary        standard_library { lito::config::StandardLibrary::Libcxx };
    lito::config::StandardLibraryRuntime standard_library_runtime {
        lito::config::StandardLibraryRuntime::Dynamic
    };
    Option<lito::compiler::MicrosoftRuntimeLibrary> microsoft_runtime_library;
    bool                                            link_standard_library { true };
    Option<lito::config::WasmToolchainSpec>         wasm;
    LinkOutputKind                                  output { LinkOutputKind::Executable };
    Option<String>                                  soname;
};

struct ElfSharedLibraryLinkRequest {
    PathBuf     output;
    LinkArchive archive;
    String      soname;
    PathBuf     version_script;
    PathBuf     working_directory;
};

struct ElfSharedLibraryArtifact {
    PathBuf              file;
    String               soname;
    String               link_identity;
    rstd::time::Duration elapsed;
};

enum class LinkerFamily
{
    Lld,
    GnuLd,
};

auto linker_family_name(LinkerFamily family) noexcept -> ref<str> {
    switch (family) {
    case LinkerFamily::Lld: return "LLD"_str;
    case LinkerFamily::GnuLd: return "GNU ld"_str;
    }
    return "unknown"_str;
}

auto lld_executable_name(const lito::system::TargetInfo& target) noexcept -> ref<str> {
    if (target.is_msvc()) return "lld-link"_str;
    if (target.platform == lito::system::TargetPlatform::Macos) return "ld64.lld"_str;
    if (target.architecture == lito::system::Architecture::Wasm32 ||
        target.architecture == lito::system::Architecture::Wasm64) {
        return "wasm-ld"_str;
    }
    return "ld.lld"_str;
}

struct LinkerCapabilities {
    bool llvm_lto {};
    bool elf_shared_library {};
    bool cross_target {};
};

struct LinkerIdentity {
    PathBuf            executable;
    LinkerFamily       family { LinkerFamily::Lld };
    String             version;
    String             build_identity;
    LinkerCapabilities capabilities;

    auto clone() const -> LinkerIdentity {
        return LinkerIdentity {
            .executable     = executable.clone(),
            .family         = family,
            .version        = version.clone(),
            .build_identity = build_identity.clone(),
            .capabilities   = capabilities,
        };
    }
};

auto push_clang_lld_selection(Vec<String>& arguments, ref<rstd::path::Path> executable)
    -> ToolchainResult<empty> {
    toolchain::command::push_option(arguments, "-fuse-ld=lld"_str);
    return toolchain::command::push_path_option(arguments, "--ld-path="_str, executable);
}

class ResolvedLinkInput {
    RSTD_ENUM(ResolvedLinkInput,
              (Archive, (LinkArchive archive;)),
              (SharedLibrary, (PathBuf library;)),
              (External, (lito::link::ArgumentSequence arguments;)))
};

auto probe_linker(ref<rstd::path::Path>                           executable,
                  const lito::system::ResolvedProcessEnvironment& environment)
    -> ToolchainResult<LinkerIdentity> {
    auto command = Vec<String>::make();
    rstd_try(toolchain::command::push_path(command, executable));
    toolchain::command::push_option(command, "--version"_str);
    auto output =
        toolchain::command::tool_output(rstd::move(command), "linker --version"_str, environment);
    if (output.is_err()) return Err(rstd::move(output).unwrap_err());

    auto first_line = output->as_str();
    auto newline    = first_line.split_once("\n"_str);
    if (newline.is_some()) first_line = newline->get<0>();
    first_line = first_line.trim_ascii();

    auto family = Option<LinkerFamily> {};
    if (first_line == "LLD"_str || first_line.starts_with("LLD "_str) ||
        first_line.contains(" LLD "_str) || first_line.ends_with(" LLD"_str)) {
        family = Some(LinkerFamily::Lld);
    } else if (first_line == "GNU ld"_str || first_line.starts_with("GNU ld "_str) ||
               first_line.starts_with("GNU ld ("_str)) {
        family = Some(LinkerFamily::GnuLd);
    }
    if (family.is_none()) {
        return Err(ToolchainError::Message(
            rstd::format("configured linker '{}' is unsupported; expected LLD or GNU ld, got '{}'",
                         executable,
                         first_line)));
    }

    auto metadata = rstd::fs::metadata(executable);
    if (metadata.is_err()) {
        return Err(ToolchainError::Io(String::make("inspect linker"_str),
                                      PathBuf::from(executable),
                                      rstd::move(metadata).unwrap_err()));
    }
    auto modified = metadata->modified();
    if (modified.is_err()) {
        return Err(ToolchainError::Io(String::make("read linker modification time"_str),
                                      PathBuf::from(executable),
                                      rstd::move(modified).unwrap_err()));
    }
    auto timestamp = modified->as_unix_time();
    auto path_text = executable.to_str();
    if (path_text.is_none()) {
        return Err(ToolchainError::Message(
            rstd::format("linker path '{}' is not valid UTF-8", executable)));
    }
    auto capabilities = LinkerCapabilities {
        .llvm_lto           = *family == LinkerFamily::Lld,
        .elf_shared_library = true,
        .cross_target       = *family == LinkerFamily::Lld,
    };
    auto identity = rstd::format("lito-linker-v1\nfamily:{}\npath:{}\nversion:{}\n{}:{}:{}",
                                 linker_family_name(*family),
                                 *path_text,
                                 output->as_str(),
                                 metadata->size(),
                                 timestamp.seconds,
                                 timestamp.nanoseconds);
    return Ok(LinkerIdentity {
        .executable     = PathBuf::from(executable),
        .family         = *family,
        .version        = rstd::move(output).unwrap(),
        .build_identity = rstd::move(identity),
        .capabilities   = capabilities,
    });
}

} // namespace lito
