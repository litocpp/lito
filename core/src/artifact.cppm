export module lito.core:artifact;

import rstd;
import lito.system;

using namespace rstd::prelude;
using rstd::path::PathBuf;
using namespace rstd::literals;

export namespace lito::artifact
{

enum class ProductKind
{
    StaticLibrary,
    SharedLibrary,
    Executable,
};

enum class Format
{
    Archive,
    Elf,
    PeCoff,
    MachO,
    WebAssembly,
};

auto format_name(Format format) noexcept -> ref<str> {
    switch (format) {
    case Format::Archive: return "archive"_str;
    case Format::Elf: return "elf"_str;
    case Format::PeCoff: return "pe-coff"_str;
    case Format::MachO: return "mach-o"_str;
    case Format::WebAssembly: return "webassembly"_str;
    }
    return "unknown"_str;
}

auto product_format(ProductKind kind, const lito::system::TargetInfo& target) noexcept -> Format {
    if (kind == ProductKind::StaticLibrary) return Format::Archive;
    if (target.architecture == lito::system::Architecture::Wasm32 ||
        target.architecture == lito::system::Architecture::Wasm64)
        return Format::WebAssembly;
    if (target.family == lito::system::TargetFamily::Windows) return Format::PeCoff;
    if (target.platform == lito::system::TargetPlatform::Macos) return Format::MachO;
    return Format::Elf;
}

auto product_name(ProductKind kind, ref<str> logical_name, const lito::system::TargetInfo& target)
    -> String {
    auto result = String::make();
    auto format = product_format(kind, target);
    if (kind == ProductKind::StaticLibrary) {
        if (target.family != lito::system::TargetFamily::Windows) result.push_str("lib"_str);
        result.push_str(logical_name);
        result.push_str(target.family == lito::system::TargetFamily::Windows ? ".lib"_str
                                                                             : ".a"_str);
        return result;
    }
    if (kind == ProductKind::SharedLibrary && format != Format::PeCoff &&
        format != Format::WebAssembly)
        result.push_str("lib"_str);
    result.push_str(logical_name);
    if (kind == ProductKind::SharedLibrary) {
        if (format == Format::PeCoff) result.push_str(".dll"_str);
        if (format == Format::MachO) result.push_str(".dylib"_str);
        if (format == Format::Elf) result.push_str(".so"_str);
        if (format == Format::WebAssembly) result.push_str(".wasm"_str);
    } else if (format == Format::PeCoff) {
        result.push_str(".exe"_str);
    } else if (format == Format::WebAssembly) {
        result.push_str(".wasm"_str);
    }
    return result;
}

enum class StripMode
{
    None,
    DebugInfo,
    Symbols,
};

struct OriginRelativeRuntimePath {
    PathBuf path;

    auto clone() const -> OriginRelativeRuntimePath {
        return OriginRelativeRuntimePath { .path = path.clone() };
    }

    auto operator==(const OriginRelativeRuntimePath& other) const noexcept -> bool {
        return path.as_path() == other.path.as_path();
    }
};

struct ElfRunpath {
    Vec<OriginRelativeRuntimePath> paths;

    auto clone() const -> ElfRunpath {
        auto result = Vec<OriginRelativeRuntimePath>::with_capacity(paths.len());
        for (const auto& path : paths) result.push(path.clone());
        return ElfRunpath { .paths = rstd::move(result) };
    }
};

using OriginRelativeRuntimePathResult = Result<OriginRelativeRuntimePath, String>;
using ElfRunpathResult                = Result<ElfRunpath, String>;

auto make_origin_relative_runtime_path(PathBuf path) -> OriginRelativeRuntimePathResult {
    if (path.is_empty() || path.as_path().is_absolute() || path.as_path().has_root()) {
        return Err(String::make("runtime search path must be a non-empty relative path"_str));
    }
    auto text = path.as_path().to_str();
    if (text.is_none()) {
        return Err(String::make("runtime search path must be valid UTF-8"_str));
    }
    if (text->contains(":"_str) || text->contains("$"_str)) {
        return Err(
            String::make("runtime search path may not contain ':' or loader substitutions"_str));
    }
    auto components = path.as_path().components();
    for (auto component = components.next(); component.is_some(); component = components.next()) {
        if (component->is_normal() || component->is_parent_dir()) continue;
        if (component->is_cur_dir() && *text == "."_str && components.next().is_none()) continue;
        return Err(String::make("runtime search path is not lexically normalized"_str));
    }
    return Ok(OriginRelativeRuntimePath { .path = rstd::move(path) });
}

auto make_elf_runpath(Vec<OriginRelativeRuntimePath> paths) -> ElfRunpathResult {
    if (paths.is_empty()) return Err(String::make("ELF RUNPATH must not be empty"_str));
    for (usize index {}; index < paths.len(); ++index) {
        for (usize prior {}; prior < index; ++prior) {
            if (paths[prior] == paths[index]) {
                return Err(rstd::format("ELF RUNPATH repeats '{}'", paths[index].path.as_path()));
            }
        }
    }
    return Ok(ElfRunpath { .paths = rstd::move(paths) });
}

auto elf_runpath_identity(const ElfRunpath& runpath) -> String {
    auto result = String::make("lito-elf-runpath-v1\n"_str);
    for (const auto& path : runpath.paths) {
        result.push_str("origin-relative="_str);
        result.push_str(path.path.as_path().to_string_lossy().as_str());
        result.push_ascii('\n');
    }
    return result;
}

} // namespace lito::artifact
