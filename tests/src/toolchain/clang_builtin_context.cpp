#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito.core;
import lito.cpp;
import lito.toolchain;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito;

namespace
{

auto context_with(
    Vec<String>                     options,
    lito::manifest::CppOptimization optimization = lito::manifest::CppOptimization::Default,
    lito::manifest::CppDebugInfo    debug_info   = lito::manifest::CppDebugInfo::None)
    -> cpp::CompileContext {
    auto parser = make_clang_cpp_argument_parser();
    if (parser.is_err()) return cpp::CompileContext {};
    auto arguments = parser->parse(options, "clang-builtin-context-test"_str);
    if (arguments.is_err()) return cpp::CompileContext {};
    auto normalized = cpp::make_cpp_options("c++20"_str,
                                            lito::config::StandardLibrary::Libcxx,
                                            false,
                                            false,
                                            optimization,
                                            debug_info,
                                            cpp::CppOptionLayer {
                                                .arguments = rstd::move(arguments).unwrap(),
                                            });
    if (normalized.is_err()) return cpp::CompileContext {};
    return cpp::CompileContext {
        .cpp = rstd::move(normalized).unwrap(),
    };
}

auto context_with_standard(ref<str> standard) -> cpp::CompileContext {
    auto normalized = cpp::make_cpp_options(standard,
                                            lito::config::StandardLibrary::Libcxx,
                                            false,
                                            false,
                                            lito::manifest::CppOptimization::Default,
                                            lito::manifest::CppDebugInfo::None);
    if (normalized.is_err()) return cpp::CompileContext {};
    return cpp::CompileContext {
        .cpp = rstd::move(normalized).unwrap(),
    };
}

template<typename... Values>
auto options(Values... values) -> Vec<String> {
    auto result = Vec<String>::with_capacity(usize(sizeof...(Values)));
    (result.push(String::make(values)), ...);
    return result;
}

auto same_command(const Vec<String>& left, const Vec<String>& right) -> bool {
    if (left.len() != right.len()) return false;
    for (auto index = usize {}; index < left.len(); ++index) {
        if (left[index].as_str() != right[index].as_str()) return false;
    }
    return true;
}

} // namespace

auto run_clang_builtin_context_test() -> int {
    auto created = ClangToolchain::create(lito::config::ToolchainSpec {
        .cxx = PathBuf::from("clang++"_str),
        .ar  = PathBuf::from("llvm-ar"_str),
    });
    if (created.is_err()) return 1;
    auto toolchain = rstd::move(created).unwrap();

    auto debug     = toolchain.builtin_context(context_with(options("-Wall"_str, "-fPIC"_str),
                                                            lito::manifest::CppOptimization::None,
                                                            lito::manifest::CppDebugInfo::Full));
    auto reordered = toolchain.builtin_context(
        context_with(options("-fPIC"_str, "-Wall"_str), lito::manifest::CppOptimization::None));
    if (debug.is_err() || reordered.is_err()) return 2;
    if (debug->key.as_str() != reordered->key.as_str() ||
        ! same_command(debug->query_command, reordered->query_command)) {
        return 3;
    }

    auto optimized = toolchain.builtin_context(
        context_with(options("-fPIC"_str), lito::manifest::CppOptimization::Level3));
    if (optimized.is_err()) return 4;
    if (optimized->key.as_str() == debug->key.as_str()) return 5;

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
        toolchain.builtin_context(context_with(options("-mllvm=lito-ignored"_str),
                                               lito::manifest::CppOptimization::Default,
                                               lito::manifest::CppDebugInfo::Full));
    auto llvm_debug_direct = toolchain.builtin_context(context_with(options()));
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
