export module lito.executable;

import rstd;
import lito.driver;
import lito.system;
import :cli;

using namespace rstd::prelude;
using namespace rstd::literals;

struct EventContext {
    bool verbose { false };
    bool standard_error { false };
};

void observe(void* raw_context, const lito::BuildEvent& event) noexcept {
    auto& context = *static_cast<EventContext*>(raw_context);
    if (event.completed) return;
    if (! context.verbose && event.kind != lito::BuildEventKind::Fetch &&
        event.kind != lito::BuildEventKind::Extract && event.kind != lito::BuildEventKind::Scan &&
        event.kind != lito::BuildEventKind::Compile &&
        event.kind != lito::BuildEventKind::Configure &&
        event.kind != lito::BuildEventKind::BuildToolFetch &&
        event.kind != lito::BuildEventKind::BuildToolRun &&
        event.kind != lito::BuildEventKind::GeneratedResource &&
        event.kind != lito::BuildEventKind::CMakeConfigure &&
        event.kind != lito::BuildEventKind::CMakeBuild &&
        event.kind != lito::BuildEventKind::CMakeInstall &&
        event.kind != lito::BuildEventKind::CMakeQuery &&
        event.kind != lito::BuildEventKind::CMakeQueryBuild &&
        event.kind != lito::BuildEventKind::CMakeSnapshot &&
        event.kind != lito::BuildEventKind::Archive && event.kind != lito::BuildEventKind::Link &&
        event.kind != lito::BuildEventKind::Strip) {
        return;
    }
    if (context.standard_error) {
        if (event.progress.is_some()) {
            rstd::io::eprintln("[{} {}/{}] {} {}",
                               event.kind,
                               event.progress->current,
                               event.progress->total,
                               event.target,
                               event.path);
        } else {
            rstd::io::eprintln("[{}] {} {}", event.kind, event.target, event.path);
        }
    } else {
        if (event.progress.is_some()) {
            rstd::io::println("[{} {}/{}] {} {}",
                              event.kind,
                              event.progress->current,
                              event.progress->total,
                              event.target,
                              event.path);
        } else {
            rstd::io::println("[{}] {} {}", event.kind, event.target, event.path);
        }
    }
}

auto tool_resolution_text(const lito::BuildToolResolution& tool) -> String {
    if (tool.requested.as_path() == tool.executable.as_path()) {
        return rstd::format("{}", tool.executable.as_path());
    }
    return rstd::format("{} -> {}", tool.requested.as_path(), tool.executable.as_path());
}

auto build_option_domain_text(lito::BuildOptionReportDomain domain) -> ref<str> {
    if (domain == lito::BuildOptionReportDomain::Cpp) return "C++"_str;
    if (domain == lito::BuildOptionReportDomain::C) return "C"_str;
    return "Link"_str;
}

auto render_build_setup(const lito::BuildSetupReport& report) -> String {
    auto linker_version = report.toolchain.linker_version.as_str();
    auto newline        = linker_version.find("\n"_str);
    if (newline.is_some()) linker_version = linker_version.split_at(*newline).get<0>();
    auto result = rstd::format("Build setup\n"
                               "  Toolchain\n"
                               "    C compiler    {}\n"
                               "    C++ compiler  {}\n"
                               "    linker        {} ({}, {})\n"
                               "    archiver      {}\n",
                               tool_resolution_text(report.toolchain.cc).as_str(),
                               tool_resolution_text(report.toolchain.cxx).as_str(),
                               tool_resolution_text(report.toolchain.ld).as_str(),
                               lito::linker_family_name(report.toolchain.linker_family),
                               linker_version,
                               tool_resolution_text(report.toolchain.ar).as_str());
    if (! report.profile_values.is_empty()) {
        result.push_str(rstd::format("  Profile {}\n", report.profile.as_str()).as_str());
        auto label_width = usize {};
        for (const auto& value : report.profile_values) {
            auto domain = build_option_domain_text(value.domain);
            auto width  = domain.len() + usize(1) + value.field.len();
            if (width > label_width) label_width = width;
        }
        for (const auto& value : report.profile_values) {
            auto domain = build_option_domain_text(value.domain);
            auto label  = rstd::format("{} {}", domain, value.field.as_str());
            result.push_str("    "_str);
            result.push_str(label.as_str());
            for (auto padding = label.len(); padding < label_width + usize(2); ++padding) {
                result.push_ascii(' ');
            }
            result.push_str(value.value.as_str());
            result.push_str(" ("_str);
            result.push_str(value.source.as_str());
            result.push_str(")\n"_str);
        }
    }
    if (! report.options.is_empty()) {
        result.push_str("  Build options\n"_str);
        auto label_width = usize {};
        for (const auto& input : report.options) {
            auto domain = build_option_domain_text(input.domain);
            auto width  = domain.len() + usize(1) + input.source.len();
            if (width > label_width) label_width = width;
        }
        for (const auto& input : report.options) {
            auto domain = build_option_domain_text(input.domain);
            auto label  = rstd::format("{} {}", domain, input.source.as_str());
            result.push_str("    "_str);
            result.push_str(label.as_str());
            for (auto padding = label.len(); padding < label_width + usize(2); ++padding) {
                result.push_ascii(' ');
            }
            result.push_str(lito::system::command_text(input.arguments).as_str());
            result.push_ascii('\n');
        }
    }
    result.push_ascii('\n');
    return result;
}

