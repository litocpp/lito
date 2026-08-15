module;

#include <rstd/macro.hpp>

export module lito.test.support.build;

import rstd;
import lito.driver;
import lito.core;
import lito.cpp;
import lito.system;
import lito.toolchain.cmake;
import lito.toolchain;
import lito.test.base_support;

using namespace rstd::prelude;
using namespace lito::system;
using namespace rstd::literals;
using PathBuf = rstd::path::PathBuf;

export namespace lito_test
{
auto executable(const lito::BuildSummary& summary) -> Option<ref<rstd::path::Path>> {
    for (const auto& artifact : summary.artifacts) {
        if (artifact.kind == lito::cpp::ArtifactKind::Executable) {
            return Some(artifact.path.as_path());
        }
    }
    return None();
}

auto project_root_role(const lito::ResolvedPackageGraph& graph, ref<str> name)
    -> Option<lito::ProjectRootRole> {
    for (const auto& root : graph.roots) {
        if (root.name.as_str() == name) {
            auto role = root.role;
            return Some(role);
        }
    }
    return None();
}

auto contains_name(const Vec<String>& names, ref<str> name) -> bool {
    for (const auto& candidate : names) {
        if (candidate.as_str() == name) return true;
    }
    return false;
}

auto write_executable(ref<rstd::path::Path> path) -> bool {
    if (rstd::fs::write(path, "fixture\n"_str.as_bytes()).is_err()) return false;
#if RSTD_OS_UNIX
    return rstd::fs::set_permissions(path, rstd::fs::Permissions::from_mode(u32(0755))).is_ok();
#else
    return true;
#endif
}

auto regular_file_count(ref<rstd::path::Path> directory) -> Option<usize> {
    auto opened = rstd::fs::read_dir(directory);
    if (opened.is_err()) return None();
    auto count   = usize {};
    auto entries = rstd::move(opened).unwrap();
    for (auto next = entries.next(); next.is_some(); next = entries.next()) {
        auto item = rstd::move(next).unwrap();
        if (item.is_err()) return None();
        auto entry = rstd::move(item).unwrap();
        auto type  = entry.file_type();
        if (type.is_err()) return None();
        if (type->is_file()) {
            ++count;
        } else if (type->is_dir()) {
            auto nested = regular_file_count(entry.path().as_path());
            if (nested.is_none()) return None();
            count += *nested;
        }
    }
    return Some(count);
}

struct CompileProgressCapture {
    Vec<lito::BuildProgress> values;
    Vec<String>              toolchain_names;
    Vec<PathBuf>             toolchain_paths;
    bool                     missing {};
};

void capture_compile_progress(void* raw_context, const lito::BuildEvent& event) noexcept {
    auto& capture = *static_cast<CompileProgressCapture*>(raw_context);
    if (event.kind == lito::BuildEventKind::Toolchain) {
        capture.toolchain_names.push(String::make(event.target));
        capture.toolchain_paths.push(PathBuf::from(event.path));
        return;
    }
    if (event.kind != lito::BuildEventKind::Compile) return;
    if (event.progress.is_none()) {
        capture.missing = true;
        return;
    }
    capture.values.push(lito::BuildProgress {
        .current = event.progress->current,
        .total   = event.progress->total,
    });
}

} // namespace lito_test
