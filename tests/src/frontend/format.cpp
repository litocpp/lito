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

TEST(BuiltinFormat, BreaksFunctionAndInitializerBracketsOnlyWhenNeeded) {
    constexpr auto call =
        "void f(){consume(first_long_value,second_long_value,third_long_value);}\n"_str;
    constexpr auto ordinary =
        "void f() {\n    consume(first_long_value,\n            second_long_value,\n"
        "            third_long_value);\n}\n"_str;
    constexpr auto values =
        "void f(){int values[]{first_long_value,second_long_value,third_long_value};}\n"_str;
    const FormatStyleCase cases[] = {
        { "ColumnLimit: 40\nBinPackArguments: false\nBinPackParameters: OnePerLine\n"
          "AllowAllParametersOfDeclarationOnNextLine: false\n"
          "BreakAfterOpenBracketFunction: true\nBreakBeforeCloseBracketFunction: true\n"_str,
          "int combine(int first_parameter,int second_parameter,int third_parameter);"
          "int combine(int first_parameter,int second_parameter,int third_parameter){return 0;}"
          "void f(){consume(first_long_value,second_long_value,third_long_value);}\n"_str,
          "int combine(\n    int first_parameter,\n    int second_parameter,\n    int "
          "third_parameter\n);\n"
          "int combine(\n    int first_parameter,\n    int second_parameter,\n    int "
          "third_parameter\n) {\n"
          "    return 0;\n}\nvoid f() {\n    consume(\n        first_long_value,\n"
          "        second_long_value,\n        third_long_value\n    );\n}\n"_str },
        { "ColumnLimit: 40\nBinPackArguments: false\nBreakAfterOpenBracketFunction: true\n"
          "BreakBeforeCloseBracketFunction: false\n"_str,
          call,
          "void f() {\n    consume(\n        first_long_value,\n        second_long_value,\n"
          "        third_long_value);\n}\n"_str },
        { "ColumnLimit: 40\nBinPackArguments: false\nAlignAfterOpenBracket: Align\n"
          "BreakAfterOpenBracketFunction: false\nBreakBeforeCloseBracketFunction: true\n"_str,
          call,
          ordinary },
        { "ColumnLimit: 40\nBinPackArguments: false\nAlignAfterOpenBracket: BlockIndent\n"
          "BreakAfterOpenBracketFunction: false\nBreakBeforeCloseBracketFunction: false\n"_str,
          call,
          ordinary },
        { "BreakBeforeCloseBracketFunction: false\nBreakAfterOpenBracketFunction: false\n"
          "AlignAfterOpenBracket: BlockIndent\nColumnLimit: 40\nBinPackArguments: false\n"_str,
          call,
          ordinary },
        { "ColumnLimit: 100\nBreakAfterOpenBracketFunction: true\n"
          "BreakBeforeCloseBracketFunction: true\n"_str,
          call,
          "void f() {\n    consume(first_long_value, second_long_value, third_long_value);\n}\n"_str },
        { "ColumnLimit: 0\nBreakAfterOpenBracketFunction: true\n"
          "BreakBeforeCloseBracketFunction: true\n"_str,
          call,
          "void f() {\n    consume(first_long_value, second_long_value, third_long_value);\n}\n"_str },
        { "ColumnLimit: 40\nBinPackArguments: false\nBreakAfterOpenBracketFunction: true\n"
          "BreakBeforeCloseBracketFunction: true\n"_str,
          "void f(){outer(inner(first_long_value,second_long_value),third_long_value);}\n"_str,
          "void f() {\n    outer(\n        inner(\n            first_long_value,\n"
          "            second_long_value\n        ),\n        third_long_value\n    );\n}\n"_str },
        { "ColumnLimit: 40\nBinPackArguments: false\nAlignOperands: false\n"
          "BreakAfterOpenBracketFunction: true\nBreakBeforeCloseBracketFunction: true\n"_str,
          "void f(){outer(inner(first_operand+second_operand),third_argument);}\n"_str,
          "void f() {\n    outer(\n        inner(\n            first_operand +\n"
          "            second_operand\n        ),\n        third_argument\n    );\n}\n"_str },
        { "ColumnLimit: 40\nBinPackArguments: false\nCpp11BracedListStyle: true\n"
          "SpaceBeforeCpp11BracedList: true\nBreakAfterOpenBracketBracedList: true\n"
          "BreakBeforeCloseBracketBracedList: true\n"_str,
          values,
          "void f() {\n    int values[] {\n        first_long_value,\n        second_long_value,\n"
          "        third_long_value\n    };\n}\n"_str },
        { "ColumnLimit: 40\nBinPackArguments: false\nCpp11BracedListStyle: true\n"
          "SpaceBeforeCpp11BracedList: true\nBreakAfterOpenBracketBracedList: true\n"
          "BreakBeforeCloseBracketBracedList: false\n"_str,
          values,
          "void f() {\n    int values[] {\n        first_long_value,\n        second_long_value,\n"
          "        third_long_value};\n}\n"_str },
        { "ColumnLimit: 40\nBinPackArguments: false\nCpp11BracedListStyle: true\n"
          "SpaceBeforeCpp11BracedList: true\nBreakAfterOpenBracketBracedList: false\n"
          "BreakBeforeCloseBracketBracedList: true\n"_str,
          values,
          "void f() {\n    int values[] {first_long_value,\n        second_long_value,\n"
          "        third_long_value};\n}\n"_str },
        { "ColumnLimit: 40\nBinPackArguments: false\nCpp11BracedListStyle: false\n"
          "SpaceBeforeCpp11BracedList: true\nBreakAfterOpenBracketBracedList: true\n"
          "BreakBeforeCloseBracketBracedList: true\n"_str,
          values,
          "void f() {\n    int values[] {\n        first_long_value,\n        second_long_value,\n"
          "        third_long_value\n    };\n}\n"_str },
        { "ColumnLimit: 40\nBinPackArguments: false\nCpp11BracedListStyle: false\n"
          "SpaceBeforeCpp11BracedList: true\nBreakAfterOpenBracketBracedList: false\n"
          "BreakBeforeCloseBracketBracedList: false\n"_str,
          values,
          "void f() {\n    int values[] {\n        first_long_value,\n        second_long_value,\n"
          "        third_long_value\n    };\n}\n"_str },
    };
    for (const auto& test : cases) expect_style_output(test);
}

