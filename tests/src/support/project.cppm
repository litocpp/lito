export module lito.test.support.project;

import rstd;
import rstd.test;
import lito.core;
import lito.driver;
import lito.test.base_support;

using namespace rstd::prelude;
using namespace rstd::literals;
using PathBuf = rstd::path::PathBuf;

export namespace lito_test
{

struct ProjectFile {
    ref<str>                     path;
    ref<str>                     contents;
    lito::source::SourceFileMode mode { lito::source::SourceFileMode::Regular };
};

auto source_tree(slice<ProjectFile> files)
    -> lito::source::SourceTreeResult<lito::source::SourceTree> {
    auto tree = lito::source::SourceTree::make();
    for (const auto& file : files) {
        auto added = tree.add_text(file.path, file.contents, file.mode);
        if (added.is_err()) return Err(rstd::move(added).unwrap_err());
    }
    return Ok(rstd::move(tree));
}

template<rstd::size_t Size>
auto source_tree(const ProjectFile (&files)[Size])
    -> lito::source::SourceTreeResult<lito::source::SourceTree> {
    return source_tree(slice<ProjectFile>::from_raw_parts(files, usize(Size)));
}

class ProjectFixture : public rstd::test::Test {
    rstd::test::TempDir temporary_;

    auto directory(ref<str> category, ref<str> name) const -> PathBuf {
        return PathBuf::from(temporary_.path())
            .join(PathBuf::from(category).as_path())
            .join(PathBuf::from(name).as_path());
    }

protected:
    auto SetUp() noexcept -> void {
        auto created = rstd::test::TempDir::make();
        if (created.is_err()) {
            auto message = rstd::format("cannot create project fixture: {}", created.unwrap_err());
            rstd::test::fail_current(message.as_str(), __FILE__, __LINE__, true);
            return;
        }
        temporary_                      = rstd::move(created).unwrap();
        constexpr ref<str> categories[] = {
            "sources"_str, "build"_str, "install"_str, "cache"_str, "tools"_str,
        };
        for (auto category : categories) {
            auto path   = PathBuf::from(temporary_.path()).join(PathBuf::from(category).as_path());
            auto result = rstd::fs::create_dir(path.as_path());
            if (result.is_ok()) continue;
            auto message = rstd::format("cannot create project fixture directory '{}': {}",
                                        path.as_path(),
                                        result.unwrap_err());
            rstd::test::fail_current(message.as_str(), __FILE__, __LINE__, true);
            return;
        }
    }

    auto TearDown() noexcept -> void {}

    auto temp_root() const -> ref<rstd::path::Path> { return temporary_.path(); }
    auto source_root(ref<str> name) const -> PathBuf { return directory("sources"_str, name); }
    auto build_root(ref<str> name) const -> PathBuf { return directory("build"_str, name); }
    auto install_root(ref<str> name) const -> PathBuf { return directory("install"_str, name); }
    auto cache_root(ref<str> name) const -> PathBuf { return directory("cache"_str, name); }
    auto tool_root(ref<str> name) const -> PathBuf { return directory("tools"_str, name); }

    auto materialize(ref<str> name, const lito::source::SourceTree& tree)
        -> lito::source::SourceTreeResult<lito::source::SourceMaterialization> {
        auto root = source_root(name);
        return lito::source::materialize_source_tree(tree, root.as_path());
    }

    auto materialize(ref<str> name, slice<ProjectFile> files)
        -> lito::source::SourceTreeResult<lito::source::SourceMaterialization> {
        auto tree = source_tree(files);
        if (tree.is_err()) return Err(rstd::move(tree).unwrap_err());
        return materialize(name, *tree);
    }

    template<rstd::size_t Size>
    auto materialize(ref<str> name, const ProjectFile (&files)[Size])
        -> lito::source::SourceTreeResult<lito::source::SourceMaterialization> {
        return materialize(name, slice<ProjectFile>::from_raw_parts(files, usize(Size)));
    }

    auto project_build_request(ref<str>                         name,
                               ref<rstd::path::Path>            project,
                               Vec<String>                      packages,
                               lito::manifest::BuildProfileName profile = {}) const
        -> lito::BuildRequest {
        auto output = build_root(name);
        return lito_test::build_request(
            project, output.as_path(), rstd::move(packages), rstd::move(profile));
    }

    auto keep() -> PathBuf { return temporary_.keep(); }
};

} // namespace lito_test