void report_build_setup(void* raw_context, const lito::BuildSetupReport& report) noexcept {
    auto& context = *static_cast<EventContext*>(raw_context);
    auto  output  = render_build_setup(report);
    if (context.standard_error)
        rstd::io::eprint("{}", output.as_str());
    else
        rstd::io::print("{}", output.as_str());
}

void report_host_tool_resolution(void*                                   raw_context,
                                 const lito::system::HostToolResolution& resolution) noexcept {
    auto& context = *static_cast<EventContext*>(raw_context);
    if (resolution.kind == lito::system::HostToolResolution::Kind::CandidateMissing) {
        if (! context.verbose) return;
        if (context.standard_error) {
            rstd::io::eprintln(
                "[tool-candidate] {} {} '{}' not found",
                lito::system::host_tool_capability_name(resolution.requirement.capability),
                resolution.provider.as_str(),
                resolution.requested.as_path());
        } else {
            rstd::io::println(
                "[tool-candidate] {} {} '{}' not found",
                lito::system::host_tool_capability_name(resolution.requirement.capability),
                resolution.provider.as_str(),
                resolution.requested.as_path());
        }
        return;
    }
    if (resolution.kind == lito::system::HostToolResolution::Kind::NotRequired) {
        if (! context.verbose) return;
        if (context.standard_error) {
            rstd::io::eprintln(
                "[tool-reuse] {} {}",
                lito::system::host_tool_capability_name(resolution.requirement.capability),
                resolution.detail.as_str());
        } else {
            rstd::io::println(
                "[tool-reuse] {} {}",
                lito::system::host_tool_capability_name(resolution.requirement.capability),
                resolution.detail.as_str());
        }
        return;
    }
    if (resolution.executable.is_none()) return;
    auto selected = resolution.requested.as_path() == resolution.executable->as_path()
                        ? rstd::format("{}", resolution.executable->as_path())
                        : rstd::format("{} -> {}",
                                       resolution.requested.as_path(),
                                       resolution.executable->as_path());
    if (context.standard_error) {
        rstd::io::eprintln(
            "[tool] {} {} {}",
            lito::system::host_tool_capability_name(resolution.requirement.capability),
            resolution.provider.as_str(),
            selected.as_str());
    } else {
        rstd::io::println(
            "[tool] {} {} {}",
            lito::system::host_tool_capability_name(resolution.requirement.capability),
            resolution.provider.as_str(),
            selected.as_str());
    }
}

void observe_sdk(void*, const lito::SdkEvent& event) noexcept {
    auto label = "certify"_str;
    switch (event.kind) {
    case lito::SdkEventKind::Fetch: label = "fetch"_str; break;
    case lito::SdkEventKind::Extract: label = "extract"_str; break;
    case lito::SdkEventKind::Build: label = "build"_str; break;
    case lito::SdkEventKind::Link: label = "link"_str; break;
    case lito::SdkEventKind::Install: label = "install"_str; break;
    case lito::SdkEventKind::Certify: label = "certify"_str; break;
    }
    rstd::io::println(
        "[{}] llvm-sdk@{} {} {}", label, event.version, event.source, event.destination);
}

auto sdk_status_text(lito::SdkListStatus status) -> ref<str> {
    switch (status) {
    case lito::SdkListStatus::Available: return "available"_str;
    case lito::SdkListStatus::Installed: return "installed"_str;
    case lito::SdkListStatus::InstalledUnavailable: return "installed, unavailable"_str;
    case lito::SdkListStatus::Invalid: return "invalid"_str;
    }
    rstd::unreachable();
}

void append_column(String& output, ref<str> value, usize width) {
    output.push_str(value);
    for (auto padding = value.len(); padding < width; ++padding) output.push_ascii(' ');
}

void render_sdk_list(const lito::SdkListSummary& summary) {
    if (summary.entries.is_empty()) {
        rstd::io::println("no LLVM SDKs are available for {}", summary.host);
        return;
    }
    auto version_width = "VERSION"_str.len();
    auto host_width    = "HOST"_str.len();
    auto status_width  = "STATUS"_str.len();
    for (const auto& entry : summary.entries) {
        if (entry.version.len() > version_width) version_width = entry.version.len();
        if (entry.host.len() > host_width) host_width = entry.host.len();
        auto status = sdk_status_text(entry.status);
        if (status.len() > status_width) status_width = status.len();
    }
    auto output = String::make();
    append_column(output, "VERSION"_str, version_width);
    output.push_str("  "_str);
    append_column(output, "HOST"_str, host_width);
    output.push_str("  "_str);
    append_column(output, "STATUS"_str, status_width);
    output.push_ascii('\n');
    for (const auto& entry : summary.entries) {
        append_column(output, entry.version.as_str(), version_width);
        output.push_str("  "_str);
        append_column(output, entry.host.as_str(), host_width);
        output.push_str("  "_str);
        append_column(output, sdk_status_text(entry.status), status_width);
        if (entry.prefix.is_some()) {
            output.push_str("  "_str);
            output.push_str(rstd::format("{}", entry.prefix->as_path()).as_str());
        }
        if (entry.issue.is_some()) {
            output.push_str("  "_str);
            output.push_str(entry.issue->as_str());
        }
        output.push_ascii('\n');
    }
    rstd::io::print("{}", output.as_str());
}

