#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito.driver;

using namespace rstd::prelude;
using namespace rstd::literals;
using rstd::path::PathBuf;

auto doc_execution_error(String output, String error) -> lito::DocError {
    return lito::DocError::Execution("litodoc generate"_Str,
                                     PathBuf::from("litodoc"_str),
                                     i32(1),
                                     rstd::move(output),
                                     rstd::move(error));
}

TEST(DocError, DisplaysCapturedProcessDiagnostics) {
    auto error = doc_execution_error("generation context\n"_Str, "invalid publication\n"_Str);
    EXPECT_EQ(rstd::format("{}", error).as_str(),
              "litodoc generate failed with exit code 1 for 'litodoc'\n"
              "stderr:\ninvalid publication\nstdout:\ngeneration context"_str);
}

TEST(DocError, DisplaysStdoutWithoutStderr) {
    auto error = doc_execution_error("failed to generate package\n"_Str, " \n\t"_Str);
    EXPECT_EQ(rstd::format("{}", error).as_str(),
              "litodoc generate failed with exit code 1 for 'litodoc'\n"
              "stdout:\nfailed to generate package"_str);
}

TEST(DocError, KeepsExitSummaryForEmptyProcessOutput) {
    auto error = doc_execution_error(" \n"_Str, "\t\n"_Str);
    EXPECT_EQ(rstd::format("{}", error).as_str(),
              "litodoc generate failed with exit code 1 for 'litodoc'"_str);
}

TEST(DocError, BoundsEachStreamAtUtf8Boundary) {
    String text;
    for (usize index {}; index < usize(16 * 1024 - 1); ++index) text.push_ascii('x');
    text.push_str("错误"_str);
    auto output  = text.clone();
    auto error   = doc_execution_error(rstd::move(output), rstd::move(text));
    auto message = rstd::format("{}", error);
    EXPECT_FALSE(message.as_str().contains("错误"_str));
    EXPECT_TRUE(message.as_str().contains("\n[output truncated]\nstdout:\n"_str));
    EXPECT_TRUE(message.as_str().ends_with("\n[output truncated]"_str));
    EXPECT_LT(message.len(), usize(2 * 16 * 1024 + 200));
}

TEST(DocError, PreservesMultibyteDiagnosticsWithinLimit) {
    auto error   = doc_execution_error(String {}, "无法生成文档\n"_Str);
    auto message = rstd::format("{}", error);
    EXPECT_TRUE(message.as_str().ends_with("stderr:\n无法生成文档"_str));
    EXPECT_FALSE(message.as_str().contains("[output truncated]"_str));
}
