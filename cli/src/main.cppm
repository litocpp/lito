export module tenon.executable;

import rstd;
import tenon;
import :cli;
import tenon.reporting;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace
{

struct EventContext {
    bool verbose { false };
};

auto event_name(tenon::BuildEventKind kind) -> ref<str> {
    switch (kind) {
    case tenon::BuildEventKind::Scan: return "scan"_str;
    case tenon::BuildEventKind::ScanReuse: return "scan-reuse"_str;
    case tenon::BuildEventKind::Compile: return "compile"_str;
    case tenon::BuildEventKind::Reuse: return "reuse"_str;
    case tenon::BuildEventKind::Archive: return "archive"_str;
    case tenon::BuildEventKind::Link: return "link"_str;
    }
    return "unknown"_str;
}

void observe(void* raw_context, const tenon::BuildEvent& event) noexcept {
    auto& context = *static_cast<EventContext*>(raw_context);
    if (! context.verbose && event.kind != tenon::BuildEventKind::Compile &&
        event.kind != tenon::BuildEventKind::Archive && event.kind != tenon::BuildEventKind::Link) {
        return;
    }
    rstd::io::println("[{}] {} {}", event_name(event.kind), event.target, event.path);
}

void observe_test(void* raw_context, const tenon::TestEvent& event) noexcept {
    auto& context = *static_cast<EventContext*>(raw_context);
    if (! context.verbose) return;
    rstd::io::println(
        "[run] {} {} (cwd {})", event.package, event.executable, event.working_directory);
    for (const auto& argument : event.arguments) {
        rstd::io::println("  [arg] {}", argument.as_str());
    }
}

auto build_configuration(tenon::ToolchainSpec toolchain) -> tenon::BuildConfiguration {
    return tenon::BuildConfiguration {
        .toolchain         = rstd::move(toolchain),
        .standard_library  = tenon::StandardLibrary::Libcxx,
        .bmi_mode          = tenon::BmiMode::Reduced,
        .language_standard = tenon::String::make("c++20"_str),
    };
}

struct ArtifactCounts {
    usize archives {};
    usize executables {};
    usize tests {};
    usize compile_tests {};
};

auto artifact_counts(const tenon::BuildSummary& summary) -> ArtifactCounts {
    auto counts = ArtifactCounts {};
    for (const auto& artifact : summary.artifacts) {
        switch (artifact.kind) {
        case tenon::ArtifactKind::StaticLibrary: ++counts.archives; break;
        case tenon::ArtifactKind::TestAttachmentArchive: ++counts.archives; break;
        case tenon::ArtifactKind::Executable: ++counts.executables; break;
        case tenon::ArtifactKind::TestExecutable: ++counts.tests; break;
        case tenon::ArtifactKind::CompileTest: ++counts.compile_tests; break;
        }
    }
    return counts;
}

auto make_timing_output(ref<rstd::path::Path>       root,
                        Option<rstd::path::PathBuf> file,
                        bool standard_output) -> tenon::timing_output::OutputOptions {
    if (file.is_none()) {
        return tenon::timing_output::OutputOptions {
            .standard_output = standard_output,
        };
    }
    auto path = rstd::move(file).unwrap();
    if (path.as_path().is_relative()) {
        path = rstd::path::PathBuf::from(root).join(path.as_path());
    }
    return tenon::timing_output::OutputOptions {
        .standard_output = standard_output,
        .file            = Some(rstd::move(path)),
    };
}

auto report_documentation(const tenon::DocSummary& summary) -> int {
    for (const auto& package : summary.packages) {
        rstd::io::println("documented {}: {} symbols, {} documented, {} undocumented, "
                          "{} unsupported, {} diagnostics",
                          package.name.as_str(),
                          package.symbols,
                          package.documented,
                          package.undocumented,
                          package.unsupported,
                          package.diagnostics);
        for (const auto& diagnostic : package.diagnostic_details) {
            rstd::io::eprintln("{}[{}] {}:{}: {}",
                               diagnostic.severity == tenon::DocDiagnosticSeverity::Error
                                   ? "error"_str
                                   : "warning"_str,
                               diagnostic.code.as_str(),
                               diagnostic.path.as_path(),
                               diagnostic.line,
                               diagnostic.message.as_str());
        }
    }
    rstd::io::println("documentation data at {}", summary.data_manifest.as_path());
    if (summary.site_generated) {
        rstd::io::println("generated documentation ({}) at {}",
                          summary.profile.as_str(),
                          summary.index.as_path());
    }
    return 0;
}

} // namespace

