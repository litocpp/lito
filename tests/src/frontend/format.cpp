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

struct FormatStyleCase {
    ref<str> configuration;
    ref<str> source;
    ref<str> expected;
};

struct FormatMessageText {
    ref<str> text;
    auto     data() const -> const char* {
        return reinterpret_cast<const char*>(text.as_bytes().as_ptr().as_raw_ptr());
    }
    auto size() const -> rstd::size_t { return text.len().to_primitive(); }
};

auto report_format_text(const char* label, ref<str> text) -> void {
    for (usize begin {}; begin < text.len();) {
        auto end = begin + usize(768);
        if (end > text.len()) end = text.len();
        auto chunk = text.get(begin, end);
        while (chunk.is_none()) chunk = text.get(begin, --end);
        ADD_FAILURE() << label << " [" << begin.to_primitive() << "]:\n"
                      << FormatMessageText { *chunk };
        begin = end;
    }
}

auto expect_style_output(const FormatStyleCase& test) -> void {
    auto configuration = parse_style_configuration(test.configuration);
    ASSERT_TRUE(configuration.is_ok()) << FormatMessageText { test.configuration };
    auto style     = configuration->apply();
    auto formatted = format_source(format_test_source(test.source), style);
    ASSERT_TRUE(formatted.is_ok()) << FormatMessageText { test.configuration };
    if (formatted->as_str() != test.expected) {
        report_format_text("configuration", test.configuration);
        report_format_text("source", test.source);
        report_format_text("expected", test.expected);
        report_format_text("actual", formatted->as_str());
    }
    auto repeated = format_source(format_test_source(formatted->as_str()), style);
    ASSERT_TRUE(repeated.is_ok()) << FormatMessageText { test.configuration };
    if (repeated->as_str() != formatted->as_str()) {
        report_format_text("idempotence configuration", test.configuration);
        report_format_text("idempotence source", test.source);
        report_format_text("idempotence expected", formatted->as_str());
        report_format_text("idempotence actual", repeated->as_str());
    }
}

