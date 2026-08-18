module;

#include <rstd/macro.hpp>

export module lito.test.support.source;

import rstd;
import lito.driver;
import lito.core;
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

auto external_git_graph(ref<str>                   url,
                        ref<rstd::path::Path>      root_directory,
                        lito::source::GitReference reference)
    -> lito::package::ResolvedPackageGraph {
    auto declarations = Vec<lito::dependency::CMakeDependencyRequirement>::make();
    declarations.push(lito::dependency::CMakeDependencyRequirement {
        .alias   = String::make("fixture"_str),
        .package = String::make("Fixture"_str),
        .source  = Some(String::make("fixture"_str)),
    });
    auto external_sources = Vec<lito::manifest::PackageExternalSourceDeclaration>::make();
    external_sources.push(lito::manifest::PackageExternalSourceDeclaration {
        .name   = String::make("fixture"_str),
        .source = lito::dependency::ExternalSourceRequirement::Git(String::make(url),
                                                                   rstd::move(reference)),
    });
    auto packages = Vec<lito::package::ResolvedPackage>::make();
    packages.push(lito::package::ResolvedPackage {
        .manifest =
            lito::manifest::PackageManifest {
                .name                        = String::make("fixture-root"_str),
                .external_sources            = rstd::move(external_sources),
                .cmake_external_dependencies = rstd::move(declarations),
            },
    });
    return lito::package::ResolvedPackageGraph {
        .root_directory = PathBuf::from(root_directory),
        .packages       = rstd::move(packages),
    };
}

auto resolved_git_commit(const lito::package::ResolvedPackageGraph& graph) -> Option<ref<str>> {
    for (const auto& package : graph.packages) {
        for (const auto& external : package.externals) {
            if (external.source.is_Git()) return Some(external.source.as_Git().commit.as_str());
        }
    }
    for (const auto& source : graph.sources) {
        if (source.kind == lito::source::PackageSourceKind::Git)
            return Some(source.commit.as_str());
    }
    return None();
}

struct FetchEventCapture {
    ref<str> expected_url;
    usize    count {};
    bool     source_matches {};
    bool     destination_matches {};
};

void record_fetch(FetchEventCapture&    context,
                  ref<str>              source,
                  ref<rstd::path::Path> destination) noexcept {
    ++context.count;
    context.source_matches =
        source.starts_with(context.expected_url) && source.ends_with("#HEAD"_str);
    auto parent = destination.parent();
    if (parent.is_none()) return;
    auto directory = parent->file_name();
    if (directory.is_none()) return;
    auto text                   = directory->to_str();
    context.destination_matches = text.is_some() && *text == "db"_str;
}

void capture_fetch(void* raw_context, const lito::BuildEvent& event) noexcept {
    if (event.kind != lito::BuildEventKind::Fetch) return;
    auto& context = *static_cast<FetchEventCapture*>(raw_context);
    record_fetch(context, event.target, event.path);
}

void capture_source_fetch(void* raw_context, const lito::source::SourceEvent& event) noexcept {
    if (event.kind != lito::source::SourceEventKind::Fetch) return;
    auto& context = *static_cast<FetchEventCapture*>(raw_context);
    record_fetch(context, event.source, event.destination);
}
class EnvironmentVariableGuard {
    String                      key_;
    Option<rstd::ffi::OsString> previous_;

public:
    explicit EnvironmentVariableGuard(ref<str> key)
        : key_(String::make(key)), previous_(rstd::env::var_os(key)) {
        rstd::env::remove_var(key);
    }

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

struct FileSourceEventCapture {
    usize fetch {};
    usize extract {};
};

void capture_file_source_event(void* raw_context, const lito::source::SourceEvent& event) noexcept {
    auto& context = *static_cast<FileSourceEventCapture*>(raw_context);
    if (event.kind == lito::source::SourceEventKind::Fetch) {
        ++context.fetch;
    } else if (event.kind == lito::source::SourceEventKind::Extract) {
        ++context.extract;
    }
}

} // namespace lito_test