TEST(BuiltinFormat, KeepsWholeNextLineArgumentsSeparateFromParametersAndConstructors) {
    constexpr auto call =
        "void f(){FunctionCallWithReallyLongName(first_value,second_value);}\n"_str;
    constexpr auto        values = "auto variable_with_long_name={first_value,second_value};\n"_str;
    const FormatStyleCase cases[] = {
        { "ColumnLimit: 40\nBinPackArguments: false\nAllowAllArgumentsOnNextLine: true\n"_str,
          call,
          "void f() {\n    FunctionCallWithReallyLongName(\n        first_value, second_value);\n}\n"_str },
        { "ColumnLimit: 40\nBinPackArguments: false\nAllowAllArgumentsOnNextLine: false\n"_str,
          call,
          "void f() {\n    FunctionCallWithReallyLongName(\n        first_value,\n        second_value);\n}\n"_str },
        { "ColumnLimit: 34\nBinPackArguments: false\nAllowAllArgumentsOnNextLine: true\n"_str,
          "void f(){consume(first_value,second_value);}\n"_str,
          "void f() {\n    consume(\n        first_value,\n        second_value);\n}\n"_str },
        { "ColumnLimit: 40\nBinPackArguments: false\nAllowAllArgumentsOnNextLine: true\n"_str,
          values,
          "auto variable_with_long_name = {\n    first_value, second_value};\n"_str },
        { "ColumnLimit: 40\nBinPackArguments: false\nAllowAllArgumentsOnNextLine: false\n"_str,
          values,
          "auto variable_with_long_name = {\n    first_value,\n    second_value};\n"_str },
        { "ColumnLimit: 40\nBinPackParameters: OnePerLine\n"
          "AllowAllParametersOfDeclarationOnNextLine: false\nAllowAllArgumentsOnNextLine: true\n"_str,
          "void FunctionCallWithReallyLongName(int first,int second);\n"_str,
          "void FunctionCallWithReallyLongName(\n    int first,\n    int second);\n"_str },
        { "ColumnLimit: 40\nPackConstructorInitializers: Never\nAllowAllArgumentsOnNextLine: true\n"_str,
          "struct Pair{Pair():first(x),second(y){}};\n"_str,
          "struct Pair {\n    Pair()\n        : first(x),\n          second(y) {\n    }\n};\n"_str },
        { "ColumnLimit: 24\nBinPackArguments: false\nAllowAllArgumentsOnNextLine: true\n"
          "BreakBeforeCloseBracketFunction: true\n"_str,
          "void f(){very_long_call(first,secondxxx);}\n"_str,
          "void f() {\n    very_long_call(\n        first, secondxxx\n    );\n}\n"_str },
        { "ColumnLimit: 24\nBinPackArguments: false\nAllowAllArgumentsOnNextLine: true\n"
          "Cpp11BracedListStyle: false\nSpaceBeforeCpp11BracedList: true\n"_str,
          "void f(){int values[]{first,secondxxx};}\n"_str,
          "void f() {\n    int values[] {\n        first, secondxxx\n    };\n}\n"_str },
    };
    for (const auto& test : cases) expect_style_output(test);
}