TEST(BuiltinFormat, NormalizesReferenceAliasesAndParenthesisRoles) {
    constexpr auto        source  = "using Value=int;int f(int x){int a=(int)x;int b=(Value)x;"
                                    "if(x>0){return ((x));}return (x);}void empty(){}\n"_str;
    constexpr auto        never   = "using Value = int;\nint f(int x) {\n    int a = (int)x;\n"
                                    "    int b = (Value)x;\n    if (x > 0) {\n        return ((x));\n"
                                    "    }\n    return (x);\n}\nvoid empty() {\n}\n"_str;
    constexpr auto        all     = "using Value = int;\nint f( int x ) {\n    int a = ( int )x;\n"
                                    "    int b = ( Value )x;\n    if ( x > 0 ) {\n        return ( ( x ) );\n"
                                    "    }\n    return ( x );\n}\nvoid empty( ) {\n}\n"_str;
    const FormatStyleCase cases[] = {
        { "SpacesInParens: Custom\nSpacesInParensOptions:\n  Other: true\n"
          "  InConditionalStatements: true\n  InCStyleCasts: true\n"
          "  InEmptyParentheses: true\n  ExceptDoubleParentheses: false\n"_str,
          source,
          all },
        { "SpacesInParens: Custom\nSpacesInParensOptions:\n  Other: true\n"
          "  InConditionalStatements: true\n  InCStyleCasts: true\n"
          "  InEmptyParentheses: true\n  ExceptDoubleParentheses: true\n"_str,
          source,
          "using Value = int;\nint f( int x ) {\n    int a = ( int )x;\n"
          "    int b = ( Value )x;\n    if ( x > 0 ) {\n        return (( x ));\n"
          "    }\n    return ( x );\n}\nvoid empty( ) {\n}\n"_str },
        { "SpacesInParentheses: true\nSpaceInEmptyParentheses: true\n"
          "SpacesInCStyleCastParentheses: true\nSpacesInConditionalStatement: true\n"
          "SpacesInParens: Custom\n"_str,
          source,
          never },
        { "SpacesInParens: Custom\nSpacesInConditionalStatement: true\n"
          "SpacesInCStyleCastParentheses: true\nSpaceInEmptyParentheses: true\n"
          "SpacesInParentheses: true\n"_str,
          source,
          never },
        { "SpacesInParens: Never\nSpacesInParentheses: true\n"
          "SpaceInEmptyParentheses: true\nSpacesInCStyleCastParentheses: true\n"_str,
          source,
          all },
        { "SpacesInCStyleCastParentheses: true\nSpaceInEmptyParentheses: true\n"
          "SpacesInParentheses: true\nSpacesInParens: Never\n"_str,
          source,
          all },
        { "SpacesInParens: Never\nSpacesInParensOptions:\n  Other: true\n"
          "  InCStyleCasts: true\n  InEmptyParentheses: true\n"_str,
          source,
          never },
        { "SpacesInConditionalStatement: true\n"_str,
          "int f(int x){if(x){return x;}return 0;}\n"_str,
          "int f(int x) {\n    if ( x ) {\n        return x;\n    }\n    return 0;\n}\n"_str },
        { "SpacesInCStyleCastParentheses: true\n"_str,
          "int f(double x){return (int)x;}\n"_str,
          "int f(double x) {\n    return ( int )x;\n}\n"_str },
        { "AllowShortFunctionsOnASingleLine: true\nAllowShortBlocksOnASingleLine: true\n"_str,
          "int f(){return 1;}void g(){if(true){run();}}\n"_str,
          "int f() { return 1; }\nvoid g() {\n    if (true) { run(); }\n}\n"_str },
        { "AllowShortFunctionsOnASingleLine: false\nAllowShortBlocksOnASingleLine: false\n"_str,
          "int f(){return 1;}void g(){if(true){run();}}\n"_str,
          "int f() {\n    return 1;\n}\nvoid g() {\n    if (true) {\n        run();\n    }\n}\n"_str },
        { "KeepEmptyLinesAtEOF: false\nKeepEmptyLinesAtTheStartOfBlocks: true\n"
          "KeepEmptyLines:\n  AtEndOfFile: true\n  AtStartOfBlock: false\n"_str,
          "int f(){\n\nreturn 1;}\n\n\n"_str,
          "int f() {\n    return 1;\n}\n\n\n"_str },
        { "KeepEmptyLines:\n  AtEndOfFile: true\n  AtStartOfBlock: false\n"
          "KeepEmptyLinesAtTheStartOfBlocks: true\nKeepEmptyLinesAtEOF: false\n"_str,
          "int f(){\n\nreturn 1;}\n\n\n"_str,
          "int f() {\n    return 1;\n}\n\n\n"_str },
        { "KeepEmptyLinesAtEOF: true\n"_str,
          "int f(){return 1;}\n\n\n"_str,
          "int f() {\n    return 1;\n}\n\n\n"_str },
        { "KeepEmptyLinesAtEOF: true\nKeepEmptyLinesAtTheStartOfBlocks: false\n"
          "KeepEmptyLines:\n  AtEndOfFile: false\n  AtStartOfBlock: true\n"_str,
          "int f(){\n\nreturn 1;}\n\n\n"_str,
          "int f() {\n\n    return 1;\n}\n"_str },
    };
    for (const auto& test : cases) expect_style_output(test);
}

