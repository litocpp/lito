module;
#include <rstd/macro.hpp>

export module lito.toolchain.clang:target;

import rstd;
import lito.core;
import lito.system;
import lito.toolchain.common;
import :options;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace lito
{

constexpr auto ascii_space(u8 value) noexcept -> bool {
    return value == u8(' ') || value == u8('\t') || value == u8('\r');
}

constexpr auto clang_backend_name(lito::system::Architecture architecture) noexcept -> ref<str> {
    using lito::system::Architecture;
    switch (architecture) {
    case Architecture::X86: return "x86"_str;
    case Architecture::X86_64: return "x86-64"_str;
    case Architecture::Powerpc: return "ppc32"_str;
    case Architecture::Powerpcle: return "ppc32le"_str;
    case Architecture::Amdgpu: return "amdgcn"_str;
    case Architecture::Systemz: return "systemz"_str;
    default: return lito::system::architecture_name(architecture);
    }
}

} // namespace lito

export namespace lito
{

class ClangSupportedTargets {
public:
    static auto parse(ref<str> output) -> ToolchainResult<ClangSupportedTargets> {
        auto backends = Vec<String>::make();
        auto bytes    = output.as_bytes();
        auto start    = usize {};
        while (start <= bytes.len()) {
            auto end = start;
            while (end < bytes.len() && bytes[end] != u8('\n')) ++end;
            auto first = start;
            while (first < end && ascii_space(bytes[first])) ++first;
            auto last = end;
            while (last > first && ascii_space(bytes[last - usize(1)])) --last;
            auto token_end = first;
            while (token_end < last && ! ascii_space(bytes[token_end])) ++token_end;
            auto token = output.get(first, token_end);
            if (token.is_some() && ! token->is_empty() && *token != "Registered"_str) {
                auto duplicate = false;
                for (const auto& existing : backends) {
                    if (existing.as_str() == *token) {
                        duplicate = true;
                        break;
                    }
                }
                if (! duplicate) backends.push(String::make(*token));
            }
            if (end == bytes.len()) break;
            start = end + usize(1);
        }
        if (backends.is_empty()) {
            return Err(ToolchainError::Message(
                String::make("clang++ --print-targets returned no registered targets"_str)));
        }
        return Ok(ClangSupportedTargets(rstd::move(backends), licrypto::sha256_hex(output)));
    }

    auto validate(const lito::system::Architecture& architecture, ref<str> triple) const
        -> ToolchainResult<empty> {
        auto required = clang_backend_name(architecture);
        for (const auto& backend : backends_) {
            if (backend == required) return Ok(empty {});
        }
        return Err(ToolchainError::Message(rstd::format(
            "compiler does not support target '{}' architecture '{}' (Clang backend '{}'); "
            "registered targets: {}",
            triple,
            lito::system::architecture_name(architecture),
            required,
            summary().as_str())));
    }

    auto backends() const noexcept -> const Vec<String>& { return backends_; }
    auto identity() const noexcept -> ref<str> { return identity_.as_str(); }

    auto selection_summary(const lito::system::Architecture& architecture) const -> String {
        return rstd::format(
            "{} ({} registered)", clang_backend_name(architecture), backends_.len());
    }

    auto summary() const -> String {
        auto result = String::make();
        for (usize index {}; index < backends_.len(); ++index) {
            if (index != usize {}) result.push_str(", "_str);
            result.push_str(backends_[index].as_str());
        }
        return result;
    }

    auto clone() const -> ClangSupportedTargets {
        return ClangSupportedTargets(backends_.clone(), identity_.clone());
    }

private:
    ClangSupportedTargets(Vec<String> backends, String identity)
        : backends_(rstd::move(backends)), identity_(rstd::move(identity)) {}

    Vec<String> backends_;
    String      identity_;
};

struct ClangTargetResolution {
    PathBuf               compiler;
    CompileTarget         target;
    ClangSupportedTargets supported_targets;
};

auto resolve_clang_target(const lito::config::ToolchainSpec&              specification,
                          lito::config::StandardLibrarySelection          standard_library,
                          const lito::system::TargetInfo*                 sdk_target,
                          const lito::system::ResolvedProcessEnvironment& environment)
    -> ToolchainResult<ClangTargetResolution> {
    auto located = environment.locate_executable(specification.cxx.as_path(), "clang++"_str);
    if (located.is_err()) {
        return Err(rstd::into<ToolchainError>(rstd::move(located).unwrap_err()));
    }
    if (located->is_none()) {
        return Err(ToolchainError::Message(rstd::format(
            "cannot resolve clang++ '{}' from effective PATH", specification.cxx.as_path())));
    }
    auto       compiler = rstd::move(located).unwrap().unwrap();
    const auto query    = [&](ref<str> option, ref<str> description) -> ToolchainResult<String> {
        auto arguments = Vec<String>::make();
        rstd_try(toolchain::command::push_path(arguments, compiler.as_path()));
        toolchain::command::push_option(arguments, option);
        return toolchain::command::tool_output(rstd::move(arguments), description, environment);
    };

    auto targets = rstd_try(
        query(toolchain::clang_options::PRINT_TARGETS, "clang++ supported target query"_str));
    auto supported = rstd_try(ClangSupportedTargets::parse(targets.as_str()));
    auto target    = Option<CompileTarget> {};
    if (sdk_target != nullptr) {
        auto os = lito::system::target_operating_system(*sdk_target);
        if (os.is_err()) return Err(ToolchainError::Platform(rstd::move(os).unwrap_err()));
        if (specification.target.is_Config() &&
            (*os != specification.target.as_Config().os ||
             sdk_target->architecture != specification.target.as_Config().architecture)) {
            return Err(ToolchainError::Message(
                rstd::format("configured toolchain target '{}-{}' conflicts with SDK target '{}'",
                             architecture_name(specification.target.as_Config().architecture),
                             operating_system_name(specification.target.as_Config().os),
                             sdk_target->triple.as_str())));
        }
        auto library = rstd_try(resolve_standard_library_selection(standard_library, *os));
        target       = Some(rstd_try(resolve_sdk_compile_target(*sdk_target, library)));
    } else {
        auto compiler_default = Option<lito::system::TargetInfo> {};
        if (specification.target.is_CompilerDefault()) {
            auto triple = rstd_try(query(toolchain::clang_options::PRINT_TARGET_TRIPLE,
                                         "clang++ default target query"_str));
            auto parsed = lito::system::parse_target_info(triple.as_str());
            if (parsed.is_err()) {
                return Err(ToolchainError::Platform(rstd::move(parsed).unwrap_err()));
            }
            compiler_default = Some(rstd::move(parsed).unwrap());
        }
        auto input   = rstd_try(resolve_toolchain_target_input(
            specification.target,
            compiler_default.is_some() ? rstd::addressof(*compiler_default) : nullptr));
        auto library = rstd_try(resolve_standard_library_selection(standard_library, input.os));
        target       = Some(rstd_try(resolve_compile_target(input, library)));
    }
    rstd_try(supported.validate(target->info.architecture, target->info.triple.as_str()));
    return Ok(ClangTargetResolution {
        .compiler          = rstd::move(compiler),
        .target            = rstd::move(target).unwrap(),
        .supported_targets = rstd::move(supported),
    });
}

auto resolve_clang_target(const lito::config::ToolchainSpec&              specification,
                          lito::config::StandardLibrarySelection          standard_library,
                          const lito::system::ResolvedProcessEnvironment& environment)
    -> ToolchainResult<ClangTargetResolution> {
    return resolve_clang_target(specification, standard_library, nullptr, environment);
}

} // namespace lito