TEST(BuiltinFormat, KeepsControlBracketKindsAndClosingStateSeparate) {
    constexpr auto source = "void f(){if(first_condition&&second_condition){run();}"
                            "while(first_condition&&second_condition){run();}"
                            "switch(first_operand+second_operand){default:run();}}\n"_str;
    constexpr auto only_if =
        "void f() {\n    if (\n        first_condition &&\n        second_condition\n    ) {\n"
        "        run();\n    }\n    while (first_condition &&\n        second_condition) {\n"
        "        run();\n    }\n    switch (first_operand +\n        second_operand) {\n"
        "    default: run();\n    }\n}\n"_str;
    const FormatStyleCase cases[] = {
        { "ColumnLimit: 40\nBreakAfterOpenBracketIf: true\nBreakBeforeCloseBracketIf: true\n"
          "BreakBeforeBraces: Custom\nBraceWrapping:\n  AfterControlStatement: MultiLine\n"_str,
          "void f(){if(first_condition&&second_condition){run();}}\n"_str,
          "void f() {\n    if (\n        first_condition &&\n        second_condition\n    )\n"
          "    {\n        run();\n    }\n}\n"_str },
        { "ColumnLimit: 100\nBreakBeforeBraces: Custom\n"
          "BraceWrapping:\n  AfterControlStatement: MultiLine\n"_str,
          "void f(){if(true\n && false){run();}}\n"_str,
          "void f() {\n    if (true && false) {\n        run();\n    }\n}\n"_str },
        { "ColumnLimit: 40\nAlignOperands: false\nAllowShortCaseLabelsOnASingleLine: true\n"
          "BreakAfterOpenBracketIf: true\nBreakBeforeCloseBracketIf: true\n"_str,
          source,
          only_if },
        { "ColumnLimit: 40\nAlignOperands: false\nAllowShortCaseLabelsOnASingleLine: true\n"
          "AlignAfterOpenBracket: BlockIndent\n"_str,
          source,
          only_if },
        { "ColumnLimit: 40\nAlignOperands: false\nAllowShortCaseLabelsOnASingleLine: true\n"
          "AlignAfterOpenBracket: AlwaysBreak\n"_str,
          source,
          "void f() {\n    if (\n        first_condition &&\n        second_condition) {\n"
          "        run();\n    }\n    while (first_condition &&\n        second_condition) {\n"
          "        run();\n    }\n    switch (first_operand +\n        second_operand) {\n"
          "    default: run();\n    }\n}\n"_str },
        { "ColumnLimit: 40\nAlignOperands: false\nAllowShortCaseLabelsOnASingleLine: true\n"
          "BreakAfterOpenBracketLoop: true\nBreakBeforeCloseBracketLoop: true\n"_str,
          source,
          "void f() {\n    if (first_condition &&\n        second_condition) {\n        run();\n   "
          " }\n"
          "    while (\n        first_condition &&\n        second_condition\n    ) {\n"
          "        run();\n    }\n    switch (first_operand +\n        second_operand) {\n"
          "    default: run();\n    }\n}\n"_str },
        { "ColumnLimit: 40\nAlignOperands: false\nAllowShortCaseLabelsOnASingleLine: true\n"
          "BreakAfterOpenBracketSwitch: true\nBreakBeforeCloseBracketSwitch: true\n"_str,
          source,
          "void f() {\n    if (first_condition &&\n        second_condition) {\n        run();\n   "
          " }\n"
          "    while (first_condition &&\n        second_condition) {\n        run();\n    }\n"
          "    switch (\n        first_operand + second_operand\n    ) {\n    default: run();\n    }\n}\n"_str },
        { "ColumnLimit: 40\nAlignOperands: false\nAllowShortCaseLabelsOnASingleLine: true\n"
          "BreakAfterOpenBracketIf: true\nBreakAfterOpenBracketLoop: true\n"
          "BreakAfterOpenBracketSwitch: true\nBreakBeforeCloseBracketIf: false\n"
          "BreakBeforeCloseBracketLoop: false\nBreakBeforeCloseBracketSwitch: false\n"_str,
          source,
          "void f() {\n    if (\n        first_condition &&\n        second_condition) {\n"
          "        run();\n    }\n    while (\n        first_condition &&\n        "
          "second_condition) {\n"
          "        run();\n    }\n    switch (\n        first_operand +\n        second_operand) "
          "{\n"
          "    default: run();\n    }\n}\n"_str },
        { "ColumnLimit: 40\nAlignOperands: false\nBreakAfterOpenBracketLoop: true\n"
          "BreakBeforeCloseBracketLoop: true\nBreakAfterOpenBracketIf: true\n"
          "BreakBeforeCloseBracketIf: true\n"_str,
          "void f(){for(int index=0;index<limit;++index){run();}for(auto item:items){run();}"
          "do{run();}while(first_condition&&second_condition);"
          "if constexpr(first_condition&&second_condition){run();}}\n"_str,
          "void f() {\n    for (\n        int index = 0;\n        index < limit;\n        ++index\n"
          "    ) {\n        run();\n    }\n    for (auto item : items) {\n        run();\n    }\n"
          "    do {\n        run();\n    } while (\n        first_condition &&\n        "
          "second_condition\n"
          "    );\n    if constexpr (\n        first_condition &&\n        second_condition\n    ) "
          "{\n"
          "        run();\n    }\n}\n"_str },
    };
    for (const auto& test : cases) expect_style_output(test);
}

TEST(BuiltinFormat, BreaksOnlyTheSelectedBinaryAndPairedTernarySides) {
    constexpr auto binary = "int long_result_variable=first_value;"
                            "int sum=first_value+second_value+third_value;\n"_str;
    constexpr auto conditional =
        "int result=long_condition?long_first_value:long_second_value;\n"_str;
    const FormatStyleCase cases[] = {
        { "ColumnLimit: 32\nAlignOperands: false\nBreakBeforeBinaryOperators: None\n"_str,
          binary,
          "int long_result_variable =\n    first_value;\nint sum = first_value +\n"
          "    second_value + third_value;\n"_str },
        { "ColumnLimit: 32\nAlignOperands: false\nBreakBeforeBinaryOperators: NonAssignment\n"_str,
          binary,
          "int long_result_variable =\n    first_value;\nint sum = first_value\n"
          "    + second_value\n    + third_value;\n"_str },
        { "ColumnLimit: 32\nAlignOperands: false\nBreakBeforeBinaryOperators: All\n"_str,
          binary,
          "int long_result_variable\n    = first_value;\nint sum = first_value\n"
          "    + second_value\n    + third_value;\n"_str },
        { "ColumnLimit: 32\nAlignOperands: false\nBreakBeforeBinaryOperators: All\n"
          "PointerAlignment: Left\n"_str,
          "void f(){long_result_variable+=first_value;int *pointer;int value=-*pointer;}\n"_str,
          "void f() {\n    long_result_variable\n        += first_value;\n    int* pointer;\n"
          "    int value = -*pointer;\n}\n"_str },
        { "ColumnLimit: 32\nAlignOperands: false\nBreakBeforeTernaryOperators: true\n"_str,
          conditional,
          "int result = long_condition\n    ? long_first_value\n    : long_second_value;\n"_str },
        { "ColumnLimit: 32\nAlignOperands: false\nBreakBeforeTernaryOperators: false\n"_str,
          conditional,
          "int result = long_condition ?\n    long_first_value :\n    long_second_value;\n"_str },
        { "ColumnLimit: 36\nAlignOperands: false\nBreakBeforeTernaryOperators: true\n"_str,
          "int result=outer_condition?first_value:inner_condition?second_value:third_value;\n"_str,
          "int result = outer_condition\n    ? first_value\n    : inner_condition\n"
          "        ? second_value\n        : third_value;\n"_str },
        { "ColumnLimit: 0\nBreakBeforeBinaryOperators: All\nBreakBeforeTernaryOperators: true\n"
          "PointerAlignment: Left\nBreakAfterOpenBracketLoop: true\nBreakBeforeCloseBracketLoop: true\n"_str,
          "void f(){for(int* p=first;p!=last;++p){run(p);}int x=+ +value;}\n"_str,
          "void f() {\n    for (int* p = first; p != last; ++p) {\n        run(p);\n    }\n"
          "    int x = + +value;\n}\n"_str },
        { "ColumnLimit: 0\nBreakBeforeBinaryOperators: All\nBreakBeforeTernaryOperators: true\n"
          "AllowShortCaseLabelsOnASingleLine: true\n"_str,
          "void f(){for(auto x:items){switch(x){case 0:break;default:run();}}}\n"_str,
          "void f() {\n    for (auto x : items) {\n        switch (x) {\n        case 0: break;\n"
          "        default: run();\n        }\n    }\n}\n"_str },
        { "ColumnLimit: 20\nBreakBeforeBinaryOperators: All\nBreakBeforeTernaryOperators: true\n"
          "PointerAlignment: Left\n"_str,
          "#define PICK(c,a,b) ((c)?(a):(b))\n// lito-format off\n"
          "int x=condition?first:second;\n// lito-format on\nint *pointer;\n"_str,
          "#define PICK(c,a,b) ((c)?(a):(b))\n// lito-format off\n"
          "int x=condition?first:second;\n// lito-format on\nint* pointer;\n"_str },
    };
    for (const auto& test : cases) expect_style_output(test);
}