TEST(BuiltinFormat, AppliesCustomWrappingWithoutLeakingInactiveFields) {
    constexpr auto records  = "class C{int c;};struct S{int s;};union U{int u;};"
                              "enum E{Value};namespace N{int n;}int f(){return 1;}\n"_str;
    constexpr auto wrapped  = "class C\n{\n    int c;\n};\nstruct S\n{\n    int s;\n};\n"
                              "union U\n{\n    int u;\n};\nenum E\n{\n    Value\n};\n"
                              "namespace N\n{\n    int n;\n}\nint f()\n{\n    return 1;\n}\n"_str;
    constexpr auto attached = "class C {\n    int c;\n};\nstruct S {\n    int s;\n};\n"
                              "union U {\n    int u;\n};\nenum E {\n    Value\n};\n"
                              "namespace N {\n    int n;\n}\nint f() {\n    return 1;\n}\n"_str;
    constexpr auto controls = "void f(){if(true){run();}else{stop();}"
                              "try{run();}catch(...){stop();}}\n"_str;
    constexpr auto indented_controls =
        "void f() {\n    if (true)\n        {\n            run();\n        }\n"
        "    else\n        {\n            stop();\n        }\n"
        "    try\n        {\n            run();\n        }\n"
        "    catch (...)\n        {\n            stop();\n        }\n}\n"_str;
    constexpr auto attached_controls =
        "void f() {\n    if (true) {\n        run();\n    } else {\n        stop();\n    }\n"
        "    try {\n        run();\n    } catch (...) {\n        stop();\n    }\n}\n"_str;
    constexpr auto        empty   = "class C{};struct S{};union U{};namespace N{};void f(){}\n"_str;
    const FormatStyleCase cases[] = {
        { "BreakBeforeBraces: Custom\nBraceWrapping:\n  AfterClass: true\n"
          "  AfterStruct: true\n  AfterUnion: true\n  AfterEnum: true\n"
          "  AfterNamespace: true\n  AfterFunction: true\n  IndentBraces: true\n"_str,
          records,
          wrapped },
        { "BreakBeforeBraces: Custom\nBraceWrapping:\n  AfterClass: false\n"
          "  AfterStruct: false\n  AfterUnion: false\n  AfterEnum: false\n"
          "  AfterNamespace: false\n  AfterFunction: false\n"_str,
          wrapped,
          attached },
        { "BreakBeforeBraces: Attach\nBraceWrapping:\n  AfterClass: true\n"
          "  AfterStruct: true\n  AfterUnion: true\n  AfterEnum: true\n"
          "  AfterNamespace: true\n  AfterFunction: true\n  IndentBraces: true\n"_str,
          records,
          attached },
        { "BreakBeforeBraces: Allman\nBraceWrapping:\n  AfterClass: false\n"
          "  AfterStruct: false\n  AfterUnion: false\n  AfterEnum: false\n"
          "  AfterNamespace: false\n  AfterFunction: false\n"_str,
          records,
          wrapped },
        { "BreakBeforeBraces: Custom\nBraceWrapping:\n  AfterControlStatement: Always\n"
          "  BeforeElse: true\n  BeforeCatch: true\n  IndentBraces: true\n"_str,
          controls,
          indented_controls },
        { "BreakBeforeBraces: Custom\nBraceWrapping:\n  AfterControlStatement: true\n"
          "  BeforeElse: true\n  BeforeCatch: true\n  IndentBraces: true\n"_str,
          controls,
          indented_controls },
        { "BreakBeforeBraces: Custom\nBraceWrapping:\n  AfterControlStatement: false\n"
          "  BeforeElse: false\n  BeforeCatch: false\n"_str,
          controls,
          attached_controls },
        { "BreakBeforeBraces: Custom\nBraceWrapping:\n  AfterControlStatement: Never\n"
          "  BeforeElse: false\n  BeforeCatch: false\n"_str,
          controls,
          attached_controls },
        { "BreakBeforeBraces: Custom\nBraceWrapping:\n  AfterControlStatement: MultiLine\n"_str,
          "void f(){if(true\n        && false){run();}if(true)\n{stop();}"
          "if constexpr(true){run();}}\n"_str,
          "void f() {\n    if (true\n        && false)\n    {\n        run();\n    }\n"
          "    if (true) {\n        stop();\n    }\n    if constexpr (true) {\n        run();\n    }\n}\n"_str },
        { "BreakBeforeBraces: Custom\nBraceWrapping:\n  AfterClass: true\n"
          "  AfterStruct: true\n  AfterUnion: true\n  AfterNamespace: true\n  AfterFunction: true\n"
          "  SplitEmptyFunction: false\n  SplitEmptyRecord: false\n  SplitEmptyNamespace: false\n"_str,
          empty,
          "class C\n{};\nstruct S\n{};\nunion U\n{};\nnamespace N\n{};\nvoid f()\n{}\n"_str },
        { "BreakBeforeBraces: Custom\nBraceWrapping:\n  AfterClass: true\n"
          "  AfterStruct: true\n  AfterUnion: true\n  AfterNamespace: true\n  AfterFunction: true\n"
          "  SplitEmptyFunction: true\n  SplitEmptyRecord: true\n  SplitEmptyNamespace: true\n"_str,
          empty,
          "class C\n{\n};\nstruct S\n{\n};\nunion U\n{\n};\nnamespace N\n{\n};\nvoid f()\n{\n}\n"_str },
        { "BreakBeforeBraces: Custom\nBraceWrapping:\n  AfterClass: false\n"
          "  AfterNamespace: false\n  AfterFunction: false\n  SplitEmptyFunction: true\n"
          "  SplitEmptyRecord: true\n  SplitEmptyNamespace: true\n"_str,
          "class C{};namespace N{};void f(){}\n"_str,
          "class C {\n};\nnamespace N {\n};\nvoid f() {\n}\n"_str },
        { "BreakBeforeBraces: Custom\nAllowShortFunctionsOnASingleLine: Empty\n"
          "BraceWrapping:\n  AfterFunction: true\n  SplitEmptyFunction: true\n"_str,
          "void empty(){}int full(){return 1;}void comment(){/* keep */}\n"_str,
          "void empty() {}\nint full()\n{\n    return 1;\n}\n"
          "void comment()\n{\n    /* keep */\n}\n"_str },
    };
    for (const auto& test : cases) expect_style_output(test);
}

