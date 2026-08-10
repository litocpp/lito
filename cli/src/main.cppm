export module lito.executable;

import rstd;
import lito;
import :cli;
import lito.reporting;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace
{

struct EventContext {
    bool verbose { false };
};

auto event_name(lito::BuildEventKind kind) -> ref<str> {
    switch (kind) {
    case lito::BuildEventKind::Scan: return "scan"_str;
    case lito::BuildEventKind::ScanReuse: return "scan-reuse"_str;
    case lito::BuildEventKind::Compile: return "compile"_str;
    case lito::BuildEventKind::Reuse: return "reuse"_str;
    case lito::BuildEventKind::Archive: return "archive"_str;
    case lito::BuildEventKind::Link: return "link"_str;
    }
    return "unknown"_str;
}

void observe(void* raw_context, const lito::BuildEvent& event) noexcept {
    auto& context = *static_cast<EventContext*>(raw_context);
    if (! context.verbose && event.kind != lito::BuildEventKind::Scan &&
        event.kind != lito::BuildEventKind::Compile &&
        event.kind != lito::BuildEventKind::Archive && event.kind != lito::BuildEventKind::Link) {
        return;
    }
    rstd::io::println("[{}] {} {}", event_name(event.kind), event.target, event.path);
}

void observe_test(void* raw_context, const lito::TestEvent& event) noexcept {
    auto& context = *static_cast<EventContext*>(raw_context);
    if (! context.verbose) return;
    rstd::io::println(
        "[run] {} {} (cwd {})", event.package, event.executable, event.working_directory);
    for (const auto& argument : event.arguments) {
        rstd::io::println("  [arg] {}", argument.as_str());
    }
}

auto build_configuration(lito::ToolchainSpec toolchain) -> lito::BuildConfiguration {
    return lito::BuildConfiguration {
        .toolchain         = rstd::move(toolchain),
        .standard_library  = lito::StandardLibrary::Libcxx,
        .bmi_mode          = lito::BmiMode::Reduced,
        .language_standard = lito::String::make("c++20"_str),
    };
}

struct ArtifactCounts {
    usize archives {};
    usize executables {};
    usize tests {};
    usize compile_tests {};
};

auto artifact_counts(const lito::BuildSummary& summary) -> ArtifactCounts {
    auto counts = ArtifactCounts {};
    for (const auto& artifact : summary.artifacts) {
        switch (artifact.kind) {
        case lito::ArtifactKind::StaticLibrary: ++counts.archives; break;
        case lito::ArtifactKind::TestAttachmentArchive: ++counts.archives; break;
        case lito::ArtifactKind::Executable: ++counts.executables; break;
        case lito::ArtifactKind::TestExecutable: ++counts.tests; break;
        case lito::ArtifactKind::CompileTest: ++counts.compile_tests; break;
        }
    }
    return counts;
}

auto make_timing_output(ref<rstd::path::Path>       root,
                        Option<rstd::path::PathBuf> file,
                        bool standard_output) -> lito::timing_output::OutputOptions {
    if (file.is_none()) {
        return lito::timing_output::OutputOptions {
            .standard_output = standard_output,
        };
    }
    auto path = rstd::move(file).unwrap();
    if (path.as_path().is_relative()) {
        path = rstd::path::PathBuf::from(root).join(path.as_path());
    }
    return lito::timing_output::OutputOptions {
        .standard_output = standard_output,
        .file            = Some(rstd::move(path)),
    };
}

} // namespace