TEST(BuiltinFormat, AlignsReferencesAndDerivesEachFileFromOriginalDeclarations) {
    constexpr auto        declarations = "int *pointer;int &lref;int &&rref;\n"_str;
    constexpr auto        left_votes   = "int* a;int* b;int* c;int *d;int &ref;\n"_str;
    const FormatStyleCase cases[]      = {
        { "PointerAlignment: Left\nReferenceAlignment: Pointer\n"_str,
          declarations,
          "int* pointer;\nint& lref;\nint&& rref;\n"_str },
        { "PointerAlignment: Right\nReferenceAlignment: Left\n"_str,
          declarations,
          "int *pointer;\nint& lref;\nint&& rref;\n"_str },
        { "PointerAlignment: Left\nReferenceAlignment: Right\n"_str,
          declarations,
          "int* pointer;\nint &lref;\nint &&rref;\n"_str },
        { "PointerAlignment: Left\nReferenceAlignment: Middle\n"_str,
          declarations,
          "int* pointer;\nint & lref;\nint && rref;\n"_str },
        { "PointerAlignment: Left\nReferenceAlignment: Right\n"_str,
          "int *f(int *p,int &r,int &&rr){return p;}\n"_str,
          "int* f(int* p, int &r, int &&rr) {\n    return p;\n}\n"_str },
        { "DerivePointerAlignment: true\nPointerAlignment: Right\nReferenceAlignment: Right\n"_str,
          left_votes,
          "int* a;\nint* b;\nint* c;\nint* d;\nint& ref;\n"_str },
        { "DerivePointerAlignment: true\nPointerAlignment: Left\nReferenceAlignment: Left\n"_str,
          "int *a;int *b;int *c;int*d;int& ref;\n"_str,
          "int *a;\nint *b;\nint *c;\nint *d;\nint &ref;\n"_str },
        { "DerivePointerAlignment: true\nPointerAlignment: Right\nReferenceAlignment: Left\n"_str,
          "int* left;int *right;int * middle;int*none;int & ref;\n"_str,
          "int *left;\nint *right;\nint *middle;\nint *none;\nint &ref;\n"_str },
        { "DerivePointerAlignment: false\nPointerAlignment: Right\nReferenceAlignment: Left\n"_str,
          left_votes,
          "int *a;\nint *b;\nint *c;\nint *d;\nint& ref;\n"_str },
        { "DerivePointerAlignment: true\nPointerAlignment: Left\nReferenceAlignment: Left\n"_str,
          "#define LEFT int* macro_a;int* macro_b;\n"
          "// lito-format off\nint* off_a;int* off_b;\n// lito-format on\n"
          "#if 1\nint* selected_a;int* selected_b;\n#endif\nint *editable;\n"_str,
          "#define LEFT int* macro_a;int* macro_b;\n"
          "// lito-format off\nint* off_a;int* off_b;\n// lito-format on\n"
          "#if 1\nint* selected_a;int* selected_b;\n#endif\nint *editable;\n"_str },
    };
    for (const auto& test : cases) expect_style_output(test);
}

TEST(BuiltinFormat, SpacesPointerQualifiersWithoutChangingExpressionRoles) {
    constexpr auto        declarations = "void * const * x;void * volatile * y;\n"_str;
    const FormatStyleCase cases[]      = {
        { "PointerAlignment: Left\nSpaceAroundPointerQualifiers: Default\n"_str,
          declarations,
          "void* const* x;\nvoid* volatile* y;\n"_str },
        { "PointerAlignment: Left\nSpaceAroundPointerQualifiers: Before\n"_str,
          declarations,
          "void* const* x;\nvoid* volatile* y;\n"_str },
        { "PointerAlignment: Left\nSpaceAroundPointerQualifiers: After\n"_str,
          declarations,
          "void* const * x;\nvoid* volatile * y;\n"_str },
        { "PointerAlignment: Left\nSpaceAroundPointerQualifiers: Both\n"_str,
          declarations,
          "void* const * x;\nvoid* volatile * y;\n"_str },
        { "PointerAlignment: Right\nSpaceAroundPointerQualifiers: Default\n"_str,
          declarations,
          "void *const *x;\nvoid *volatile *y;\n"_str },
        { "PointerAlignment: Right\nSpaceAroundPointerQualifiers: Both\n"_str,
          declarations,
          "void * const *x;\nvoid * volatile *y;\n"_str },
        { "PointerAlignment: Middle\nSpaceAroundPointerQualifiers: After\n"_str,
          declarations,
          "void * const * x;\nvoid * volatile * y;\n"_str },
        { "PointerAlignment: Left\nReferenceAlignment: Middle\n"
          "SpaceAroundPointerQualifiers: Both\n"_str,
          "int f(int a,int b){const int x=1;int *p=&a;int &&r=make();return a*b&&*p;}\n"_str,
          "int f(int a, int b) {\n    const int x = 1;\n    int* p = &a;\n"
          "    int && r = make();\n    return a * b && *p;\n}\n"_str },
    };
    for (const auto& test : cases) expect_style_output(test);
}