TEST(BuiltinFormat, FormatsDirectRecordLabelsAndMembers) {
    constexpr auto        source  = "class C{public:int a;protected:int b;private:int c;};\n"_str;
    const FormatStyleCase cases[] = {
        { "IndentWidth: 4\n"_str,
          source,
          "class C {\n  public:\n    int a;\n  protected:\n    int b;\n  private:\n    int c;\n};\n"_str },
        { "AccessModifierOffset: 2\n"_str,
          "class C{public:int value;};\n"_str,
          "class C {\n      public:\n    int value;\n};\n"_str },
        { "AccessModifierOffset: -8\n"_str,
          "class C{public:int value;};\n"_str,
          "class C {\n    public:\n    int value;\n};\n"_str },
        { "IndentAccessModifiers: true\nAccessModifierOffset: 1000\n"_str,
          "class C{int before;public:int after;};struct S{int value;};enum class E{Value};\n"_str,
          "class C {\n        int before;\n    public:\n        int after;\n};\n"
          "struct S {\n        int value;\n};\nenum class E {\n    Value\n};\n"_str },
        { "IndentAccessModifiers: true\nAccessModifierOffset: -8\n"
          "UseTab: ForIndentation\nTabWidth: 4\n"_str,
          "class C{public:int value;};struct S{int value;};\n"_str,
          "class C {\n\tpublic:\n\t\tint value;\n};\nstruct S {\n\t\tint value;\n};\n"_str },
        { "IndentAccessModifiers: false\nAccessModifierOffset: -2\n"_str,
          "struct Base{int base;};class C : public Base{public:class Inner{public:int value;};};\n"_str,
          "struct Base {\n    int base;\n};\nclass C : public Base {\n  public:\n"
          "    class Inner {\n      public:\n        int value;\n    };\n};\n"_str },
    };
    for (const auto& test : cases) expect_style_output(test);
}