extern "C++" int main() {
    auto parsed = lito::cli::parse();
    if (parsed.is_Exit()) {
        auto result = rstd::move(parsed).as_Exit();
        if (result.standard_error)
            rstd::io::eprint("{}", result.output.as_str());
        else
            rstd::io::print("{}", result.output.as_str());
        return static_cast<int>(result.exit_code.to_primitive());
    }
    auto invocation    = rstd::move(parsed).as_Parsed();
    auto loaded_config = lito::load_project_config(invocation.working_directory.as_path());
    if (loaded_config.is_err()) {
        auto error = rstd::move(loaded_config).unwrap_err();
        rstd::io::eprintln("lito: {}", error.message.as_str());
        return 1;
    }
    auto project = rstd::move(loaded_config).unwrap();

    if (invocation.command.is_Update()) {
        auto request = lito::UpdateRequest {
            .root        = rstd::move(project.root),
            .environment = rstd::move(project.environment),
            .sources     = rstd::move(project.sources),
        };
        auto result = lito::update_dependencies(request);
        if (result.is_err()) {
            auto error = rstd::move(result).unwrap_err();
            rstd::io::eprintln("lito: {}", error.message.as_str());
            return 1;
        }
        if (*result == lito::LockStatus::Updated)
            rstd::io::println(
                "updated {}",
                request.root.join(lito::PathBuf::from("lito.lock"_str).as_path()).as_path());
        else
            rstd::io::println("dependencies are up to date");
        return 0;
    }

    if (invocation.command.is_Format()) {
        auto options               = rstd::move(invocation.command).as_Format().options;
        auto request               = lito::FormatRequest {};
        request.selection.root     = rstd::move(project.root);
        request.environment        = rstd::move(project.environment);
        request.toolchain          = rstd::move(project.toolchain);
        request.sources            = rstd::move(project.sources);
        request.selection.packages = rstd::move(options.packages);

        auto result = lito::format(request);
        if (result.is_err()) {
            auto error = rstd::move(result).unwrap_err();
            rstd::io::eprintln("lito: {}", error.message.as_str());
            return 1;
        }
        auto summary = rstd::move(result).unwrap();
        rstd::io::println("formatted {} packages, {} files", summary.packages, summary.files);
        return 0;
    }

    if (invocation.command.is_Scan()) {
        auto options               = rstd::move(invocation.command).as_Scan().options;
        auto request               = lito::ScanRequest {};
        request.selection.root     = rstd::move(project.root);
        request.environment        = rstd::move(project.environment);
        request.configuration      = build_configuration(rstd::move(project.toolchain));
        request.sources            = rstd::move(project.sources);
        request.pkg_config         = rstd::move(project.pkg_config);
        request.cmake              = rstd::move(project.cmake);
        request.selection.packages = rstd::move(options.packages);
        request.targets            = rstd::move(options.targets);
        request.source             = rstd::move(options.source);
        request.locked             = options.locked;
        if (options.profile.is_some()) request.configuration.profile = *options.profile;

        auto scanned = lito::scan(request);
        if (scanned.is_err()) {
            auto error = rstd::move(scanned).unwrap_err();
            rstd::io::eprintln("lito: {}", error.message.as_str());
            return 1;
        }
        auto json = lito::scan_report_json(*scanned, options.format);
        if (json.is_err()) {
            auto error = rstd::move(json).unwrap_err();
            rstd::io::eprintln("lito: {}", error.message.as_str());
            return 1;
        }
        rstd::io::println("{}", json->as_str());
        return 0;
    }

    if (invocation.command.is_Test()) {
        auto options                 = rstd::move(invocation.command).as_Test().options;
        auto timing                  = make_timing_output(project.root.as_path(),
                                                          rstd::move(options.timing_file),
                                                          options.verbose && ! options.no_timing);
        auto request                 = lito::TestRequest {};
        request.build.selection.root = rstd::move(project.root);
        request.build.environment    = rstd::move(project.environment);
        request.build.configuration  = build_configuration(rstd::move(project.toolchain));
        request.build.sources        = rstd::move(project.sources);
        request.build.pkg_config     = rstd::move(project.pkg_config);
        request.build.cmake          = rstd::move(project.cmake);
        request.build.selection.packages = rstd::move(options.packages);
        request.build.locked             = options.locked;
        request.arguments                = rstd::move(options.arguments);
        request.no_run                   = options.no_run;
        if (options.profile.is_some()) request.build.configuration.profile = *options.profile;
        request.build.execution.scan.jobs    = options.jobs;
        request.build.execution.compile.jobs = options.jobs;
        if (options.output.is_some()) request.build.output = rstd::move(*options.output);
        auto event_context     = EventContext { .verbose = options.verbose };
        request.build.observer = Some(lito::BuildObserver {
            .context = rstd::addressof(event_context),
            .notify  = observe,
        });
        request.observer       = Some(lito::TestObserver {
            .context = rstd::addressof(event_context),
            .notify  = observe_test,
        });

        auto result = lito::test(rstd::move(request));
        if (result.is_err()) {
            auto error = rstd::move(result).unwrap_err();
            rstd::io::eprintln("lito: {}", error.message.as_str());
            return 1;
        }
        auto summary = rstd::move(result).unwrap();
        auto counts  = artifact_counts(summary.build);
        auto emitted = lito::timing_output::emit(summary.build, timing);
        if (emitted.is_err()) {
            rstd::io::eprintln("lito: {}", rstd::move(emitted).unwrap_err().as_str());
            return 1;
        }
        if (options.no_run) {
            rstd::io::println("built {} tests ({}) in {}: {} scanned, {} compiled, {} reused",
                              counts.tests + summary.build.compile_tests.len(),
                              summary.build.profile.as_str(),
                              summary.build.output.as_path(),
                              summary.build.scanned,
                              summary.build.compiled,
                              summary.build.reused);
            return 0;
        }

        rstd::io::println("test build: {} scanned, {} compiled, {} reused",
                          summary.build.scanned,
                          summary.build.compiled,
                          summary.build.reused);

        auto passed = usize {};
        auto failed = usize {};
        for (const auto& execution : summary.build.compile_tests) {
            if (execution.success()) {
                ++passed;
                rstd::io::println("[pass] {}::{} ({} ms)",
                                  execution.package.as_str(),
                                  execution.name.as_str(),
                                  execution.elapsed.as_millis());
                continue;
            }
            ++failed;
            rstd::io::eprintln("[fail] {}::{}: {}",
                               execution.package.as_str(),
                               execution.name.as_str(),
                               execution.mismatch->as_str());
            if (! execution.standard_error.is_empty()) {
                rstd::io::eprintln("{}", execution.standard_error.as_str());
            }
        }
        for (const auto& execution : summary.executions) {
            if (execution.success()) {
                ++passed;
                rstd::io::println(
                    "[pass] {} ({} ms)", execution.package.as_str(), execution.elapsed.as_millis());
                continue;
            }
            ++failed;
            if (execution.error.is_some()) {
                rstd::io::eprintln("[fail] {} in {}: {}",
                                   execution.package.as_str(),
                                   execution.working_directory.as_path(),
                                   execution.error->as_str());
            } else if (execution.status->code().is_some()) {
                rstd::io::eprintln("[fail] {} in {}: exit code {}",
                                   execution.package.as_str(),
                                   execution.working_directory.as_path(),
                                   *execution.status->code());
            } else {
                rstd::io::eprintln("[fail] {} in {}: signal {}",
                                   execution.package.as_str(),
                                   execution.working_directory.as_path(),
                                   *execution.status->signal());
            }
        }
        rstd::io::println("test result: {}. {} passed; {} failed",
                          failed == usize {} ? "ok"_str : "failed"_str,
                          passed,
                          failed);
        return failed == usize {} ? 0 : 1;
    }

    auto options               = rstd::move(invocation.command).as_Build().options;
    auto timing                = make_timing_output(project.root.as_path(),
                                                    rstd::move(options.timing_file),
                                                    options.verbose && ! options.no_timing);
    auto request               = lito::BuildRequest {};
    request.selection.root     = rstd::move(project.root);
    request.environment        = rstd::move(project.environment);
    request.configuration      = build_configuration(rstd::move(project.toolchain));
    request.sources            = rstd::move(project.sources);
    request.pkg_config         = rstd::move(project.pkg_config);
    request.cmake              = rstd::move(project.cmake);
    request.selection.packages = rstd::move(options.packages);
    request.targets            = rstd::move(options.targets);
    request.locked             = options.locked;
    if (options.profile.is_some()) request.configuration.profile = *options.profile;
    request.execution.scan.jobs    = options.jobs;
    request.execution.compile.jobs = options.jobs;
    if (options.output.is_some()) request.output = rstd::move(*options.output);
    auto event_context = EventContext { .verbose = options.verbose };

    request.observer = Some(lito::BuildObserver {
        .context = rstd::addressof(event_context),
        .notify  = observe,
    });
    auto result      = lito::build(request);
    if (result.is_err()) {
        auto error = rstd::move(result).unwrap_err();
        rstd::io::eprintln("lito: {}", error.message.as_str());
        return 1;
    }

    auto summary = rstd::move(result).unwrap();
    auto counts  = artifact_counts(summary);
    rstd::io::println("built {} ({}) in {}: {} scanned, {} compiled, {} reused, "
                      "{} archives, {} executables, {} tests",
                      summary.package.as_str(),
                      summary.profile.as_str(),
                      summary.output.as_path(),
                      summary.scanned,
                      summary.compiled,
                      summary.reused,
                      counts.archives,
                      counts.executables,
                      counts.tests + summary.compile_tests.len());
    auto emitted = lito::timing_output::emit(summary, timing);
    if (emitted.is_err()) {
        rstd::io::eprintln("lito: {}", rstd::move(emitted).unwrap_err().as_str());
        return 1;
    }
    return 0;
}