TEST(BuiltinFormat, SeparatesBitfieldCaseArrayAndGotoRoles) {
    constexpr auto fields = "struct S{unsigned bits:3;unsigned :0;};\n"_str;
    constexpr auto arrays =
        "[[nodiscard]] int f(){int values[2][3];auto [first, second] = values;"
        "auto pick=[](){return 0;};switch(values[0][1]){case 1:break;default:break;}"
        "return pick();}\n"_str;
    constexpr auto labels =
        "void f(){top:goto done;if(true){inner:again:;goto inner;}done:;}\n"_str;
    const FormatStyleCase cases[] = {
        { "BitFieldColonSpacing: Both\n"_str,
          fields,
          "struct S {\n    unsigned bits : 3;\n    unsigned : 0;\n};\n"_str },
        { "BitFieldColonSpacing: None\n"_str,
          fields,
          "struct S {\n    unsigned bits:3;\n    unsigned:0;\n};\n"_str },
        { "BitFieldColonSpacing: Before\n"_str,
          fields,
          "struct S {\n    unsigned bits :3;\n    unsigned :0;\n};\n"_str },
        { "BitFieldColonSpacing: After\n"_str,
          fields,
          "struct S {\n    unsigned bits: 3;\n    unsigned: 0;\n};\n"_str },
        { "SpaceBeforeSquareBrackets: true\nSpaceBeforeCaseColon: true\n"
          "AllowShortCaseLabelsOnASingleLine: true\nAllowShortLambdasOnASingleLine: All\n"_str,
          arrays,
          "[[nodiscard]] int f() {\n    int values [2][3];\n"
          "    auto [first, second] = values;\n    auto pick = []() { return 0; };\n"
          "    switch (values [0][1]) {\n    case 1 : break;\n    default : break;\n    }\n"
          "    return pick();\n}\n"_str },
        { "SpaceBeforeSquareBrackets: false\nSpacesInSquareBrackets: true\n"
          "SpaceBeforeCaseColon: false\nAllowShortCaseLabelsOnASingleLine: true\n"
          "AllowShortLambdasOnASingleLine: All\n"_str,
          "void f(){int values [2][3];switch(values [0][1]){case 1 :break;default :break;}"
          "auto pick=[](){return 0;};}\n"_str,
          "void f() {\n    int values[ 2 ][ 3 ];\n    switch (values[ 0 ][ 1 ]) {\n"
          "    case 1: break;\n    default: break;\n    }\n"
          "    auto pick = []() { return 0; };\n}\n"_str },
        { "IndentGotoLabels: true\n"_str,
          labels,
          "void f() {\ntop:\n    goto done;\n    if (true) {\n    inner:\n    again:\n"
          "        ;\n        goto inner;\n    }\ndone:\n    ;\n}\n"_str },
        { "IndentGotoLabels: false\n"_str,
          labels,
          "void f() {\ntop:\n    goto done;\n    if (true) {\ninner:\nagain:\n"
          "        ;\n        goto inner;\n    }\ndone:\n    ;\n}\n"_str },
        { "BitFieldColonSpacing: None\nSpaceBeforeCaseColon: true\nIndentGotoLabels: false\n"
          "AllowShortCaseLabelsOnASingleLine: true\n"_str,
          "struct S:Base{S():value(1){}int value;};"
          "void f(){for(auto item:items){switch(item){case 1:break;}}"
          "int value=condition?first:second;finish:goto finish;}\n"_str,
          "struct S : Base {\n    S() : value(1) {\n    }\n    int value;\n};\n"
          "void f() {\n    for (auto item : items) {\n        switch (item) {\n"
          "        case 1 : break;\n        }\n    }\n"
          "    int value = condition ? first : second;\nfinish:\n    goto finish;\n}\n"_str },
        { "PointerAlignment: Left\n"_str,
          "OPAQUE_MACRO( int* raw, value+value );\nstruct UPPER{UPPER(){}};\n"_str,
          "OPAQUE_MACRO( int* raw, value+value );\n"
          "struct UPPER {\n    UPPER() {\n    }\n};\n"_str },
    };
    for (const auto& test : cases) expect_style_output(test);
}

TEST(BuiltinFormat, OwnsAccessModifierEmptyLinesBeforeAndAfterLabels) {
    constexpr auto        source  = "class C{\npublic:\n\nint a;\n\nprotected:\n\nprivate:\n\n"
                                    "int b;\npublic:\n};\n"_str;
    const FormatStyleCase cases[] = {
        { "AccessModifierOffset: -4\nEmptyLineBeforeAccessModifier: Never\n"
          "EmptyLineAfterAccessModifier: Never\n"_str,
          source,
          "class C {\npublic:\n    int a;\nprotected:\nprivate:\n    int b;\npublic:\n};\n"_str },
        { "AccessModifierOffset: -4\nEmptyLineBeforeAccessModifier: Leave\n"
          "EmptyLineAfterAccessModifier: Leave\n"_str,
          source,
          "class C {\npublic:\n\n    int a;\n\nprotected:\n\nprivate:\n\n"
          "    int b;\npublic:\n};\n"_str },
        { "AccessModifierOffset: -4\nEmptyLineBeforeAccessModifier: LogicalBlock\n"
          "EmptyLineAfterAccessModifier: Never\nMaxEmptyLinesToKeep: 0\n"_str,
          source,
          "class C {\npublic:\n    int a;\n\nprotected:\nprivate:\n    int b;\n\npublic:\n};\n"_str },
        { "AccessModifierOffset: -4\nEmptyLineBeforeAccessModifier: Always\n"
          "EmptyLineAfterAccessModifier: Never\nMaxEmptyLinesToKeep: 0\n"_str,
          source,
          "class C {\npublic:\n    int a;\n\nprotected:\n\nprivate:\n    int b;\n\npublic:\n};\n"_str },
        { "AccessModifierOffset: -4\nEmptyLineBeforeAccessModifier: Never\n"
          "EmptyLineAfterAccessModifier: Always\nMaxEmptyLinesToKeep: 0\n"_str,
          source,
          "class C {\npublic:\n\n    int a;\nprotected:\nprivate:\n\n    int b;\npublic:\n};\n"_str },
        { "AccessModifierOffset: -4\nEmptyLineBeforeAccessModifier: LogicalBlock\n"
          "EmptyLineAfterAccessModifier: Always\n"_str,
          "class C{public:int a;\n// keep member comment\nprotected:int b;private:};\n"_str,
          "class C {\npublic:\n\n    int a;\n    // keep member comment\nprotected:\n\n"
          "    int b;\n\nprivate:\n};\n"_str },
        { "IndentAccessModifiers: true\nAccessModifierOffset: -100\n"
          "EmptyLineBeforeAccessModifier: Always\nEmptyLineAfterAccessModifier: Always\n"
          "MaxEmptyLinesToKeep: 0\n"_str,
          "class C{int before;public:int after;protected:};\n"_str,
          "class C {\n        int before;\n\n    public:\n\n        int after;\n\n"
          "    protected:\n};\n"_str },
    };
    for (const auto& test : cases) expect_style_output(test);
}

