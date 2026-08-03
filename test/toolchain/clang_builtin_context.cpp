#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import tenon;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace tenon;

namespace
{

auto context_with(tenon::Vec<String> options) -> CompileContext {
    return CompileContext {
        .language_standard = String::make("c++20"_str),
        .options           = rstd::move(options),
    };
}

auto context_with_standard(ref<str> standard) -> CompileContext {
    return CompileContext {
        .language_standard = String::make(standard),
    };
}

template<typename... Values>
auto options(Values... values) -> tenon::Vec<String> {
    auto result = tenon::Vec<String>::with_capacity(usize(sizeof...(Values)));
    (result.push(String::make(values)), ...);
    return result;
}

auto same_command(const tenon::Vec<String>& left, const tenon::Vec<String>& right) -> bool {
    if (left.len() != right.len()) return false;
    for (auto index = usize {}; index < left.len(); ++index) {
        if (left[index].as_str() != right[index].as_str()) return false;
    }
    return true;
}

} // namespace

auto run_clang_builtin_context_test() -> int {
    auto created = ClangToolchain::create(ToolchainSpec {
        .compiler = PathBuf::from("clang++"_str),
        .archiver = PathBuf::from("llvm-ar"_str),
    });
    if (created.is_err()) return 1;
    auto toolchain = rstd::move(created).unwrap();

    auto debug = toolchain.builtin_context(
        context_with(options("-O0"_str, "-g"_str, "-Wall"_str, "-fPIC"_str)));
    auto reordered = toolchain.builtin_context(
        context_with(options("-Wall"_str, "-fPIC"_str, "-g0"_str, "-O0"_str)));
    if (debug.is_err() || reordered.is_err()) return 2;
    if (debug->key.as_str() != reordered->key.as_str() ||
        ! same_command(debug->query_command, reordered->query_command)) {
        return 3;
    }

    auto optimized =
        toolchain.builtin_context(context_with(options("-O0"_str, "-O3"_str, "-fPIC"_str)));
    auto optimized_direct =
        toolchain.builtin_context(context_with(options("-O3"_str, "-fPIC"_str)));
    if (optimized.is_err() || optimized_direct.is_err()) return 4;
    if (optimized->key.as_str() != optimized_direct->key.as_str() ||
        optimized->key.as_str() == debug->key.as_str()) {
        return 5;
    }

    auto target_attached = toolchain.builtin_context(
        context_with(options("--target=x86_64-pc-linux-gnu"_str, "-fPIC"_str, "-fno-PIC"_str)));
    auto target_separate = toolchain.builtin_context(
        context_with(options("-target"_str, "x86_64-pc-linux-gnu"_str, "-fno-PIC"_str)));
    if (target_attached.is_err() || target_separate.is_err()) return 6;
    if (target_attached->key.as_str() != target_separate->key.as_str() ||
        ! same_command(target_attached->query_command, target_separate->query_command)) {
        return 7;
    }

    auto target_feature =
        toolchain.builtin_context(context_with(options("-msse2"_str, "-mno-sse2"_str)));
    auto target_feature_direct = toolchain.builtin_context(context_with(options("-mno-sse2"_str)));
    if (target_feature.is_err() || target_feature_direct.is_err()) return 8;
    if (target_feature->key.as_str() != target_feature_direct->key.as_str()) return 9;

    auto llvm_debug =
        toolchain.builtin_context(context_with(options("-mllvm=tenon-ignored"_str, "-g"_str)));
    auto llvm_debug_direct = toolchain.builtin_context(context_with(options("-g0"_str)));
    if (llvm_debug.is_err() || llvm_debug_direct.is_err()) return 10;
    if (llvm_debug->key.as_str() != llvm_debug_direct->key.as_str() ||
        ! same_command(llvm_debug->query_command, llvm_debug_direct->query_command)) {
        return 11;
    }

    auto cxx23 = toolchain.builtin_context(context_with_standard("c++23"_str));
    auto cxx17 = toolchain.builtin_context(context_with_standard("c++17"_str));
    if (cxx23.is_err() || cxx23->key.as_str() == debug->key.as_str()) return 12;
    if (cxx17.is_ok()) return 13;
    return 0;
}

TEST(ClangBuiltinContext, RelevantOptionsOnly) {
    EXPECT_EQ(run_clang_builtin_context_test(), 0);
}