TEST(BuiltinFormat, WrapsArgumentsParametersAndOperandsAtFiniteColumns) {
    const FormatStyleCase cases[] = {
        { "ColumnLimit: 32\nAlignAfterOpenBracket: Align\nBinPackArguments: false\n"_str,
          "void f(){consume(first_long_value,second_long_value,third_long_value);}\n"_str,
          "void f() {\n    consume(first_long_value,\n            second_long_value,\n"
          "            third_long_value);\n}\n"_str },
        { "ColumnLimit: 40\nAlignAfterOpenBracket: Align\nBinPackParameters: OnePerLine\n"
          "AllowAllParametersOfDeclarationOnNextLine: false\nAlwaysBreakAfterReturnType: None\n"
          "IndentWrappedFunctionNames: false\n"_str,
          "int combine(int first_parameter,int second_parameter,int third_parameter){return first_parameter;}\n"_str,
          "int combine(int first_parameter,\n            int second_parameter,\n"
          "            int third_parameter) {\n    return first_parameter;\n}\n"_str },
        { "ColumnLimit: 32\nAlignAfterOpenBracket: Align\nBinPackParameters: OnePerLine\n"
          "AllowAllParametersOfDeclarationOnNextLine: false\nAlwaysBreakAfterReturnType: None\n"
          "IndentWrappedFunctionNames: false\n"_str,
          "SomeLongReturnTypeName function_name(int first,int second);\n"_str,
          "SomeLongReturnTypeName\nfunction_name(int first,\n              int second);\n"_str },
        { "ColumnLimit: 40\nAlignOperands: true\n"_str,
          "int f(){return first_operand+second_operand+third_operand;}\n"_str,
          "int f() {\n    return first_operand +\n           second_operand +\n"
          "           third_operand;\n}\n"_str },
    };
    for (const auto& test : cases) expect_style_output(test);
}

TEST(BuiltinFormat, AlignsConsecutiveGroupsAndOnlySafeMacroSlots) {
    const FormatStyleCase cases[] = {
        { "AlignConsecutiveAssignments: true\n"_str,
          "int a=1;\nint longer=2;\n\nint z=3;\n"_str,
          "int a      = 1;\nint longer = 2;\n\nint z = 3;\n"_str },
        { "AlignConsecutiveDeclarations: true\n"_str,
          "int a;\nlong longer;\n\nchar z;\n"_str,
          "int  a;\nlong longer;\n\nchar z;\n"_str },
        { "AlignTrailingComments: true\n"_str,
          "int a; // one\nint longer; // two\n\nint z; // three\n"_str,
          "int a;      // one\nint longer; // two\n\nint z; // three\n"_str },
        { "AlignConsecutiveMacros: true\n"_str,
          "#define A 1\n#define LONGER 2\n#define F(x) #x\n\nauto text = F(a+b);\n"_str,
          "#define A      1\n#define LONGER 2\n#define F(x)   #x\n\nauto text = F(a+b);\n"_str },
        { "AlignEscapedNewlines: Left\n"_str,
          "#define SUM(x) x +   \\\n  x + 1 \\\n  2\n\n#define WORD A\\\n1\n"_str,
          "#define SUM(x) x + \\\n  x + 1            \\\n  2\n\n#define WORD A\\\n1\n"_str },
    };
    for (const auto& test : cases) expect_style_output(test);
}