TEST(BuiltinFormat, SeparatesConstructorBreakPositionsFromPacking) {
    constexpr auto        short_list  = "struct S{S():first(1),second(2){}};\n"_str;
    constexpr auto        long_header = "struct S{S(int value):first(111),second(222){}};\n"_str;
    const FormatStyleCase cases[]     = {
        { "ColumnLimit: 0\nPackConstructorInitializers: Never\n"
          "BreakConstructorInitializers: BeforeColon\n"_str,
          short_list,
          "struct S {\n    S()\n        : first(1),\n          second(2) {\n    }\n};\n"_str },
        { "ColumnLimit: 0\nPackConstructorInitializers: Never\n"
          "BreakConstructorInitializers: BeforeComma\n"_str,
          short_list,
          "struct S {\n    S()\n        : first(1)\n        , second(2) {\n    }\n};\n"_str },
        { "ColumnLimit: 0\nPackConstructorInitializers: Never\n"
          "BreakConstructorInitializers: AfterColon\n"_str,
          short_list,
          "struct S {\n    S() :\n        first(1),\n        second(2) {\n    }\n};\n"_str },
        { "ColumnLimit: 0\nPackConstructorInitializers: NextLineOnly\n"
          "BreakConstructorInitializers: BeforeColon\n"_str,
          short_list,
          "struct S {\n    S()\n        : first(1), second(2) {\n    }\n};\n"_str },
        { "ColumnLimit: 100\nPackConstructorInitializers: NextLineOnly\n"
          "BreakConstructorInitializers: BeforeComma\n"_str,
          short_list,
          "struct S {\n    S()\n        : first(1), second(2) {\n    }\n};\n"_str },
        { "ColumnLimit: 28\nPackConstructorInitializers: NextLineOnly\n"
          "BreakConstructorInitializers: AfterColon\n"_str,
          "struct S{S():first(11111111),second(22222222){}};\n"_str,
          "struct S {\n    S() :\n        first(11111111),\n        second(22222222) {\n    }\n};\n"_str },
        { "ColumnLimit: 36\nPackConstructorInitializers: NextLine\n"
          "BreakConstructorInitializers: BeforeColon\n"_str,
          long_header,
          "struct S {\n    S(int value)\n        : first(111), second(222) {\n    }\n};\n"_str },
        { "ColumnLimit: 36\nPackConstructorInitializers: CurrentLine\n"
          "BreakConstructorInitializers: BeforeColon\n"_str,
          long_header,
          "struct S {\n    S(int value)\n        : first(111),\n          second(222) {\n    }\n};\n"_str },
        { "ColumnLimit: 100\nPackConstructorInitializers: BinPack\n"
          "BreakConstructorInitializers: AfterColon\n"_str,
          short_list,
          "struct S {\n    S() : first(1), second(2) {\n    }\n};\n"_str },
        { "ColumnLimit: 100\nPackConstructorInitializers: BinPack\n"
          "BreakConstructorInitializers: BeforeComma\n"_str,
          short_list,
          "struct S {\n    S()\n        : first(1)\n        , second(2) {\n    }\n};\n"_str },
    };
    for (const auto& test : cases) expect_style_output(test);
}

TEST(BuiltinFormat, SelectsInheritanceColonAndCommaBreaksWithoutRewritingBases) {
    constexpr auto        source  = "class Derived:public First,public Second{int value;};\n"_str;
    const FormatStyleCase cases[] = {
        { "ColumnLimit: 32\nBreakInheritanceList: BeforeColon\n"_str,
          "class Derived:public Base<int>,public Second{int value;};\n"_str,
          "class Derived\n    : public Base<int>,\n      public Second {\n    int value;\n};\n"_str },
        { "ColumnLimit: 32\nBreakInheritanceList: BeforeComma\n"_str,
          source,
          "class Derived\n    : public First\n    , public Second {\n    int value;\n};\n"_str },
        { "ColumnLimit: 100\nBreakInheritanceList: BeforeComma\n"_str,
          source,
          "class Derived\n    : public First\n    , public Second {\n    int value;\n};\n"_str },
        { "ColumnLimit: 32\nBreakInheritanceList: AfterColon\n"_str,
          source,
          "class Derived :\n    public First,\n    public Second {\n    int value;\n};\n"_str },
        { "ColumnLimit: 32\nBreakInheritanceList: BeforeColon\n"
          "ConstructorInitializerIndentWidth: 2\nContinuationIndentWidth: 8\n"_str,
          source,
          "class Derived\n  : public First,\n    public Second {\n    int value;\n};\n"_str },
        { "ColumnLimit: 100\nBreakInheritanceList: AfterComma\n"_str,
          source,
          "class Derived : public First,\n                public Second {\n    int value;\n};\n"_str },
        { "ColumnLimit: 0\nBreakInheritanceList: BeforeColon\n"_str,
          "// lito-format off\nclass Raw:public A,public B{};\n// lito-format on\n"
          "class Short:public Base<int>{};\n"_str,
          "// lito-format off\nclass Raw:public A,public B{};\n// lito-format on\n"
          "class Short : public Base<int> {\n};\n"_str },
        { "ColumnLimit: 40\nBreakInheritanceList: BeforeColon\n"_str,
          "class Derived:public Base<int,Value>,public Second{int value;};\n"_str,
          "class Derived\n    : public Base<int, Value>,\n      public Second {\n"
          "    int value;\n};\n"_str },
    };
    for (const auto& test : cases) expect_style_output(test);
}