auto configure_build_output(lito::BuildRequest& request, EventContext& context) -> void {
    request.observer       = Some(lito::BuildEventSink {
        .context = rstd::addressof(context),
        .notify  = observe,
    });
    request.setup_reporter = Some(lito::BuildSetupReportSink {
        .context = rstd::addressof(context),
        .notify  = report_build_setup,
    });
    request.tool_reporter  = Some(lito::system::HostToolResolutionSink {
        .context = rstd::addressof(context),
        .notify  = report_host_tool_resolution,
    });
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

void observe_bench(void* raw_context, const lito::BenchEvent& event) noexcept {
    auto& context = *static_cast<EventContext*>(raw_context);
    if (! context.verbose) return;
    rstd::io::println("[run] {} {} (cwd {})",
                      lito::package::package_target_id_text(event.target),
                      event.executable,
                      event.working_directory);
    for (const auto& argument : event.arguments) {
        rstd::io::println("  [arg] {}", argument.as_str());
    }
}

void observe_doc(void* raw_context, const lito::DocEvent& event) noexcept {
    auto& context = *static_cast<EventContext*>(raw_context);
    if (! context.verbose && (event.kind == lito::DocEventKind::ToolReuse ||
                              event.kind == lito::DocEventKind::ExtractReuse)) {
        return;
    }
    rstd::io::println("[{}] {} {}", event.kind, event.target, event.path);
}

auto project_output_path(ref<rstd::path::Path> root, rstd::path::PathBuf path)
    -> rstd::path::PathBuf {
    return path.as_path().is_absolute() ? rstd::move(path)
                                        : rstd::path::PathBuf::from(root).join(path.as_path());
}

auto build_configuration(lito::config::ToolchainSpec          toolchain,
                         lito::config::StandardLibrary        standard_library,
                         lito::config::StandardLibraryRuntime standard_library_runtime,
                         lito::config::ProjectBuildOptions    options)
    -> lito::cpp::BuildConfiguration {
    return lito::cpp::BuildConfiguration {
        .toolchain                = rstd::move(toolchain),
        .standard_library         = standard_library,
        .standard_library_runtime = standard_library_runtime,
        .bmi_mode                 = lito::cpp::BmiMode::Reduced,
        .language_standard        = String::make("c++20"_str),
        .global_options           = rstd::move(options),
    };
}

struct ArtifactCounts {
    usize archives {};
    usize executables {};
    usize tests {};
    usize benchmarks {};
    usize compile_tests {};
};

auto artifact_counts(const lito::BuildSummary& summary) -> ArtifactCounts {
    auto counts = ArtifactCounts {};
    for (const auto& artifact : summary.artifacts) {
        switch (artifact.kind) {
        case lito::cpp::ArtifactKind::StaticLibrary: ++counts.archives; break;
        case lito::cpp::ArtifactKind::TestAttachmentArchive: ++counts.archives; break;
        case lito::cpp::ArtifactKind::Executable: ++counts.executables; break;
        case lito::cpp::ArtifactKind::TestExecutable: ++counts.tests; break;
        case lito::cpp::ArtifactKind::BenchmarkExecutable: ++counts.benchmarks; break;
        case lito::cpp::ArtifactKind::CompileTest: ++counts.compile_tests; break;
        }
    }
    return counts;
}

auto install_action_label(lito::InstallAction action) -> ref<str> {
    switch (action) {
    case lito::InstallAction::Created: return "install"_str;
    case lito::InstallAction::Replaced: return "update"_str;
    case lito::InstallAction::Unchanged: return "install"_str;
    }
    rstd::unreachable();
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

void apply_source_options(lito::source::PackageSourceConfig& sources,
                          ref<rstd::path::Path>              root,
                          bool                               offline,
                          bool                               frozen,
                          Vec<rstd::path::PathBuf>           seeds) {
    if (offline || frozen) sources.network = lito::source::NetworkPolicy::Offline;
    for (auto& seed : seeds) {
        if (seed.as_path().is_relative()) {
            seed = rstd::path::PathBuf::from(root).join(seed.as_path());
        }
        sources.fetch_seeds.push(rstd::move(seed));
    }
}

template<typename E>
    requires Impled<E, rstd::error::Error>
void report_error(const E& error) {
    rstd::io::eprintln("lito: {}", error);
    const void* seen_data[32] {};
    const void* seen_metadata[32] {};
    auto        seen_count = usize {};
    auto        source     = as<rstd::error::Error>(error).source();
    while (source.is_some() && seen_count < usize(32)) {
        auto address  = source->as_raw_ptr();
        auto metadata = static_cast<const void*>(source->metadata());
        auto repeated = false;
        for (auto index = usize {}; index < seen_count; ++index) {
            if (seen_data[index.to_primitive()] == address &&
                seen_metadata[index.to_primitive()] == metadata) {
                repeated = true;
                break;
            }
        }
        if (repeated) break;
        seen_data[seen_count.to_primitive()]     = address;
        seen_metadata[seen_count.to_primitive()] = metadata;
        ++seen_count;
        rstd::io::eprintln("  caused by: {}", *source);
        auto next = (*source)->source();
        source    = rstd::move(next);
    }
}

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
    auto invocation = rstd::move(parsed).as_Parsed();
    if (invocation.command.is_Config()) {
        auto command = rstd::move(invocation.command).as_Config().command;
        if (command.is_Path()) {
            auto path = lito::config::project_config_path(invocation.working_directory.as_path());
            if (path.is_err()) {
                auto error = rstd::move(path).unwrap_err();
                report_error(error);
                return 1;
            }
            rstd::io::println("{}", path->as_path());
            return 0;
        }
        if (command.is_Get()) {
            auto options = rstd::move(command).as_Get().options;
            auto query = lito::config::get_persisted_config(invocation.working_directory.as_path(),
                                                            rstd::move(options.key));
            if (query.is_err()) {
                auto error = rstd::move(query).unwrap_err();
                report_error(error);
                return 1;
            }
            rstd::io::print("{}", query->output.as_str());
            return 0;
        }
        if (command.is_Set()) {
            auto options = rstd::move(command).as_Set().options;
            auto mutation =
                lito::config::set_persisted_config(invocation.working_directory.as_path(),
                                                   options.key.as_str(),
                                                   options.value.as_str());
            if (mutation.is_err()) {
                auto error = rstd::move(mutation).unwrap_err();
                report_error(error);
                return 1;
            }
            rstd::io::println("set {} in {}", mutation->key, mutation->path.as_path());
            return 0;
        }
        auto options  = rstd::move(command).as_Unset().options;
        auto mutation = lito::config::unset_persisted_config(invocation.working_directory.as_path(),
                                                             options.key.as_str());
        if (mutation.is_err()) {
            auto error = rstd::move(mutation).unwrap_err();
            report_error(error);
            return 1;
        }
        rstd::io::println("unset {} in {}", mutation->key, mutation->path.as_path());
        return 0;
    }
    if (invocation.command.is_Sdk()) {
        auto command = rstd::move(invocation.command).as_Sdk().command;
        if (command.is_List()) {
            auto result = lito::list_llvm_sdks();
            if (result.is_err()) {
                auto error = rstd::move(result).unwrap_err();
                report_error(error);
                return 1;
            }
            render_sdk_list(*result);
            return 0;
        }
        auto options = rstd::move(command).as_Install().options;
        auto loaded  = lito::config::load_host_tool_command_config(
            invocation.working_directory.as_path(),
            lito::config::ProjectConfigRequest {
                .mode      = invocation.no_config ? lito::config::ConfigLoadMode::LocalDisabled
                                                  : lito::config::ConfigLoadMode::Enabled,
                .overrides = rstd::move(invocation.config_overrides),
            });
        if (loaded.is_err()) {
            auto error = rstd::move(loaded).unwrap_err();
            report_error(error);
            return 1;
        }
        auto config        = rstd::move(loaded).unwrap();
        auto event_context = EventContext {};
        auto result        = lito::install_llvm_sdk(lito::SdkInstallRequest {
            .version       = rstd::move(options.version),
            .environment   = rstd::move(config.environment),
            .tools         = rstd::move(config.tools),
            .toolchain     = rstd::move(config.toolchain),
            .tool_reporter = Some(lito::system::HostToolResolutionSink {
                .context = rstd::addressof(event_context),
                .notify  = report_host_tool_resolution,
            }),
            .observer      = Some(lito::SdkEventSink {
                .notify = observe_sdk,
            }),
        });
        if (result.is_err()) {
            auto error = rstd::move(result).unwrap_err();
            report_error(error);
            return 1;
        }
        auto summary = rstd::move(result).unwrap();
        rstd::io::println("{} LLVM SDK {} for {} at {}",
                          summary.reused ? "reused"_str : "installed"_str,
                          summary.version,
                          summary.host,
                          summary.prefix.as_path());
        return 0;
    }
    auto install_source = Option<lito::ResolvedInstallSource> {};
    if (invocation.command.is_Install()) {
        auto resolved = lito::resolve_install_source(
            lito::InstallSourceRequirement::LocalProject(invocation.working_directory.clone()));
        if (resolved.is_err()) {
            auto error = rstd::move(resolved).unwrap_err();
            report_error(error);
            return 1;
        }
        install_source = Some(rstd::move(resolved).unwrap());
    }
    auto config_root   = install_source.is_some() ? install_source->project.root.as_path()
                                                  : invocation.working_directory.as_path();
    auto loaded_config = lito::config::load_project_config(
        config_root,
        lito::config::ProjectConfigRequest {
            .mode              = invocation.no_config ? lito::config::ConfigLoadMode::LocalDisabled
                                                      : lito::config::ConfigLoadMode::Enabled,
            .overrides         = rstd::move(invocation.config_overrides),
            .environment_flags = invocation.use_env_flags
                                     ? lito::config::EnvironmentFlagPolicy::Append
                                     : lito::config::EnvironmentFlagPolicy::Ignore,
        });
    if (loaded_config.is_err()) {
        auto error = rstd::move(loaded_config).unwrap_err();
        report_error(error);
        return 1;
    }
    auto project = rstd::move(loaded_config).unwrap();

    if (invocation.command.is_Lock()) {
        auto command = rstd::move(invocation.command).as_Lock().command;
        auto options = rstd::move(command).as_Export().options;
        switch (options.format) {
        case lito::lock::LockExportFormat::FlatpakSources: {
            auto result = lito::lock::export_flatpak_sources(
                project.root.as_path(), project.lock, options.output.as_path());
            if (result.is_err()) {
                auto error = rstd::move(result).unwrap_err();
                report_error(error);
                return 1;
            }
            auto output = options.output.as_path().is_absolute()
                              ? rstd::move(options.output)
                              : project.root.join(options.output.as_path());
            rstd::io::println("exported Flatpak sources to {}", output.as_path());
            return 0;
        }
        }
        __builtin_unreachable();
    }

    if (invocation.command.is_Install()) {
        auto options = rstd::move(invocation.command).as_Install().options;
        apply_source_options(project.sources,
                             project.root.as_path(),
                             options.offline,
                             options.frozen,
                             rstd::move(options.fetch_seeds));
        auto destination = lito::resolve_install_destination(invocation.working_directory.as_path(),
                                                             rstd::move(options.destination),
                                                             project.install);
        if (destination.is_err()) {
            auto error = rstd::move(destination).unwrap_err();
            report_error(error);
            return 1;
        }
        auto timing  = make_timing_output(project.root.as_path(),
                                          rstd::move(options.timing_file),
                                          options.verbose && ! options.no_timing);
        auto request = lito::InstallRequest {
            .source      = rstd::move(install_source).unwrap(),
            .destination = rstd::move(destination).unwrap(),
        };
        request.binaries             = rstd::move(options.binaries);
        request.force                = options.force;
        request.build.selection.root = project.root.clone();
        request.build.environment    = rstd::move(project.environment);
        request.build.tools          = rstd::move(project.tools);
        request.build.configuration  = build_configuration(rstd::move(project.toolchain),
                                                           project.standard_library,
                                                           project.standard_library_runtime,
                                                           rstd::move(project.build_options));
        request.build.lock           = rstd::move(project.lock);
        request.build.sources        = rstd::move(project.sources);
        request.build.pkg_config     = rstd::move(project.pkg_config);
        request.build.cmake          = rstd::move(project.cmake);
        request.build.cmake_build_overrides = rstd::move(project.cmake_build_overrides);
        request.build.selection.packages    = rstd::move(options.packages);
        request.build.selection.features    = rstd::move(options.features);
        request.build.locked                = options.locked || options.frozen;
        if (options.profile.is_some()) request.build.profile = Some(options.profile->clone());
        request.build.execution.scan.jobs    = options.jobs;
        request.build.execution.compile.jobs = options.jobs;
        auto event_context                   = EventContext { .verbose = options.verbose };
        configure_build_output(request.build, event_context);
        auto result = lito::install(rstd::move(request));
        if (result.is_err()) {
            auto error = rstd::move(result).unwrap_err();
            report_error(error);
            return 1;
        }
        auto summary = rstd::move(result).unwrap();
        if (summary.destination.is_Prefix()) {
            for (const auto& entry : summary.entries) {
                rstd::io::println(
                    "[{}] {}", install_action_label(entry.action), entry.destination.as_path());
            }
        } else {
            for (const auto& link : summary.links) {
                rstd::io::println("[install] {}", link.destination.as_path());
            }
            for (const auto& entry : summary.entries) {
                if (entry.origin.is_BuildArtifact()) {
                    auto linked = false;
                    for (const auto& link : summary.links) {
                        if (link.target == entry.origin.as_BuildArtifact().target) linked = true;
                    }
                    if (linked) continue;
                    rstd::io::println("[install] {}", entry.destination.as_path());
                }
            }
        }
        auto emitted = lito::timing_output::emit(summary.build, timing);
        if (emitted.is_err()) {
            auto error = rstd::move(emitted).unwrap_err();
            report_error(error);
            return 1;
        }
        rstd::io::println("installed {} entries from {} packages ({}) to {} {}",
                          summary.entries.len(),
                          summary.packages.len(),
                          summary.build.profile.as_str(),
                          summary.destination.is_Managed() ? "managed root"_str
                                                           : "untracked prefix"_str,
                          summary.destination.path());
        return 0;
    }

    if (invocation.command.is_Update()) {
        auto options = rstd::move(invocation.command).as_Update().options;
        apply_source_options(project.sources,
                             project.root.as_path(),
                             options.offline,
                             false,
                             rstd::move(options.fetch_seeds));
        auto event_context = EventContext {};
        auto request       = lito::UpdateRequest {
            .root          = rstd::move(project.root),
            .environment   = rstd::move(project.environment),
            .tools         = rstd::move(project.tools),
            .lock          = rstd::move(project.lock),
            .sources       = rstd::move(project.sources),
            .observer      = Some(lito::BuildEventSink {
                .context = rstd::addressof(event_context),
                .notify  = observe,
            }),
            .tool_reporter = Some(lito::system::HostToolResolutionSink {
                .context = rstd::addressof(event_context),
                .notify  = report_host_tool_resolution,
            }),
        };
        auto result = lito::update_dependencies(request);
        if (result.is_err()) {
            auto error = rstd::move(result).unwrap_err();
            report_error(error);
            return 1;
        }
        if (*result == lito::lock::LockStatus::Updated)
            rstd::io::println("updated {}", request.lock.path.as_path());
        else
            rstd::io::println("dependencies are up to date");
        return 0;
    }

    if (invocation.command.is_Format()) {
        auto options               = rstd::move(invocation.command).as_Format().options;
        auto request               = lito::FormatRequest {};
        request.selection.root     = rstd::move(project.root);
        request.environment        = rstd::move(project.environment);
        request.tools              = rstd::move(project.tools);
        request.lock               = rstd::move(project.lock);
        request.sources            = rstd::move(project.sources);
        request.selection.packages = rstd::move(options.packages);
        request.mode          = options.check ? lito::FormatMode::Check : lito::FormatMode::Write;
        auto event_context    = EventContext {};
        request.observer      = Some(lito::BuildEventSink {
            .context = rstd::addressof(event_context),
            .notify  = observe,
        });
        request.tool_reporter = Some(lito::system::HostToolResolutionSink {
            .context = rstd::addressof(event_context),
            .notify  = report_host_tool_resolution,
        });
        auto result           = lito::format(request);
        if (result.is_err()) {
            auto error = rstd::move(result).unwrap_err();
            report_error(error);
            return 1;
        }
        auto summary = rstd::move(result).unwrap();
        if (options.check) {
            for (const auto& path : summary.unformatted_files) {
                rstd::io::eprintln("would reformat {}", path.as_path());
            }
            if (! summary.success()) return 1;
            rstd::io::println("checked {} packages, {} files", summary.packages, summary.files);
            return 0;
        }
        rstd::io::println("formatted {} packages, {} files", summary.packages, summary.files);
        return 0;
    }

    if (invocation.command.is_Scan()) {
        auto options = rstd::move(invocation.command).as_Scan().options;
        apply_source_options(project.sources,
                             project.root.as_path(),
                             options.offline,
                             options.frozen,
                             rstd::move(options.fetch_seeds));
        auto request                  = lito::ScanRequest {};
        request.selection.root        = rstd::move(project.root);
        request.environment           = rstd::move(project.environment);
        request.tools                 = rstd::move(project.tools);
        request.configuration         = build_configuration(rstd::move(project.toolchain),
                                                            project.standard_library,
                                                            project.standard_library_runtime,
                                                            rstd::move(project.build_options));
        request.lock                  = rstd::move(project.lock);
        request.sources               = rstd::move(project.sources);
        request.pkg_config            = rstd::move(project.pkg_config);
        request.cmake                 = rstd::move(project.cmake);
        request.cmake_build_overrides = rstd::move(project.cmake_build_overrides);
        request.selection.packages    = rstd::move(options.packages);
        request.selection.features    = rstd::move(options.features);
        request.targets               = rstd::move(options.targets);
        request.source                = rstd::move(options.source);
        request.locked                = options.locked || options.frozen;
        if (options.profile.is_some()) request.profile = Some(options.profile->clone());
        auto event_context     = EventContext { .standard_error = true };
        request.observer       = Some(lito::BuildEventSink {
            .context = rstd::addressof(event_context),
            .notify  = observe,
        });
        request.setup_reporter = Some(lito::BuildSetupReportSink {
            .context = rstd::addressof(event_context),
            .notify  = report_build_setup,
        });
        request.tool_reporter  = Some(lito::system::HostToolResolutionSink {
            .context = rstd::addressof(event_context),
            .notify  = report_host_tool_resolution,
        });

        auto scanned = lito::scan(request);
        if (scanned.is_err()) {
            auto error = rstd::move(scanned).unwrap_err();
            report_error(error);
            return 1;
        }
        auto json = lito::scan_report_json(*scanned, options.format);
        if (json.is_err()) {
            auto error = rstd::move(json).unwrap_err();
            report_error(error);
            return 1;
        }
        rstd::io::println("{}", json->as_str());
        return 0;
    }

    if (invocation.command.is_Doc()) {
        auto options = rstd::move(invocation.command).as_Doc().options;
        apply_source_options(project.sources,
                             project.root.as_path(),
                             options.offline,
                             options.frozen,
                             rstd::move(options.fetch_seeds));
        auto timing                  = make_timing_output(project.root.as_path(),
                                                          rstd::move(options.timing_file),
                                                          options.verbose && ! options.no_timing);
        auto request                 = lito::DocRequest {};
        request.build.selection.root = project.root.clone();
        request.build.environment    = rstd::move(project.environment);
        request.build.tools          = rstd::move(project.tools);
        request.build.configuration  = build_configuration(rstd::move(project.toolchain),
                                                           project.standard_library,
                                                           project.standard_library_runtime,
                                                           rstd::move(project.build_options));
        request.build.lock           = rstd::move(project.lock);
        request.build.sources        = rstd::move(project.sources);
        request.build.pkg_config     = rstd::move(project.pkg_config);
        request.build.cmake          = rstd::move(project.cmake);
        request.build.cmake_build_overrides = rstd::move(project.cmake_build_overrides);
        request.build.purpose               = lito::package::PackageSelectionPurpose::Documentation;
        request.build.selection.packages    = rstd::move(options.packages);
        request.build.selection.features    = rstd::move(options.features);
        request.build.targets               = rstd::move(options.targets);
        request.build.locked                = options.locked || options.frozen;
        if (options.profile.is_some()) request.build.profile = Some(options.profile->clone());
        request.build.execution.scan.jobs    = options.jobs;
        request.build.execution.compile.jobs = options.jobs;
        request.config                       = rstd::move(project.doc);
        if (options.output.is_some()) {
            request.output =
                project_output_path(project.root.as_path(), rstd::move(options.output).unwrap());
        }
        if (options.publication_dir.is_some()) {
            request.output = project_output_path(project.root.as_path(),
                                                 rstd::move(options.publication_dir).unwrap());
            request.package_publication = true;
        }
        if (options.data_output.is_some()) {
            request.data_output = project_output_path(project.root.as_path(),
                                                      rstd::move(options.data_output).unwrap());
        }
        if (options.frontend.is_some()) {
            request.frontend = Some(
                project_output_path(project.root.as_path(), rstd::move(options.frontend).unwrap()));
        }
        request.data_only  = options.data_only;
        auto event_context = EventContext { .verbose = options.verbose };
        configure_build_output(request.build, event_context);
        request.observer = Some(lito::DocEventSink {
            .context = rstd::addressof(event_context),
            .notify  = observe_doc,
        });
        auto result      = lito::doc(rstd::move(request));
        if (result.is_err()) {
            auto error = rstd::move(result).unwrap_err();
            report_error(error);
            return 1;
        }
        auto summary = rstd::move(result).unwrap();
        auto emitted = lito::timing_output::emit(summary.build, timing);
        if (emitted.is_err()) {
            auto error = rstd::move(emitted).unwrap_err();
            report_error(error);
            return 1;
        }
        if (summary.publication_receipt.is_some()) {
            rstd::io::println(
                "generated package publications for {} packages: {} ({} extracted, {} reused)",
                summary.build.selected_packages.len(),
                summary.publication_receipt->as_path(),
                summary.extracted,
                summary.reused);
        } else {
            rstd::io::println(
                "generated documentation for {} packages in {}: {} extracted, {} reused",
                summary.build.selected_packages.len(),
                options.data_only ? summary.data_output.as_path() : summary.output.as_path(),
                summary.extracted,
                summary.reused);
        }
        return 0;
    }

    if (invocation.command.is_Test()) {
        auto options = rstd::move(invocation.command).as_Test().options;
        apply_source_options(project.sources,
                             project.root.as_path(),
                             options.offline,
                             options.frozen,
                             rstd::move(options.fetch_seeds));
        auto timing                  = make_timing_output(project.root.as_path(),
                                                          rstd::move(options.timing_file),
                                                          options.verbose && ! options.no_timing);
        auto request                 = lito::TestRequest {};
        request.build.selection.root = rstd::move(project.root);
        request.build.environment    = rstd::move(project.environment);
        request.build.tools          = rstd::move(project.tools);
        request.build.configuration  = build_configuration(rstd::move(project.toolchain),
                                                           project.standard_library,
                                                           project.standard_library_runtime,
                                                           rstd::move(project.build_options));
        request.build.lock           = rstd::move(project.lock);
        request.build.sources        = rstd::move(project.sources);
        request.build.pkg_config     = rstd::move(project.pkg_config);
        request.build.cmake          = rstd::move(project.cmake);
        request.build.cmake_build_overrides = rstd::move(project.cmake_build_overrides);
        request.build.selection.packages    = rstd::move(options.packages);
        request.build.selection.features    = rstd::move(options.features);
        request.build.targets               = rstd::move(options.targets);
        request.build.locked                = options.locked || options.frozen;
        request.arguments                   = rstd::move(options.arguments);
        request.no_run                      = options.no_run;
        if (options.profile.is_some()) {
            request.build.profile = Some(options.profile->clone());
        }
        request.build.execution.scan.jobs    = options.jobs;
        request.build.execution.compile.jobs = options.jobs;
        if (options.output.is_some()) request.build.output = rstd::move(*options.output);
        auto event_context = EventContext { .verbose = options.verbose };
        configure_build_output(request.build, event_context);
        request.observer = Some(lito::TestObserver {
            .context = rstd::addressof(event_context),
            .notify  = observe_test,
        });

        auto result = lito::test(rstd::move(request));
        if (result.is_err()) {
            auto error = rstd::move(result).unwrap_err();
            report_error(error);
            return 1;
        }
        auto summary = rstd::move(result).unwrap();
        auto counts  = artifact_counts(summary.build);
        auto emitted = lito::timing_output::emit(summary.build, timing);
        if (emitted.is_err()) {
            auto error = rstd::move(emitted).unwrap_err();
            report_error(error);
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
                rstd::io::println("[pass] {} ({} ms)",
                                  lito::package::package_target_id_text(execution.target),
                                  execution.elapsed.as_millis());
                continue;
            }
            ++failed;
            if (execution.error.is_some()) {
                rstd::io::eprintln("[fail] {} in {}: {}",
                                   lito::package::package_target_id_text(execution.target),
                                   execution.working_directory.as_path(),
                                   *execution.error);
            } else if (execution.status->code().is_some()) {
                rstd::io::eprintln("[fail] {} in {}: exit code {}",
                                   lito::package::package_target_id_text(execution.target),
                                   execution.working_directory.as_path(),
                                   *execution.status->code());
            } else {
                rstd::io::eprintln("[fail] {} in {}: signal {}",
                                   lito::package::package_target_id_text(execution.target),
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

    if (invocation.command.is_Bench()) {
        auto options = rstd::move(invocation.command).as_Bench().options;
        apply_source_options(project.sources,
                             project.root.as_path(),
                             options.offline,
                             options.frozen,
                             rstd::move(options.fetch_seeds));
        auto timing                  = make_timing_output(project.root.as_path(),
                                                          rstd::move(options.timing_file),
                                                          options.verbose && ! options.no_timing);
        auto request                 = lito::BenchRequest {};
        request.build.selection.root = rstd::move(project.root);
        request.build.environment    = rstd::move(project.environment);
        request.build.tools          = rstd::move(project.tools);
        request.build.configuration  = build_configuration(rstd::move(project.toolchain),
                                                           project.standard_library,
                                                           project.standard_library_runtime,
                                                           rstd::move(project.build_options));
        request.build.lock           = rstd::move(project.lock);
        request.build.sources        = rstd::move(project.sources);
        request.build.pkg_config     = rstd::move(project.pkg_config);
        request.build.cmake          = rstd::move(project.cmake);
        request.build.cmake_build_overrides = rstd::move(project.cmake_build_overrides);
        request.build.selection.packages    = rstd::move(options.packages);
        request.build.selection.features    = rstd::move(options.features);
        request.build.targets               = rstd::move(options.targets);
        request.build.locked                = options.locked || options.frozen;
        request.arguments                   = rstd::move(options.arguments);
        request.no_run                      = options.no_run;
        if (options.profile.is_some()) request.build.profile = Some(options.profile->clone());
        request.build.execution.scan.jobs    = options.jobs;
        request.build.execution.compile.jobs = options.jobs;
        if (options.output.is_some()) request.build.output = rstd::move(*options.output);
        auto event_context = EventContext { .verbose = options.verbose };
        configure_build_output(request.build, event_context);
        request.observer = Some(lito::BenchObserver {
            .context = rstd::addressof(event_context),
            .notify  = observe_bench,
        });

        auto result = lito::bench(rstd::move(request));
        if (result.is_err()) {
            auto error = rstd::move(result).unwrap_err();
            report_error(error);
            return 1;
        }
        auto summary = rstd::move(result).unwrap();
        auto counts  = artifact_counts(summary.build);
        auto emitted = lito::timing_output::emit(summary.build, timing);
        if (emitted.is_err()) {
            auto error = rstd::move(emitted).unwrap_err();
            report_error(error);
            return 1;
        }
        if (options.no_run) {
            rstd::io::println("built {} benchmarks ({}) in {}: {} scanned, {} compiled, {} reused",
                              counts.benchmarks,
                              summary.build.profile.as_str(),
                              summary.build.output.as_path(),
                              summary.build.scanned,
                              summary.build.compiled,
                              summary.build.reused);
            return 0;
        }

        auto passed = usize {};
        auto failed = usize {};
        for (const auto& execution : summary.executions) {
            if (execution.success()) {
                ++passed;
                rstd::io::println("[pass] {} ({} ms)",
                                  lito::package::package_target_id_text(execution.target),
                                  execution.elapsed.as_millis());
                continue;
            }
            ++failed;
            if (execution.error.is_some()) {
                rstd::io::eprintln("[fail] {} in {}: {}",
                                   lito::package::package_target_id_text(execution.target),
                                   execution.working_directory.as_path(),
                                   *execution.error);
            } else if (execution.status->code().is_some()) {
                rstd::io::eprintln("[fail] {} in {}: exit code {}",
                                   lito::package::package_target_id_text(execution.target),
                                   execution.working_directory.as_path(),
                                   *execution.status->code());
            } else {
                rstd::io::eprintln("[fail] {} in {}: signal {}",
                                   lito::package::package_target_id_text(execution.target),
                                   execution.working_directory.as_path(),
                                   *execution.status->signal());
            }
        }
        rstd::io::println("benchmark result: {}. {} passed; {} failed",
                          failed == usize {} ? "ok"_str : "failed"_str,
                          passed,
                          failed);
        return failed == usize {} ? 0 : 1;
    }

    auto options = rstd::move(invocation.command).as_Build().options;
    apply_source_options(project.sources,
                         project.root.as_path(),
                         options.offline,
                         options.frozen,
                         rstd::move(options.fetch_seeds));
    auto timing                   = make_timing_output(project.root.as_path(),
                                                       rstd::move(options.timing_file),
                                                       options.verbose && ! options.no_timing);
    auto request                  = lito::BuildRequest {};
    request.selection.root        = rstd::move(project.root);
    request.environment           = rstd::move(project.environment);
    request.tools                 = rstd::move(project.tools);
    request.configuration         = build_configuration(rstd::move(project.toolchain),
                                                        project.standard_library,
                                                        project.standard_library_runtime,
                                                        rstd::move(project.build_options));
    request.lock                  = rstd::move(project.lock);
    request.sources               = rstd::move(project.sources);
    request.pkg_config            = rstd::move(project.pkg_config);
    request.cmake                 = rstd::move(project.cmake);
    request.cmake_build_overrides = rstd::move(project.cmake_build_overrides);
    request.selection.packages    = rstd::move(options.packages);
    request.selection.features    = rstd::move(options.features);
    request.targets               = rstd::move(options.targets);
    request.locked                = options.locked || options.frozen;
    if (options.profile.is_some()) request.profile = Some(options.profile->clone());
    request.execution.scan.jobs    = options.jobs;
    request.execution.compile.jobs = options.jobs;
    if (options.output.is_some()) request.output = rstd::move(*options.output);
    auto event_context = EventContext { .verbose = options.verbose };

    configure_build_output(request, event_context);
    auto result = lito::build(request);
    if (result.is_err()) {
        auto error = rstd::move(result).unwrap_err();
        report_error(error);
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
        auto error = rstd::move(emitted).unwrap_err();
        report_error(error);
        return 1;
    }
    return 0;
}