TEST(BuiltinFormat, AppliesDeclarationAndExpressionSpacing) {
    const FormatStyleCase cases[] = {
        { "PointerAlignment: Left\n"_str,
          "int *pointer;int f(int x){return *pointer*x;}\n"_str,
          "int* pointer;\nint f(int x) {\n    return *pointer * x;\n}\n"_str },
        { "SpaceAfterCStyleCast: false\n"_str,
          "int f(double value){return (int) value;}\n"_str,
          "int f(double value) {\n    return (int)value;\n}\n"_str },
        { "SpaceAfterLogicalNot: true\n"_str,
          "bool f(bool value){return !value;}\n"_str,
          "bool f(bool value) {\n    return ! value;\n}\n"_str },
        { "SpacesInAngles: false\n"_str, "Box< int > value;\n"_str, "Box<int> value;\n"_str },
        { "SpaceBeforeCpp11BracedList: true\nCpp11BracedListStyle: false\n"
          "SpacesInSquareBrackets: false\n"_str,
          "int f(){int values[ ]{1,2};return values[ 0 ];}\n"_str,
          "int f() {\n    int values[] { 1, 2 };\n    return values[0];\n}\n"_str },
        { "SpaceBeforeInheritanceColon: true\n"_str,
          "struct Base{int base;};struct Child:Base{int value;};\n"_str,
          "struct Base {\n    int base;\n};\nstruct Child : Base {\n    int value;\n};\n"_str },
        { "SpaceBeforeCtorInitializerColon: false\n"_str,
          "struct Pair{int x;int y;Pair(int a,int b) : x(a),y(b){}};\n"_str,
          "struct Pair {\n    int x;\n    int y;\n    Pair(int a, int b): x(a), y(b) {\n    }\n};\n"_str },
        { "SpaceBeforeRangeBasedForLoopColon: true\n"_str,
          "void f(){for(auto value:values){run(value);}}\n"_str,
          "void f() {\n    for (auto value : values) {\n        run(value);\n    }\n}\n"_str },
    };
    for (const auto& test : cases) expect_style_output(test);
}

TEST(BuiltinFormat, PlacesTemplateAttributesAndConstructorInitializers) {
    const FormatStyleCase cases[] = {
        { "AlwaysBreakTemplateDeclarations: true\nSpaceAfterTemplateKeyword: false\n"
          "SpacesInAngles: false\n"_str,
          "template < class T > T f(T value){return value;}\n"_str,
          "template<class T>\nT f(T value) {\n    return value;\n}\n"_str },
        { "BreakAfterAttributes: Always\n"_str,
          "[[nodiscard]] int f(){return 1;}\n"_str,
          "[[nodiscard]]\nint f() {\n    return 1;\n}\n"_str },
        { "BasedOnStyle: LLVM\nIndentWidth: 4\nColumnLimit: 60\n"
          "ConstructorInitializerAllOnOneLineOrOnePerLine: true\n"_str,
          "struct Pair{Pair():first(11111111),second(22222222),third(33333333){}};\n"_str,
          "struct Pair {\n    Pair()\n        : first(11111111),\n          second(22222222),\n"
          "          third(33333333) {\n    }\n};\n"_str },
    };
    for (const auto& test : cases) expect_style_output(test);
}

TEST(BuiltinFormat, DistinguishesShortStatementsCasesLambdasAndInlineMembers) {
    const FormatStyleCase cases[] = {
        { "AllowShortFunctionsOnASingleLine: InlineOnly\n"_str,
          "int f(){return 1;}struct S{int g(){return 2;}void h(){}};\n"_str,
          "int f() {\n    return 1;\n}\nstruct S {\n    int g() { return 2; }\n    void h() {}\n};\n"_str },
        { "AllowShortIfStatementsOnASingleLine: true\n"_str,
          "void f(){if(ready)\nrun();if(ready)\nrun();else\nstop();}\n"_str,
          "void f() {\n    if (ready) run();\n    if (ready)\n        run();\n"
          "    else\n        stop();\n}\n"_str },
        { "AllowShortLoopsOnASingleLine: true\n"_str,
          "void f(){for(int i=0;i<3;++i)\nrun(i);while(ready)\nwait();}\n"_str,
          "void f() {\n    for (int i = 0; i < 3; ++i) run(i);\n    while (ready) wait();\n}\n"_str },
        { "AllowShortLambdasOnASingleLine: false\nAllowShortFunctionsOnASingleLine: All\n"_str,
          "void f(){auto fn=[](int x){return x;};}\n"_str,
          "void f() {\n    auto fn = [](int x) {\n        return x;\n    };\n}\n"_str },
        { "AllowShortCaseLabelsOnASingleLine: true\nIndentCaseLabels: false\n"_str,
          "int f(int value){switch(value){case 0:return 1;case 1:return 2;default:return 0;}}\n"_str,
          "int f(int value) {\n    switch (value) {\n    case 0: return 1;\n"
          "    case 1: return 2;\n    default: return 0;\n    }\n}\n"_str },
    };
    for (const auto& test : cases) expect_style_output(test);
}

