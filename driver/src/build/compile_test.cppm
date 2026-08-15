module lito.driver:build.compile_test;

import rstd;
import lito.core;
import lito.cpp;
import lito.toolchain.common;
import :build.artifact;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace lito
{

auto same_path(ref<rstd::path::Path> left, ref<rstd::path::Path> right) noexcept -> bool {
    return left.starts_with(right) && right.starts_with(left);
}

} // namespace lito

namespace lito
{

auto compile_test_for_source(const cpp::TargetSpec& target, ref<rstd::path::Path> source)
    -> Option<const cpp::ResolvedCompileTestCase*> {
    for (const auto& test : target.compile_tests) {
        if (same_path(test.source.as_path(), source)) return Some(rstd::addressof(test));
    }
    return None();
}

auto evaluate_compile_test(ref<str>                            package,
                           const cpp::ResolvedCompileTestCase& test,
                           ref<rstd::path::Path>               source,
                           CompileCommandResult                output) -> CompileTestExecution {
    auto mismatch = Option<String> {};
    if (test.outcome == CompileTestOutcome::Success && output.exit_code != i32 {}) {
        mismatch = Some(rstd::format("expected compilation to succeed, but clang exited with {}",
                                     output.exit_code));
    } else if (test.outcome == CompileTestOutcome::Failure && output.exit_code == i32 {}) {
        mismatch = Some(String::make("expected compilation to fail, but clang succeeded"_str));
    }
    if (mismatch.is_none() && output.exit_code != i32 {}) {
        for (const auto& required : test.diagnostic_contains) {
            if (! output.standard_error.as_str().contains(required.as_str())) {
                mismatch =
                    Some(rstd::format("missing required diagnostic '{}'", required.as_str()));
                break;
            }
        }
        if (mismatch.is_none() && ! test.diagnostic_contains_any.is_empty()) {
            auto matched = false;
            for (const auto& required : test.diagnostic_contains_any) {
                if (output.standard_error.as_str().contains(required.as_str())) {
                    matched = true;
                    break;
                }
            }
            if (! matched) {
                mismatch =
                    Some(String::make("none of the alternative diagnostics were present"_str));
            }
        }
    }
    return CompileTestExecution {
        .package         = String::make(package),
        .name            = test.name.clone(),
        .source          = PathBuf::from(source),
        .expected        = test.outcome,
        .exit_code       = output.exit_code,
        .standard_output = rstd::move(output.standard_output),
        .standard_error  = rstd::move(output.standard_error),
        .mismatch        = rstd::move(mismatch),
        .elapsed         = output.elapsed,
    };
}

} // namespace lito
