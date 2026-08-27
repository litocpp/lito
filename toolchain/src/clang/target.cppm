export module lito.toolchain.clang:target;

import rstd;
import lito.core;
import lito.system;
import lito.toolchain.common;

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
        return Ok(ClangSupportedTargets(rstd::move(backends), lito::crypto::sha256_hex(output)));
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

} // namespace lito
