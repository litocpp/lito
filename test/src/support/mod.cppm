export module tenon.test.support;

import rstd;
import tenon;

using namespace rstd::prelude;
using namespace rstd::literals;
using PathBuf = rstd::path::PathBuf;

export namespace tenon_test
{

template<typename... Values>
auto strings(Values... values) -> Vec<String> {
    auto result = Vec<String>::with_capacity(usize(sizeof...(Values)));
    (result.push(String::make(values)), ...);
    return result;
}

auto root(ref<str> relative) -> PathBuf {
    return rstd::fs::canonicalize(PathBuf::from(relative).as_path()).unwrap();
}

auto project_root() -> PathBuf { return root("project"_str); }

auto output_root(ref<str> name) -> PathBuf {
    auto directory = rstd::format("tenon-test-{}-{}", rstd::process::id(), name);
    return rstd::env::temp_dir().join(PathBuf::from(directory.as_str()).as_path());
}

auto clear_output(ref<rstd::path::Path> path) -> bool {
    auto exists = rstd::fs::exists(path);
    if (exists.is_err()) return false;
    return ! *exists || rstd::fs::remove_dir_all(path).is_ok();
}

auto configuration(tenon::BuildProfile profile = tenon::BuildProfile::Debug)
    -> tenon::BuildConfiguration {
    return tenon::BuildConfiguration {
        .profile = profile,
        .toolchain = tenon::ToolchainSpec {
            .compiler  = PathBuf::from("clang++"_str),
            .archiver  = PathBuf::from("llvm-ar"_str),
            .formatter = PathBuf::from("clang-format"_str),
        },
        .standard_library  = tenon::StandardLibrary::Libcxx,
        .bmi_mode          = tenon::BmiMode::Reduced,
        .language_standard = String::make("c++20"_str),
    };
}

auto build_request(ref<rstd::path::Path> root,
                   ref<rstd::path::Path> output,
                   Vec<String>           packages,
                   tenon::BuildProfile   profile = tenon::BuildProfile::Debug)
    -> tenon::BuildRequest {
    return tenon::BuildRequest {
        .selection = tenon::PackageSelection {
            .root     = PathBuf::from(root),
            .packages = rstd::move(packages),
        },
        .output        = PathBuf::from(output),
        .configuration = configuration(profile),
        .locked        = true,
    };
}

auto artifact_count(const tenon::BuildSummary& summary, tenon::ArtifactKind kind) -> usize {
    auto count = usize {};
    for (const auto& artifact : summary.artifacts) {
        if (artifact.kind == kind) ++count;
    }
    return count;
}

auto has_import(const tenon::ScanReport& report, ref<str> logical_name) -> bool {
    for (const auto& imported : report.result.imports) {
        if (imported.logical_name.as_str() == logical_name) return true;
    }
    return false;
}

auto copy_directory(ref<rstd::path::Path> source, ref<rstd::path::Path> destination) -> bool {
    if (rstd::fs::create_dir_all(destination).is_err()) return false;
    auto opened = rstd::fs::read_dir(source);
    if (opened.is_err()) return false;
    auto entries = rstd::move(opened).unwrap();
    for (auto next = entries.next(); next.is_some(); next = entries.next()) {
        auto entry_result = rstd::move(next).unwrap();
        if (entry_result.is_err()) return false;
        auto entry = rstd::move(entry_result).unwrap();
        auto type  = entry.file_type();
        if (type.is_err()) return false;
        auto source_path = entry.path();
        auto relative    = source_path.as_path().strip_prefix(source);
        if (relative.is_none()) return false;
        auto destination_path = PathBuf::from(destination).join(*relative);
        if (type->is_dir()) {
            if (! copy_directory(source_path.as_path(), destination_path.as_path())) return false;
            continue;
        }
        if (! type->is_file() ||
            rstd::fs::copy(source_path.as_path(), destination_path.as_path()).is_err()) {
            return false;
        }
    }
    return true;
}

} // namespace tenon_test
