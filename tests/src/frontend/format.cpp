#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito.frontend.lexical;
import lito.frontend.format;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito::frontend::lexical;
using lito::frontend::format::format_source;
using lito::frontend::format::parse_style_configuration;

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

TEST(BuiltinFormat, AppliesSupportedStyleOptionsToFinalOutput) {
    struct Case {
        ref<str> configuration;
        ref<str> source;
        ref<str> expected;
    };
    const Case cases[] = {
        { "\xef\xbb\xbfIndentWidth: '2'\nUseTab: \"Never\" # indentation\nNamespaceIndentation: "
          "All\n"
          "BreakBeforeBraces: Attach\nSpaceBeforeAssignmentOperators: true\n"_str,
          "namespace outer\n{\nnamespace inner\n{\nint value=1;\n}\n}\n"_str,
          "namespace outer {\n  namespace inner {\n    int value = 1;\n  }\n}\n"_str },
        { "IndentWidth: 2\nNamespaceIndentation: Inner\n"_str,
          "namespace outer{namespace inner{int value;}}\r\n"_str,
          "namespace outer {\r\nnamespace inner {\r\n  int value;\r\n}\r\n}\r\n"_str },
        { "IndentWidth: 2\nNamespaceIndentation: None\n"_str,
          "namespace sample{int f(){return 1;}}\n"_str,
          "namespace sample {\nint f() {\n  return 1;\n}\n}\n"_str },
        { "IndentWidth: 10\nTabWidth: 8\nUseTab: ForIndentation\n"_str,
          "// lito-format off\n  int   untouched( ){return   2;}\n// lito-format on\n"
          "int f(){return 1;}\n"_str,
          "// lito-format off\n  int   untouched( ){return   2;}\n// lito-format on\n"
          "int f() {\n\t  return 1;\n}\n"_str },
        { "BreakBeforeBraces: Allman\nSpaceBeforeParens: Always\n"
          "SpaceInEmptyParentheses: true\n"_str,
          "int f(){return 1;}\n"_str,
          "int f ( )\n{\n    return 1;\n}\n"_str },
        { "BreakBeforeBraces: Allman\n"_str,
          "void f(){if(true){run();}else{stop();}try{run();}catch(...){stop();}}\n"_str,
          "void f()\n{\n    if (true)\n    {\n        run();\n    }\n"
          "    else\n    {\n        stop();\n    }\n    try\n    {\n        run();\n    }\n"
          "    catch (...)\n    {\n        stop();\n    }\n}\n"_str },
        { "SpaceBeforeParens: Never\nSpacesInParentheses: true\n"
          "SpaceBeforeAssignmentOperators: false\n"_str,
          "int f(int x){int y=-x;y+=-1;if(x>0){return y;}return 0;}\n"_str,
          "int f( int x ) {\n    int y= -x;\n    y+= -1;\n    if( x > 0 ) {\n"
          "        return y;\n    }\n    return 0;\n}\n"_str },
        { "AllowShortFunctionsOnASingleLine: All\nAllowShortBlocksOnASingleLine: Never\n"
          "SpaceInEmptyParentheses: false\n"_str,
          "int f( ){return 1;}\n"_str,
          "int f() { return 1; }\n"_str },
        { "AllowShortFunctionsOnASingleLine: Empty\nAllowShortBlocksOnASingleLine: Empty\n"
          "SpacesInParentheses: false\n"_str,
          "void empty(){}\nint full( int x ){return x;}\nvoid blocks(){if(true){}}\n"_str,
          "void empty() {}\nint full(int x) {\n    return x;\n}\n"
          "void blocks() {\n    if (true) {}\n}\n"_str },
        { "AllowShortFunctionsOnASingleLine: None\nAllowShortBlocksOnASingleLine: Always\n"
          "KeepEmptyLinesAtTheStartOfBlocks: false\nSpaceBeforeParens: ControlStatements\n"_str,
          "int f(){\n\nif(true){return 1;}return 0;}\nint g(){return 2;}\n"_str,
          "int f() {\n    if (true) { return 1; }\n    return 0;\n}\n"
          "int g() {\n    return 2;\n}\n"_str },
        { "MaxEmptyLinesToKeep: 1\nKeepEmptyLines:\n  AtStartOfFile: false\n"
          "  AtEndOfFile: false\n  AtStartOfBlock: false\n  AtEndOfBlock: false\n"_str,
          "\n\nint f(){\n\n\nint x;\n\n\nreturn 1;\n\n\n}\n\n\n"_str,
          "int f() {\n    int x;\n\n    return 1;\n}\n"_str },
        { "IndentWidth: 2\nMaxEmptyLinesToKeep: 2\nKeepEmptyLines:\n  AtStartOfFile: true\n"
          "  AtEndOfFile: true\n  AtStartOfBlock: true\n  AtEndOfBlock: true\n"_str,
          "\n\nint f(){\n\n\nreturn 1;\n\n\n}\n\n\n"_str,
          "\n\nint f() {\n\n\n  return 1;\n\n\n}\n\n\n"_str },
        { "---\nLanguage: Cpp\nColumnLimit: 0\nBasedOnStyle: LLVM\n...\n"_str,
          "namespace sample{int f(){return 1;}}\n"_str,
          "namespace sample {\nint f() { return 1; }\n}\n"_str },
    };
    for (const auto& test : cases) {
        auto configuration = parse_style_configuration(test.configuration);
        ASSERT_TRUE(configuration.is_ok()) << test.configuration;
        auto style     = configuration->apply();
        auto formatted = format_source(format_test_source(test.source), style);
        ASSERT_TRUE(formatted.is_ok()) << test.configuration;
        EXPECT_EQ(formatted->as_str(), test.expected) << test.configuration;
        auto repeated = format_source(format_test_source(formatted->as_str()), style);
        ASSERT_TRUE(repeated.is_ok());
        EXPECT_EQ(repeated->as_str(), formatted->as_str()) << test.configuration;
    }
}