TEST(BuiltinFormat, SpacesOnlyEmptyBracesAlreadySelectedForTheSameLine) {
    constexpr auto source =
        "void f(){}class C{};namespace n{};auto action=[](){};int value{};\n"_str;
    const FormatStyleCase cases[] = {
        { "SpaceInEmptyBraces: Always\nAllowShortFunctionsOnASingleLine: All\n"
          "AllowShortLambdasOnASingleLine: All\nCpp11BracedListStyle: false\n"
          "SpaceBeforeCpp11BracedList: true\nBreakBeforeBraces: Custom\nBraceWrapping:\n"
          "  AfterClass: true\n  AfterNamespace: true\n  SplitEmptyRecord: false\n"
          "  SplitEmptyNamespace: false\n"_str,
          source,
          "void f() { }\nclass C\n{ };\nnamespace n\n{ };\nauto action = []() { };\n"
          "int value { };\n"_str },
        { "SpaceInEmptyBraces: Block\nAllowShortFunctionsOnASingleLine: All\n"
          "AllowShortLambdasOnASingleLine: All\nCpp11BracedListStyle: false\n"
          "SpaceBeforeCpp11BracedList: true\nBreakBeforeBraces: Custom\nBraceWrapping:\n"
          "  AfterClass: true\n  AfterNamespace: true\n  SplitEmptyRecord: false\n"
          "  SplitEmptyNamespace: false\n"_str,
          source,
          "void f() { }\nclass C\n{ };\nnamespace n\n{ };\nauto action = []() { };\n"
          "int value {};\n"_str },
        { "SpaceInEmptyBraces: Never\nAllowShortFunctionsOnASingleLine: All\n"
          "AllowShortLambdasOnASingleLine: All\nCpp11BracedListStyle: false\n"
          "SpaceBeforeCpp11BracedList: true\nBreakBeforeBraces: Custom\nBraceWrapping:\n"
          "  AfterClass: true\n  AfterNamespace: true\n  SplitEmptyRecord: false\n"
          "  SplitEmptyNamespace: false\n"_str,
          source,
          "void f() {}\nclass C\n{};\nnamespace n\n{};\nauto action = []() {};\n"
          "int value {};\n"_str },
        { "SpaceInEmptyBraces: Always\nSpaceInEmptyBlock: false\n"
          "Cpp11BracedListStyle: true\nSpaceInEmptyParentheses: false\n"
          "AllowShortFunctionsOnASingleLine: All\n"_str,
          "void f(){}int value{};\n"_str,
          "void f() { }\nint value{};\n"_str },
        { "SpaceInEmptyBraces: Never\nCpp11BracedListStyle: true\n"
          "SpaceInEmptyParentheses: true\n"_str,
          "int value{};\n"_str,
          "int value{ };\n"_str },
        { "SpaceInEmptyBraces: Never\nSpaceInEmptyBlock: true\n"
          "AllowShortFunctionsOnASingleLine: All\nCpp11BracedListStyle: false\n"_str,
          "void f(){}int value{};\n"_str,
          "void f() { }\nint value{};\n"_str },
        { "SpaceInEmptyBlock: true\nSpaceInEmptyBraces: Never\n"
          "AllowShortFunctionsOnASingleLine: All\nCpp11BracedListStyle: false\n"_str,
          "void f(){}int value{};\n"_str,
          "void f() { }\nint value{};\n"_str },
        { "SpaceInEmptyBraces: Always\n"_str,
          "void f(){}class C{};namespace n{};auto action=[](){};\n"_str,
          "void f() {\n}\nclass C {\n};\nnamespace n {\n};\nauto action = []() {\n};\n"_str },
    };
    for (const auto& test : cases) expect_style_output(test);
}

TEST(BuiltinFormat, KeepsShortEnumsBoundedByCommentsProtectionAndColumns) {
    constexpr auto        enumerators = "enum E{A=1,B=2,};enum Empty{};\n"_str;
    const FormatStyleCase cases[]     = {
        { "AllowShortEnumsOnASingleLine: true\nBreakBeforeBraces: Custom\n"
          "BraceWrapping:\n  AfterEnum: true\n"_str,
          enumerators,
          "enum E { A = 1, B = 2, };\nenum Empty {};\n"_str },
        { "AllowShortEnumsOnASingleLine: false\nBreakBeforeBraces: Custom\n"
          "BraceWrapping:\n  AfterEnum: true\n"_str,
          enumerators,
          "enum E\n{\n    A = 1,\n    B = 2,\n};\nenum Empty\n{\n};\n"_str },
        { "AllowShortEnumsOnASingleLine: true\nColumnLimit: 24\n"_str,
          "enum Long{FirstLongValue,SecondLongValue,ThirdLongValue};\n"_str,
          "enum Long {\n    FirstLongValue,\n    SecondLongValue,\n    ThirdLongValue\n};\n"_str },
        { "AllowShortEnumsOnASingleLine: true\n"_str,
          "// lito-format off\nenum Raw{A,B};\n// lito-format on\n"
          "enum E{\nA,\n// keep separator\nB,\n};\n"_str,
          "// lito-format off\nenum Raw{A,B};\n// lito-format on\n"
          "enum E {\n    A,\n    // keep separator\n    B,\n};\n"_str },
    };
    for (const auto& test : cases) expect_style_output(test);
}

