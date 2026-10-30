#include <rstd/test/gtest.hpp>

import rstd;
import rstd.json;
import rstd.test;
import lito.driver;
import lito.test.support;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito_test;
using PathBuf = rstd::path::PathBuf;

class CompilationDatabaseCommand : public ProjectFixture {};

TEST_F(CompilationDatabaseCommand, RecordsMaterializedArgumentsAndPreservesWarmDatabase) {
    const ProjectFile files[] = {
        { "lito.toml"_str, R"toml([package]
name = "commands"
version = "0.1.0"
[[bin]]
name = "commands"
link-stdlib = false
sources = ["value.cppm", "main.cpp", "plain.cpp"]
)toml"_str },
        { "value.cppm"_str, "export module value; export int answer() { return 42; }\n"_str },
        { "main.cpp"_str, "import value; int main() { return answer() == 42 ? 0 : 1; }\n"_str },
        { "plain.cpp"_str, "int plain() { return 0; }\n"_str },
    };
    auto project = materialize("commands with spaces"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto output  = build_root("output with spaces"_str);
    auto request = build_request(project->root.as_path(), output.as_path(), Vec<String>::make());
    request.result.documentation = true;
    auto cold                    = lito::build(request);
    if (cold.is_err())
        rstd::test::fail_current(
            error_chain_text(cold.unwrap_err()).as_str(), __FILE__, __LINE__, false);
    ASSERT_TRUE(cold.is_ok());
    EXPECT_EQ(cold->compilation_database.as_path(),
              output.join(PathBuf::from("compile_commands.json"_str).as_path()).as_path());
    auto        text     = rstd::fs::read_to_string(cold->compilation_database.as_path()).unwrap();
    auto        document = rstd::json::from_str(text.as_str()).unwrap();
    const auto& entries  = *document.as_array().unwrap();
    ASSERT_EQ(entries.len(), usize(3));
    EXPECT_EQ(entries.len(), cold->scanned);
    ASSERT_EQ(cold->documentation_units.len(), entries.len());
    for (const auto& unit : cold->documentation_units) {
        auto matched = false;
        for (const auto& entry : entries) {
            if (entry["file"_str].as_str().unwrap() != unit.source.as_path().to_str().unwrap())
                continue;
            matched = true;
            EXPECT_EQ(entry.as_object().unwrap()->len(), usize(4));
            EXPECT_EQ(entry["directory"_str].as_str().unwrap(),
                      unit.invocation.working_directory.as_path().to_str().unwrap());
            EXPECT_EQ(entry["output"_str].as_str().unwrap(),
                      unit.invocation.staged_object.as_path().to_str().unwrap());
            const auto& arguments = *entry["arguments"_str].as_array().unwrap();
            ASSERT_EQ(arguments.len(), unit.invocation.arguments.len());
            for (auto i = usize {}; i < arguments.len(); ++i)
                EXPECT_EQ(arguments[i].as_str().unwrap(), unit.invocation.arguments[i].as_str());
        }
        EXPECT_TRUE(matched);
    }
    auto modified =
        rstd::fs::metadata(cold->compilation_database.as_path()).unwrap().modified().unwrap();
    auto warm = lito::build(request);
    ASSERT_TRUE(warm.is_ok());
    EXPECT_EQ(warm->compiled, usize {});
    EXPECT_EQ(rstd::fs::read_to_string(warm->compilation_database.as_path()).unwrap(), text);
    EXPECT_EQ(rstd::fs::metadata(warm->compilation_database.as_path()).unwrap().modified().unwrap(),
              modified);
    ASSERT_TRUE(rstd::fs::remove_file(warm->compilation_database.as_path()).is_ok());
    auto restored = lito::build(request);
    ASSERT_TRUE(restored.is_ok());
    EXPECT_EQ(restored->compiled, usize {});
    EXPECT_EQ(rstd::fs::read_to_string(restored->compilation_database.as_path()).unwrap(), text);

    auto main = project->root.join(PathBuf::from("main.cpp"_str).as_path());
    ASSERT_TRUE(
        rstd::fs::write(main.as_path(), "import missing; int main() {}\n"_str.as_bytes()).is_ok());
    EXPECT_TRUE(lito::build(request).is_err());
    EXPECT_EQ(rstd::fs::read_to_string(cold->compilation_database.as_path()).unwrap(), text);
}

TEST_F(CompilationDatabaseCommand, ReplacesSelectedClosureAndSurvivesCompilerFailure) {
    const ProjectFile files[] = {
        { "lito.toml"_str, R"toml([workspace]
name = "commands-workspace"
members = ["first", "second"]
)toml"_str },
        { "first/lito.toml"_str, R"toml([package]
name = "first"
version = "0.1.0"
[[bin]]
name = "first"
link-stdlib = false
sources = ["main.cpp"]
)toml"_str },
        { "second/lito.toml"_str, R"toml([package]
name = "second"
version = "0.1.0"
[[bin]]
name = "second"
link-stdlib = false
sources = ["main.cpp"]
)toml"_str },
        { "first/main.cpp"_str, "int main() {}\n"_str },
        { "second/main.cpp"_str, "int main() {}\n"_str },
    };
    auto project = materialize("workspace commands"_str, files);
    ASSERT_TRUE(project.is_ok());
    auto output  = build_root("workspace commands"_str);
    auto request = build_request(project->root.as_path(), output.as_path(), Vec<String>::make());
    auto all     = lito::build(request);
    if (all.is_err())
        rstd::test::fail_current(
            error_chain_text(all.unwrap_err()).as_str(), __FILE__, __LINE__, false);
    ASSERT_TRUE(all.is_ok());
    auto read = [&]() {
        return rstd::json::from_str(
                   rstd::fs::read_to_string(all->compilation_database.as_path()).unwrap().as_str())
            .unwrap();
    };
    auto document = read();
    ASSERT_EQ(document.as_array().unwrap()->len(), usize(2));
    for (const auto& entry : *document.as_array().unwrap()) {
        auto source = PathBuf::from(entry["file"_str].as_str().unwrap());
        EXPECT_EQ(entry["directory"_str].as_str().unwrap(),
                  source.as_path().parent().unwrap().to_str().unwrap());
    }
    request.selection.packages = strings("first"_str);
    auto selected              = lito::build(request);
    ASSERT_TRUE(selected.is_ok());
    EXPECT_EQ(selected->compiled, usize {});
    document = read();
    ASSERT_EQ(document.as_array().unwrap()->len(), usize(1));
    EXPECT_TRUE((*document.as_array().unwrap())[usize {}]["file"_str].as_str().unwrap().ends_with(
        "first/main.cpp"_str));

    auto source = project->root.join(PathBuf::from("second/main.cpp"_str).as_path());
    ASSERT_TRUE(
        rstd::fs::write(source.as_path(), "int main() { return undefined_name; }\n"_str.as_bytes())
            .is_ok());
    request.selection.packages = strings("second"_str);
    EXPECT_TRUE(lito::build(request).is_err());
    document = read();
    ASSERT_EQ(document.as_array().unwrap()->len(), usize(1));
    EXPECT_EQ((*document.as_array().unwrap())[usize {}]["file"_str].as_str().unwrap(),
              source.as_path().to_str().unwrap());
}