extern "C++" int main() {
    auto parsed = tenon::cli::parse();
    if (parsed.is_Exit()) {
        auto result = rstd::move(parsed).as_Exit();
        if (result.standard_error)
            rstd::io::eprint("{}", result.output.as_str());
        else
            rstd::io::print("{}", result.output.as_str());
        return static_cast<int>(result.exit_code.to_primitive());
    }
    auto invocation = rstd::move(parsed).as_Parsed();
    if (invocation.command.is_Doc()) {
        auto& options = invocation.command.as_Doc().options;
        if (options.from_data.is_some()) {
            auto request = tenon::DocRenderRequest {
                .working_directory = invocation.working_directory.clone(),
                .data              = options.from_data->clone(),
                .output =
                    options.output.is_some() ? options.output->clone() : rstd::path::PathBuf {},
                .frontend = options.frontend.is_some() ? Some(options.frontend->clone()) : None(),
            };
            auto rendered = tenon::render_documentation(request);
            if (rendered.is_err()) {
                auto error = rstd::move(rendered).unwrap_err();
                rstd::io::eprintln("tenon: {}", error.message.as_str());
                return 1;
            }
            return report_documentation(*rendered);
        }
    }
    auto loaded_config = tenon::load_project_config(invocation.working_directory.as_path());
    if (loaded_config.is_err()) {
        auto error = rstd::move(loaded_config).unwrap_err();
        rstd::io::eprintln("tenon: {}", error.message.as_str());
        return 1;
    }
    auto project = rstd::move(loaded_config).unwrap();

    if (invocation.command.is_Format()) {
        auto options               = rstd::move(invocation.command).as_Format().options;
        auto request               = tenon::FormatRequest {};
        request.selection.root     = rstd::move(project.root);
        request.toolchain          = rstd::move(project.toolchain);
        request.sources            = rstd::move(project.sources);
        request.selection.packages = rstd::move(options.packages);

        auto result = tenon::format(request);
        if (result.is_err()) {
            auto error = rstd::move(result).unwrap_err();
            rstd::io::eprintln("tenon: {}", error.message.as_str());
            return 1;
        }
        auto summary = rstd::move(result).unwrap();
        rstd::io::println("formatted {} packages, {} files", summary.packages, summary.files);
        return 0;
    }

    if (invocation.command.is_Scan()) {
        auto options               = rstd::move(invocation.command).as_Scan().options;
        auto request               = tenon::ScanRequest {};
        request.selection.root     = rstd::move(project.root);
        request.configuration      = build_configuration(rstd::move(project.toolchain));
        request.sources            = rstd::move(project.sources);
        request.pkg_config         = rstd::move(project.pkg_config);
        request.cmake              = rstd::move(project.cmake);
        request.selection.packages = rstd::move(options.packages);
        request.targets            = rstd::move(options.targets);
        request.source             = rstd::move(options.source);
        request.locked             = options.locked;
        if (options.profile.is_some()) request.configuration.profile = *options.profile;
        request.configuration.exceptions = options.exceptions;

        auto scanned = tenon::scan(request);
        if (scanned.is_err()) {
            auto error = rstd::move(scanned).unwrap_err();
            rstd::io::eprintln("tenon: {}", error.message.as_str());
            return 1;
        }
        auto json = tenon::scan_report_json(*scanned);
        if (json.is_err()) {
            auto error = rstd::move(json).unwrap_err();
            rstd::io::eprintln("tenon: {}", error.message.as_str());
            return 1;
        }
        rstd::io::println("{}", json->as_str());
        return 0;
    }

    if (invocation.command.is_Doc()) {
        auto options               = rstd::move(invocation.command).as_Doc().options;
        auto request               = tenon::DocRequest {};
        request.selection.root     = rstd::move(project.root);
        request.configuration      = build_configuration(rstd::move(project.toolchain));
        request.sources            = rstd::move(project.sources);
        request.pkg_config         = rstd::move(project.pkg_config);
        request.cmake              = rstd::move(project.cmake);
        request.selection.packages = rstd::move(options.packages);
        request.targets            = rstd::move(options.targets);
        request.locked             = options.locked;
        request.data_only          = options.data_only;
        if (options.profile.is_some()) request.configuration.profile = *options.profile;
        request.configuration.exceptions = options.exceptions;
        if (options.output.is_some()) request.output = rstd::move(*options.output);
        if (options.data_output.is_some()) request.data_output = rstd::move(*options.data_output);
        if (options.frontend.is_some()) request.frontend = rstd::move(options.frontend);

        auto generated = tenon::generate_documentation(request);
        if (generated.is_err()) {
            auto error = rstd::move(generated).unwrap_err();
            rstd::io::eprintln("tenon: {}", error.message.as_str());
            return 1;
        }
        return report_documentation(*generated);
    }

    if (invocation.command.is_Test()) {
        auto options                 = rstd::move(invocation.command).as_Test().options;
        auto timing                  = make_timing_output(project.root.as_path(),
                                                          rstd::move(options.timing_file),
                                                          options.verbose && ! options.no_timing);
        auto request                 = tenon::TestRequest {};
        request.build.selection.root = rstd::move(project.root);
        request.build.configuration  = build_configuration(rstd::move(project.toolchain));
        request.build.sources        = rstd::move(project.sources);
        request.build.pkg_config     = rstd::move(project.pkg_config);
        request.build.cmake          = rstd::move(project.cmake);
        request.build.selection.packages = rstd::move(options.packages);
        request.build.locked             = options.locked;
        request.arguments                = rstd::move(options.arguments);
        request.no_run                   = options.no_run;
        if (options.profile.is_some()) request.build.configuration.profile = *options.profile;
        request.build.configuration.exceptions = options.exceptions;
        if (options.output.is_some()) request.build.output = rstd::move(*options.output);
        auto event_context     = EventContext { .verbose = options.verbose };
        request.build.observer = Some(tenon::BuildObserver {
            .context = rstd::addressof(event_context),
            .notify  = observe,
        });
        request.observer       = Some(tenon::TestObserver {
            .context = rstd::addressof(event_context),
            .notify  = observe_test,
        });

        auto result = tenon::test(rstd::move(request));
        if (result.is_err()) {
            auto error = rstd::move(result).unwrap_err();
            rstd::io::eprintln("tenon: {}", error.message.as_str());
            return 1;
        }
        auto summary = rstd::move(result).unwrap();
        auto counts  = artifact_counts(summary.build);
        auto emitted = tenon::timing_output::emit(summary.build, timing);
        if (emitted.is_err()) {
            rstd::io::eprintln("tenon: {}", rstd::move(emitted).unwrap_err().as_str());
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
    auto request               = tenon::BuildRequest {};
    request.selection.root     = rstd::move(project.root);
    request.configuration      = build_configuration(rstd::move(project.toolchain));
    request.sources            = rstd::move(project.sources);
    request.pkg_config         = rstd::move(project.pkg_config);
    request.cmake              = rstd::move(project.cmake);
    request.selection.packages = rstd::move(options.packages);
    request.targets            = rstd::move(options.targets);
    request.locked             = options.locked;
    if (options.profile.is_some()) request.configuration.profile = *options.profile;
    request.configuration.exceptions = options.exceptions;
    if (options.output.is_some()) request.output = rstd::move(*options.output);
    auto event_context = EventContext { .verbose = options.verbose };

    request.observer = Some(tenon::BuildObserver {
        .context = rstd::addressof(event_context),
        .notify  = observe,
    });
    auto result      = tenon::build(request);
    if (result.is_err()) {
        auto error = rstd::move(result).unwrap_err();
        rstd::io::eprintln("tenon: {}", error.message.as_str());
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
    auto emitted = tenon::timing_output::emit(summary, timing);
    if (emitted.is_err()) {
        rstd::io::eprintln("tenon: {}", rstd::move(emitted).unwrap_err().as_str());
        return 1;
    }
    return 0;
}