TEST(BuiltinFormat, AnchorsLambdaBodiesToPlannedSignaturesOrLexicalScopes) {
    constexpr auto nested =
        "void f(){consume_long_argument_list(invoke_long_callback_name([](){run();}),"
        "third_argument);}\n"_str;
    const FormatStyleCase cases[] = {
        { "ColumnLimit: 40\nAlignAfterOpenBracket: DontAlign\nBinPackArguments: false\n"
          "BreakAfterOpenBracketFunction: true\nBreakBeforeCloseBracketFunction: true\n"
          "AllowShortLambdasOnASingleLine: None\nLambdaBodyIndentation: Signature\n"_str,
          nested,
          "void f() {\n    consume_long_argument_list(\n        invoke_long_callback_name(\n"
          "            []() {\n                run();\n            }\n        ),\n"
          "        third_argument\n    );\n}\n"_str },
        { "ColumnLimit: 40\nAlignAfterOpenBracket: DontAlign\nBinPackArguments: false\n"
          "BreakAfterOpenBracketFunction: true\nBreakBeforeCloseBracketFunction: true\n"
          "AllowShortLambdasOnASingleLine: None\nLambdaBodyIndentation: OuterScope\n"_str,
          nested,
          "void f() {\n    consume_long_argument_list(\n        invoke_long_callback_name(\n"
          "            []() {\n        run();\n    }\n        ),\n"
          "        third_argument\n    );\n}\n"_str },
        { "BreakBeforeBraces: Custom\nBraceWrapping:\n  BeforeLambdaBody: true\n"
          "  AfterControlStatement: Always\n  IndentBraces: true\n"
          "LambdaBodyIndentation: Signature\nAllowShortLambdasOnASingleLine: None\n"_str,
          "void f(){auto action=[](){if(true){run();}};}\n"_str,
          "void f() {\n    auto action = []()\n        {\n            if (true)\n                "
          "{\n"
          "                    run();\n                }\n        };\n}\n"_str },
        { "BreakBeforeBraces: Custom\nBraceWrapping:\n  BeforeLambdaBody: true\n"
          "  IndentBraces: true\nLambdaBodyIndentation: OuterScope\n"
          "AllowShortLambdasOnASingleLine: None\n"_str,
          "void f(){auto action=[](){run();};}\n"_str,
          "void f() {\n    auto action = []()\n        {\n            run();\n        };\n}\n"_str },
        { "BreakBeforeBraces: Custom\nBraceWrapping:\n  BeforeLambdaBody: true\n"
          "LambdaBodyIndentation: OuterScope\nAllowShortLambdasOnASingleLine: All\n"_str,
          "void f(){auto action=[](){return 1;};}\n"_str,
          "void f() {\n    auto action = []() { return 1; };\n}\n"_str },
    };
    for (const auto& test : cases) expect_style_output(test);
}

TEST(BuiltinFormat, KeepsCaseLambdaAndDoWhileBraceFlagsInTheirOwnRoles) {
    constexpr auto source = "void f(){switch(kind){case 1:{run();}default:stop();}"
                            "do{run();}while(ready);while(ready){stop();}"
                            "auto action=[](){run();};label:{run();}}\n"_str;
    constexpr auto attached =
        "void f() {\n    switch (kind) {\n    case 1: {\n        run();\n    }\n"
        "    default:\n        stop();\n    }\n    do {\n        run();\n    } while (ready);\n"
        "    while (ready) {\n        stop();\n    }\n    auto action = []() {\n        run();\n"
        "    };\nlabel:\n    {\n        run();\n    }\n}\n"_str;
    const FormatStyleCase cases[] = {
        { "BreakBeforeBraces: Custom\nBraceWrapping:\n  AfterCaseLabel: true\n"
          "  BeforeLambdaBody: true\n  BeforeWhile: true\n"_str,
          source,
          "void f() {\n    switch (kind) {\n    case 1:\n    {\n        run();\n    }\n"
          "    default:\n        stop();\n    }\n    do {\n        run();\n    }\n"
          "    while (ready);\n    while (ready) {\n        stop();\n    }\n"
          "    auto action = []()\n    {\n        run();\n    };\nlabel:\n    {\n"
          "        run();\n    }\n}\n"_str },
        { "BreakBeforeBraces: Custom\nBraceWrapping:\n  AfterCaseLabel: false\n"
          "  BeforeLambdaBody: false\n  BeforeWhile: false\n"_str,
          source,
          attached },
        { "BreakBeforeBraces: Attach\nBraceWrapping:\n  AfterCaseLabel: true\n"
          "  BeforeLambdaBody: true\n  BeforeWhile: true\n"_str,
          source,
          attached },
        { "BreakBeforeBraces: Allman\nBraceWrapping:\n  AfterCaseLabel: false\n"
          "  BeforeLambdaBody: false\n  BeforeWhile: true\n"_str,
          "void f(){switch(kind){case 1:{run();}}do{run();}while(ready);"
          "auto action=[](){run();};}\n"_str,
          "void f()\n{\n    switch (kind)\n    {\n    case 1:\n    {\n        run();\n    }\n"
          "    }\n    do\n    {\n        run();\n    } while (ready);\n"
          "    auto action = []()\n    {\n        run();\n    };\n}\n"_str },
        { "IndentCaseLabels: true\nBreakBeforeBraces: Custom\nBraceWrapping:\n"
          "  AfterCaseLabel: true\n"_str,
          "void f(){switch(kind){case 1:{run();}default:{stop();}}}\n"_str,
          "void f() {\n    switch (kind) {\n        case 1:\n        {\n            run();\n"
          "        }\n        default:\n        {\n            stop();\n        }\n    }\n}\n"_str },
    };
    for (const auto& test : cases) expect_style_output(test);
}
