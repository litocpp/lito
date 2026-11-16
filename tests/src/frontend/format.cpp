#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito.frontend.lexical;
import lito.frontend.format;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito::frontend::lexical;
using lito::frontend::format::format_source;

auto format_test_source(ref<str> contents) -> SourceFile {
    return SourceFile::make(usize {},
                            SourceBuffer {
                                .path     = rstd::path::PathBuf::from("format.cpp"_str),
                                .contents = String::make(contents),
                            });
}

TEST(BuiltinFormat, FormatsBasicStructureAndIsIdempotent) {
    auto source = format_test_source("export module demo;\nimport rstd;\n"
                                     "namespace sample{auto f(int x)->int{"
                                     "if(x>0){return x;}return 0;}}\n"_str);
    auto result = format_source(source);
    ASSERT_TRUE(result.is_ok());
    constexpr auto expected = "export module demo;\n"
                              "import rstd;\n"
                              "namespace sample {\n"
                              "    auto f(int x) -> int {\n"
                              "        if (x > 0) {\n"
                              "            return x;\n"
                              "        }\n"
                              "        return 0;\n"
                              "    }\n"
                              "}\n"_str;
    EXPECT_EQ(result->as_str(), expected);
    auto repeated = format_source(format_test_source(result->as_str()));
    ASSERT_TRUE(repeated.is_ok());
    EXPECT_EQ(repeated->as_str(), result->as_str());
}

TEST(BuiltinFormat, PreservesRawTextCommentsAndDisabledRegions) {
    constexpr auto directive = "#include <vector>  // preserve directive\n"_str;
    constexpr auto disabled  = "// clang-format off\n"
                               "  int   untouched( ){return   2;}\n"
                               "// clang-format on\n"_str;
    auto           contents  = String::make(directive);
    contents.push_str(disabled);
    contents.push_str(R"cpp(// keep   comment spacing
/* keep block
   comment spacing */
auto text = R"tag({ raw text
  // lito-format off
})tag";
auto marker="lito-format off";
auto after()->int{return 1;}
)cpp"_str);
    auto result = format_source(format_test_source(contents.as_str()));
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(result->as_str().contains(directive));
    EXPECT_TRUE(result->as_str().contains(disabled));
    EXPECT_TRUE(result->as_str().contains("// keep   comment spacing\n"_str));
    EXPECT_TRUE(result->as_str().contains("/* keep block\n   comment spacing */"_str));
    EXPECT_TRUE(result->as_str().contains("R\"tag({ raw text\n"
                                          "  // lito-format off\n"
                                          "})tag\""_str));
    EXPECT_TRUE(result->as_str().contains("auto after() -> int {\n    return 1;\n}"_str));
    auto repeated = format_source(format_test_source(result->as_str()));
    ASSERT_TRUE(repeated.is_ok());
    EXPECT_EQ(repeated->as_str(), result->as_str());
}

TEST(BuiltinFormat, PreservesMacroSpellingContinuationsAndConditionalRegions) {
    const ref<str> endings[] = { "\n"_str, "\r\n"_str, "\r"_str };
    for (auto ending : endings) {
        auto macros      = rstd::format("#define stringify(x) #x{}"
                                        "#define ADD_ONE(x) ((x) + \\{}"
                                        "    1){}",
                                        ending,
                                        ending,
                                        ending);
        auto conditional = rstd::format("#if 1{}"
                                        "  int   selected = 1 ;{}"
                                        "#else{}"
                                        "int alternate=  2;{}"
                                        "#endif{}",
                                        ending,
                                        ending,
                                        ending,
                                        ending,
                                        ending);
        auto contents    = macros.clone();
        contents.push_str(conditional.as_str());
        contents.push_str(rstd::format("auto text=stringify(a+b);{}", ending).as_str());
        contents.push_str(
            rstd::format("auto other=stringify /* trivia */ (c+d);{}", ending).as_str());
        contents.push_str("auto tail()->int{return 0;}"_str);
        contents.push_str(ending);
        auto result = format_source(format_test_source(contents.as_str()));
        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result->as_str().contains(macros.as_str()));
        EXPECT_TRUE(result->as_str().contains(conditional.as_str()));
        EXPECT_TRUE(result->as_str().contains("stringify(a+b)"_str));
        EXPECT_TRUE(result->as_str().contains("stringify /* trivia */ (c+d)"_str));
        EXPECT_TRUE(result->as_str().contains(
            rstd::format("auto tail() -> int {{{}    return 0;{}}}{}", ending, ending, ending)
                .as_str()));
        auto repeated = format_source(format_test_source(result->as_str()));
        ASSERT_TRUE(repeated.is_ok());
        EXPECT_EQ(repeated->as_str(), result->as_str());
    }
}

TEST(BuiltinFormat, LeavesSourceAndScanStorageUnchangedAndOwnsOutput) {
    Option<String> output;
    {
        constexpr auto original = "// original comment\r\nauto f()->int{return 7;}\r\n"_str;
        auto           source   = format_test_source(original);
        auto           before   = lex_scan_file(source);
        ASSERT_TRUE(before.is_ok());
        auto formatted = format_source(source);
        ASSERT_TRUE(formatted.is_ok());
        EXPECT_EQ(rstd::str_::from_utf8(source.contents()).unwrap(), original);
        EXPECT_EQ(rstd::str_::from_utf8(before->snapshot->contents.as_bytes()).unwrap(), original);
        auto after = lex_scan_file(source);
        ASSERT_TRUE(after.is_ok());
        ASSERT_EQ(after->tokens.len(), before->tokens.len());
        ASSERT_EQ(after->comments.len(), before->comments.len());
        for (usize index {}; index < before->tokens.len(); ++index) {
            auto old_token = before->token(source.id, index);
            auto new_token = after->token(source.id, index);
            EXPECT_EQ(new_token.kind(), old_token.kind());
            EXPECT_EQ(new_token.offset(), old_token.offset());
            EXPECT_TRUE(new_token.text().same_bytes(old_token.text()));
        }
        EXPECT_EQ(before->comment(source.id, usize {}).text.utf8().unwrap(),
                  "// original comment"_str);
        output = Some(rstd::move(formatted).unwrap());
    }
    ASSERT_TRUE(output.is_some());
    EXPECT_EQ(output->as_str(),
              "// original comment\r\nauto f() -> int {\r\n    return 7;\r\n}\r\n"_str);
}