TEST(BuiltinFormat, PreservesExplicitOrderingAndOpaqueTextPolicies) {
    const FormatStyleCase cases[] = {
        { "SortIncludes: Never\nSortUsingDeclarations: Never\nReflowComments: false\n"
          "CompactNamespaces: false\nStandard: Latest\nSpacesInContainerLiterals: false\n"_str,
          "#include \"z.hpp\"\n#include \"a.hpp\"\nusing Z=int;using A=int;\n"
          "// keep    every   word and a deliberately long comment without wrapping or reflow\n"
          "namespace a{namespace b{int value;}}\n"_str,
          "#include \"z.hpp\"\n#include \"a.hpp\"\nusing Z = int;\nusing A = int;\n"
          "// keep    every   word and a deliberately long comment without wrapping or reflow\n"
          "namespace a {\n    namespace b {\n        int value;\n    }\n}\n"_str },
        { "IndentPPDirectives: None\n"_str,
          "    #  define VALUE 1\n\t# include \"header.hpp\"\n\nint f(){return VALUE;}\n"_str,
          "#define VALUE 1\n#include \"header.hpp\"\n\nint f() {\n    return VALUE;\n}\n"_str },
    };
    for (const auto& test : cases) expect_style_output(test);
}

auto test_repository_style_configuration() -> ref<str> {
    return "\xef\xbb\xbf"
           R"yaml(---
BasedOnStyle: LLVM

AccessModifierOffset: -4

AlignAfterOpenBracket: Align
AlignConsecutiveMacros: true
AlignConsecutiveAssignments: true
AlignConsecutiveDeclarations: true
AlignEscapedNewlines: Left
AlignOperands: true
AlignTrailingComments: true

AllowAllParametersOfDeclarationOnNextLine: false
AllowShortBlocksOnASingleLine: false
AllowShortCaseLabelsOnASingleLine: true
AllowShortIfStatementsOnASingleLine: true
AllowShortLoopsOnASingleLine: true
AllowShortLambdasOnASingleLine: false
AllowShortFunctionsOnASingleLine: InlineOnly

AlwaysBreakAfterReturnType: None
AlwaysBreakTemplateDeclarations: true

BreakBeforeBraces: Custom
BraceWrapping:
  AfterEnum: true
  AfterNamespace: true
  SplitEmptyFunction: false
  SplitEmptyRecord: false
BinPackArguments: false
BinPackParameters: OnePerLine
CompactNamespaces: false
ConstructorInitializerAllOnOneLineOrOnePerLine: true
Cpp11BracedListStyle: false
IndentCaseLabels: false
IndentPPDirectives: None
IndentWidth: 4
IndentWrappedFunctionNames: false
KeepEmptyLinesAtTheStartOfBlocks: false
Language: Cpp
PointerAlignment: Left
ReflowComments: false
SortIncludes: Never
SortUsingDeclarations: Never
SpaceAfterCStyleCast: false
SpaceAfterLogicalNot: true
SpaceAfterTemplateKeyword: false
SpaceBeforeAssignmentOperators: true
SpaceBeforeCpp11BracedList: true
SpaceBeforeCtorInitializerColon: false
SpaceBeforeInheritanceColon: true
SpaceBeforeRangeBasedForLoopColon: true
SpaceInEmptyParentheses: false
SpacesInAngles: false
SpacesInCStyleCastParentheses: false
SpacesInContainerLiterals: false
SpacesInParentheses: false
SpacesInSquareBrackets: false
Standard: Latest
TabWidth: 4
UseTab: Never
ColumnLimit: 100

BreakAfterAttributes: Always
)yaml"_str;
}

