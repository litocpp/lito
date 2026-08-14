module;

#include <rstd/macro.hpp>

export module lito.test.support.source;

import rstd;
import lito;
import lito.lock;
import lito.package;
import lito.package.graph_contract;
import lito.workspace.contract;
import lito.workspace.resolver;
import lito.platform;
import lito.dependency;
import lito.dependency.cmake;
import lito.source;
import lito.manifest;
import lito.toolchain;
import lito.build.discovery;
import lito.build.layout;
import lito.system.environment;
import lito.system.process;
import lito.system.storage;
import lito.test.base_support;

using namespace rstd::prelude;
using namespace rstd::literals;
using PathBuf = rstd::path::PathBuf;

export namespace lito_test
{
auto locked_graph_is_current(ref<str> relative) -> bool {
    auto directory = fixture_path(relative);
    auto session   = lito::load_lock_session(directory.as_path(), true);
    if (session.is_err()) return false;
    auto options = session->take_resolution_options();
    auto graph   = lito::resolve_package_graph(directory.as_path(), rstd::move(options));
    if (graph.is_err()) return false;
    return lito::sync_lock(*graph, rstd::move(session).unwrap()).is_ok();
}

auto git_revision(ref<rstd::path::Path> repository, ref<str> revision) -> Option<String> {
    auto command = rstd::process::Command::make("git"_str);
    command.arg("-C"_str).arg(repository.as_os_str()).arg("rev-parse"_str).arg(revision);
    auto output = command.output();
    if (output.is_err() || ! output->status.success()) return None();
    auto text = String::from_utf8(rstd::move(output->stdout_buf));
    if (text.is_err()) return None();
    return Some(String::make(text->as_str().trim_ascii()));
}

template<typename... Arguments>
auto git_succeeds(ref<rstd::path::Path> repository, Arguments... arguments) -> bool {
    auto command = rstd::process::Command::make("git"_str);
    (command.arg(arguments), ...);
    command.current_dir(repository)
        .set_stdout(rstd::process::Stdio::null())
        .set_stderr(rstd::process::Stdio::null());
    auto status = command.status();
    return status.is_ok() && status->success();
}

auto external_git_graph(ref<str> url, lito::GitReference reference) -> lito::ResolvedPackageGraph {
    auto declarations = Vec<lito::CMakeDependencyRequirement>::make();
    declarations.push(lito::CMakeDependencyRequirement {
        .alias   = String::make("fixture"_str),
        .package = String::make("Fixture"_str),
        .source  = lito::CMakeDependencySource::Git(String::make(url), rstd::move(reference)),
    });
    auto packages = Vec<lito::ResolvedPackage>::make();
    packages.push(lito::ResolvedPackage {
        .manifest =
            lito::PackageManifest {
                .name                        = String::make("fixture-root"_str),
                .cmake_external_dependencies = rstd::move(declarations),
            },
    });
    return lito::ResolvedPackageGraph {
        .root_directory = fixture_path("project"_str),
        .packages       = rstd::move(packages),
    };
}

auto resolved_git_commit(const lito::ResolvedPackageGraph& graph) -> Option<ref<str>> {
    for (const auto& source : graph.sources) {
        if (source.kind == lito::PackageSourceKind::Git) return Some(source.commit.as_str());
    }
    return None();
}

struct FetchEventCapture {
    ref<str> expected_url;
    usize    count {};
    bool     source_matches {};
    bool     destination_matches {};
};

void capture_fetch(void* raw_context, const lito::BuildEvent& event) noexcept {
    if (event.kind != lito::BuildEventKind::Fetch) return;
    auto& context = *static_cast<FetchEventCapture*>(raw_context);
    ++context.count;
    context.source_matches =
        event.target.starts_with(context.expected_url) && event.target.ends_with("#HEAD"_str);
    auto parent = event.path.parent();
    if (parent.is_none()) return;
    auto directory = parent->file_name();
    if (directory.is_none()) return;
    auto text                   = directory->to_str();
    context.destination_matches = text.is_some() && *text == "db"_str;
}
class EnvironmentVariableGuard {
    String                      key_;
    Option<rstd::ffi::OsString> previous_;

public:
    EnvironmentVariableGuard(ref<str> key, ref<str> value)
        : key_(String::make(key)), previous_(rstd::env::var_os(key)) {
        rstd::env::set_var(key, value);
    }

    ~EnvironmentVariableGuard() {
        if (previous_.is_some()) {
            rstd::env::set_var(key_.as_str(), previous_->as_os_str());
        } else {
            rstd::env::remove_var(key_.as_str());
        }
    }
};

struct FileFetchEventCapture {
    usize count {};
};

void capture_file_fetch(void* raw_context, const lito::BuildEvent& event) noexcept {
    if (event.kind != lito::BuildEventKind::Fetch) return;
    ++static_cast<FileFetchEventCapture*>(raw_context)->count;
}

} // namespace lito_test