auto test_repository_style_source() -> ref<str> {
    return R"cpp(#include "z.hpp"
#include "a.hpp"

using Zulu=int;
using Alfa=int;

#define A 1
#define LONGER 2

namespace demo{
template<class T>[[nodiscard]] T *identity(T *value){if(!value)return nullptr;return value;}
struct Pair:Base{int first;long second;public:Pair(int a,int b):first(a),second(b){}int get(){return first;}};
int plain(){return 1;}
void run(){int values[ ]{1,2};for(auto value:values)use(value);while(!ready)wait();auto fn=[](int value){return value;};switch(values[ 0 ]){case 0:break;default:use(values[0]);}invoke(first_argument_name_with_extra_padding_000000000000,second_argument_name_with_extra_padding_000000000000,third_argument_name_with_extra_padding_000000000000);}
}
)cpp"_str;
}

auto test_repository_style_expected() -> ref<str> {
    return R"cpp(#include "z.hpp"
#include "a.hpp"

using Zulu = int;
using Alfa = int;

#define A      1
#define LONGER 2

namespace demo
{
template<class T>
[[nodiscard]]
T* identity(T* value) {
    if (! value) return nullptr;
    return value;
}
struct Pair : Base {
    int  first;
    long second;
public:
    Pair(int a, int b): first(a), second(b) {}
    int get() { return first; }
};
int plain() {
    return 1;
}
void run() {
    int values[] { 1, 2 };
    for (auto value : values) use(value);
    while (! ready) wait();
    auto fn = [](int value) {
        return value;
    };
    switch (values[0]) {
    case 0: break;
    default: use(values[0]);
    }
    invoke(first_argument_name_with_extra_padding_000000000000,
           second_argument_name_with_extra_padding_000000000000,
           third_argument_name_with_extra_padding_000000000000);
}
}
)cpp"_str;
}

TEST(BuiltinFormat, AppliesTheCompleteRepositoryConfiguration) {
    expect_style_output(FormatStyleCase {
        test_repository_style_configuration(),
        test_repository_style_source(),
        test_repository_style_expected(),
    });
}

TEST(BuiltinFormat, PreservesAmbiguousAnglesWithoutFreezingSurroundingCode) {
    const FormatStyleCase cases[] = {
        { "SpacesInAngles: false\n"_str,
          "vector< T > value;\nvoid f(){if(check<T &&>::value){run();}if(check<T &&>()){run();}}\n"_str,
          "vector< T > value;\nvoid f() {\n    if (check<T &&>::value) {\n        run();\n    }\n"
          "    if (check<T &&>()) {\n        run();\n    }\n}\n"_str },
        { "SpacesInAngles: false\n"_str,
          "auto x=box<T &&>{};\n"_str,
          "auto x = box<T &&>{};\n"_str },
        { "SpacesInAngles: false\n"_str,
          "template<bool B = X && Y> using selected=int;\n"_str,
          "template<bool B = X && Y> using selected = int;\n"_str },
        { "SpacesInAngles: false\n"_str,
          "void f(){if(a<b && c>d){run();}}\n"_str,
          "void f() {\n    if (a < b && c > d) {\n        run();\n    }\n}\n"_str },
    };
    for (const auto& test : cases) expect_style_output(test);
}
