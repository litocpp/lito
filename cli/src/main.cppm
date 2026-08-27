export module lito.executable;

import rstd;
import lito.tools;
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
        event.kind != lito::BuildEventKind::Strip &&
        event.kind != lito::BuildEventKind::ProductFinalize &&
        event.kind != lito::BuildEventKind::ProductPublish) {
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
    result.push_str(rstd::format("  Target\n"
                                 "    host           {}\n"
                                 "    effective      {} ({})\n"
                                 "    stdlib         {}\n"
                                 "    backends       {}\n",
                                 report.target.host.as_str(),
                                 report.target.effective.as_str(),
                                 report.target.source.as_str(),
                                 report.target.standard_library.as_str(),
                                 report.target.supported_targets.as_str())
                        .as_str());
    if (report.target.android_abi.is_some()) {
        result.push_str(rstd::format("    Android ABI    {}\n    minimum API    {}\n",
                                     report.target.android_abi->as_str(),
                                     *report.target.android_minimum_api)
                            .as_str());
    }
    if (report.target.sdk_kind.is_some()) {
        result.push_str(rstd::format("    SDK            {} {}\n",
                                     report.target.sdk_kind->as_str(),
                                     report.target.sdk_version.is_some()
                                         ? report.target.sdk_version->as_str()
                                         : "<external>"_str)
                            .as_str());
    }
    if (report.target.sysroot.is_some()) {
        result.push_str(
            rstd::format("    sysroot        {}\n", report.target.sysroot->as_path()).as_str());
    }
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
    if (! report.script_packages.is_empty()) {
        result.push_str("  Script packages\n"_str);
        for (const auto& package : report.script_packages) {
            result.push_str(rstd::format("    {} {} <- {} ({}; {}; {}#{})\n",
                                         package.package.as_str(),
                                         package.require_name.as_str(),
                                         package.dependency.as_str(),
                                         package.source_identity.as_str(),
                                         package.supports.as_str(),
                                         package.entry.as_path(),
                                         package.entry_digest.as_str())
                                .as_str());
        }
    }
    if (! report.cargo_profiles.is_empty()) {
        result.push_str("  Cargo dependencies\n"_str);
        for (const auto& profile : report.cargo_profiles) {
            result.push_str(rstd::format("    {}:{} {} <- {} ({})\n",
                                         profile.package.as_str(),
                                         profile.dependency.as_str(),
                                         profile.selected.as_str(),
                                         profile.inherits.as_str(),
                                         profile.settings.as_str())
                                .as_str());
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

void report_host_tool_resolution(void*                                  raw_context,
                                 const lito::tools::HostToolResolution& resolution) noexcept {
    auto& context = *static_cast<EventContext*>(raw_context);
    if (resolution.kind == lito::tools::HostToolResolution::Kind::CandidateMissing) {
        if (! context.verbose) return;
        if (context.standard_error) {
            rstd::io::eprintln(
                "[tool-candidate] {} {} '{}' not found",
                lito::tools::host_tool_capability_name(resolution.requirement.capability),
                resolution.provider.as_str(),
                resolution.requested.as_path());
        } else {
            rstd::io::println(
                "[tool-candidate] {} {} '{}' not found",
                lito::tools::host_tool_capability_name(resolution.requirement.capability),
                resolution.provider.as_str(),
                resolution.requested.as_path());
        }
        return;
    }
    if (resolution.kind == lito::tools::HostToolResolution::Kind::NotRequired) {
        if (! context.verbose) return;
        if (context.standard_error) {
            rstd::io::eprintln(
                "[tool-reuse] {} {}",
                lito::tools::host_tool_capability_name(resolution.requirement.capability),
                resolution.detail.as_str());
        } else {
            rstd::io::println(
                "[tool-reuse] {} {}",
                lito::tools::host_tool_capability_name(resolution.requirement.capability),
                resolution.detail.as_str());
        }
        return;
    }
    if (resolution.executable.is_none()) return;
    auto selected  = resolution.requested.as_path() == resolution.executable->as_path()
                         ? rstd::format("{}", resolution.executable->as_path())
                         : rstd::format("{} -> {}",
                                        resolution.requested.as_path(),
                                        resolution.executable->as_path());
    auto requested = resolution.requested.as_path().to_str();
    if (requested.is_none() || *requested != resolution.provider.as_str()) {
        selected = rstd::format("{} {}", resolution.provider.as_str(), selected.as_str());
    }
    if (context.standard_error) {
        rstd::io::eprintln(
            "[tool] {} {}",
            lito::tools::host_tool_capability_name(resolution.requirement.capability),
            selected.as_str());
    } else {
        rstd::io::println("[tool] {} {}",
                          lito::tools::host_tool_capability_name(resolution.requirement.capability),
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
    rstd::io::println("[{}] {}@{} {} {}",
                      label,
                      lito::config::sdk_kind_name(event.sdk),
                      event.version,
                      event.source,
                      event.destination);
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

auto sdk_entry_status_text(const lito::SdkListEntry& entry) -> String {
    auto text = String::make(sdk_status_text(entry.status));
    if (entry.active) text.push_str(", active"_str);
    return text;
}

void append_column(String& output, ref<str> value, usize width) {
    output.push_str(value);
    for (auto padding = value.len(); padding < width; ++padding) output.push_ascii(' ');
}

void render_sdk_list(const lito::SdkListSummary& summary, ref<str> sdk_name) {
    if (summary.entries.is_empty()) {
        rstd::io::println("no {} releases are available for {}", sdk_name, summary.host);
        if (summary.active_issue.is_some()) {
            rstd::io::println("active SDK state: {}", *summary.active_issue);
        }
        return;
    }
    auto version_width = "VERSION"_str.len();
    auto host_width    = "HOST"_str.len();
    auto status_width  = "STATUS"_str.len();
    for (const auto& entry : summary.entries) {
        if (entry.version.len() > version_width) version_width = entry.version.len();
        if (entry.host.len() > host_width) host_width = entry.host.len();
        auto status = sdk_entry_status_text(entry);
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
        auto status = sdk_entry_status_text(entry);
        append_column(output, status.as_str(), status_width);
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
    if (summary.active_issue.is_some()) {
        output.push_str("active SDK state: "_str);
        output.push_str(summary.active_issue->as_str());
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
    request.tool_reporter  = Some(lito::tools::HostToolResolutionSink {
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

auto build_configuration(lito::config::ToolchainSpec            toolchain,
                         lito::config::StandardLibrarySelection standard_library,
                         lito::config::StandardLibraryRuntime   standard_library_runtime,
                         lito::config::ProjectBuildOptions      options,
                         lito::config::BuildTargetRequest       target)
    -> lito::config::BuildConfigurationRequest {
    return lito::config::BuildConfigurationRequest {
        .toolchain                = rstd::move(toolchain),
        .standard_library         = standard_library,
        .standard_library_runtime = standard_library_runtime,
        .bmi_mode                 = lito::cpp::BmiMode::Reduced,
        .language_standard        = String::make("c++20"_str),
        .global_options           = rstd::move(options),
        .target                   = rstd::move(target),
    };
}

struct ArtifactCounts {
    usize archives {};
    usize shared_libraries {};
    usize executables {};
    usize tests {};
    usize benchmarks {};
    usize compile_tests {};
};

auto artifact_counts(const lito::BuildSummary& summary) -> ArtifactCounts {
    auto counts = ArtifactCounts {};
    for (const auto& artifact : summary.product.artifacts) {
        switch (artifact.kind) {
        case lito::cpp::ArtifactKind::StaticLibrary: ++counts.archives; break;
        case lito::cpp::ArtifactKind::SharedLibrary: ++counts.shared_libraries; break;
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
                          bool&                              cargo_offline,
                          ref<rstd::path::Path>              root,
                          bool                               offline,
                          bool                               frozen,
                          Vec<rstd::path::PathBuf>           bundles) {
    if (offline || frozen) {
        sources.network = lito::source::NetworkPolicy::Offline;
        cargo_offline   = true;
    }
    for (auto& bundle : bundles) {
        if (bundle.as_path().is_relative()) {
            bundle = rstd::path::PathBuf::from(root).join(bundle.as_path());
        }
        sources.source_bundles.push(rstd::move(bundle));
    }
}

struct InspectorInputError {
    String message;
};

auto read_inspector_standard_input() -> Result<String, InspectorInputError> {
    constexpr auto maximum_size = usize(1024 * 1024);
    auto           input        = rstd::io::stdin();
    auto           bytes        = Vec<u8>::make();
    auto           buffer       = array<u8, 16384> {};
    while (true) {
        auto read = as<rstd::io::Read>(input).read(buffer.as_mut_slice());
        if (read.is_err()) {
            return Err(InspectorInputError {
                .message = rstd::format("cannot read inspection request from stdin: {}",
                                        rstd::move(read).unwrap_err()),
            });
        }
        if (*read == usize {}) break;
        if (bytes.len() > maximum_size - *read) {
            return Err(InspectorInputError {
                .message = String::make("inspection request exceeds 1 MiB"_str),
            });
        }
        bytes.extend_from_slice(slice<u8>::from_raw_parts(buffer.as_ptr(), *read));
    }
    auto text = String::from_utf8(rstd::move(bytes));
    if (text.is_err()) {
        return Err(InspectorInputError {
            .message = String::make("inspection request is not UTF-8"_str),
        });
    }
    return Ok(rstd::move(text).unwrap());
}

auto read_inspector_request(ref<str> source, ref<rstd::path::Path> root)
    -> Result<String, InspectorInputError> {
    if (source == "-"_str) return read_inspector_standard_input();
    auto path = rstd::path::PathBuf::from(source);
    if (path.as_path().is_relative()) path = rstd::path::PathBuf::from(root).join(path.as_path());
    auto text = rstd::fs::read_to_string(path.as_path());
    if (text.is_err()) {
        return Err(InspectorInputError {
            .message = rstd::format("cannot read inspection request '{}': {}",
                                    path.as_path(),
                                    rstd::move(text).unwrap_err()),
        });
    }
    if (text->len() > usize(1024 * 1024)) {
        return Err(InspectorInputError {
            .message = String::make("inspection request exceeds 1 MiB"_str),
        });
    }
    return Ok(rstd::move(text).unwrap());
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
    if (invocation.command.is_Registry()) {
        auto command = rstd::move(invocation.command).as_Registry().command;
        auto options = rstd::move(command).as_Inspect().options;
        if (options.capabilities) {
            rstd::io::println("{}", lito::registry::registry_inspector_capabilities_json());
            return 0;
        }
        if (options.protocol->as_str() != lito::registry::REGISTRY_INSPECTION_PROTOCOL) {
            rstd::io::eprintln("lito: unsupported Registry inspector protocol '{}': expected '{}'",
                               options.protocol->as_str(),
                               lito::registry::REGISTRY_INSPECTION_PROTOCOL);
            return 1;
        }
        auto request_text = read_inspector_request(options.request_json->as_str(),
                                                   invocation.working_directory.as_path());
        if (request_text.is_err()) {
            rstd::io::eprintln("lito: {}", rstd::move(request_text).unwrap_err().message);
            return 1;
        }
        auto request =
            lito::registry::parse_registry_inspection_request(request_text->as_str().as_bytes());
        if (request.is_err()) {
            rstd::io::eprintln("lito: {}", rstd::move(request).unwrap_err().message);
            return 1;
        }
        auto archive = rstd::move(*options.archive);
        if (archive.as_path().is_relative()) {
            archive = invocation.working_directory.join(archive.as_path());
        }
        auto verified = lito::registry::verify_registry_blob_file(
            rstd::move(archive), request->package, request->blob);
        if (verified.is_err()) {
            rstd::io::eprintln("lito: {}", rstd::move(verified).unwrap_err().message);
            return 1;
        }
        auto inspected = lito::registry::PackageArchiveInspector::inspect_candidate(
            *verified, request->package, request->version, request->limits);
        if (inspected.is_err()) {
            rstd::io::eprintln("lito: {}", rstd::move(inspected).unwrap_err().message);
            return 1;
        }
        rstd::io::println("{}", lito::registry::serialize_verified_publish_candidate(*inspected));
        return 0;
    }
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
    if (invocation.command.is_Clean()) {
        auto options = rstd::move(invocation.command).as_Clean().options;
        auto target  = lito::CleanTarget::All();
        if (options.profile.is_some()) {
            target = lito::CleanTarget::Profile(rstd::move(options.profile).unwrap());
        } else if (options.build_directory.is_some()) {
            target = lito::CleanTarget::Directory(rstd::move(options.build_directory).unwrap());
        }
        auto result = lito::clean(lito::CleanRequest {
            .root   = invocation.working_directory.clone(),
            .target = rstd::move(target),
        });
        if (result.is_err()) {
            report_error(rstd::move(result).unwrap_err());
            return 1;
        }
        auto summary = rstd::move(result).unwrap();
        if (summary.removed) {
            rstd::io::println("removed {}", summary.path.as_path());
        } else {
            rstd::io::println("nothing to clean at {}", summary.path.as_path());
        }
        return 0;
    }
    if (invocation.command.is_Sdk()) {
        auto       sdk_command = rstd::move(invocation.command).as_Sdk().command;
        const auto android     = sdk_command.is_AndroidNdk();
        auto       command     = android ? rstd::move(sdk_command).as_AndroidNdk().command
                                         : rstd::move(sdk_command).as_Llvm().command;
        const auto sdk_name    = android ? "Android NDK"_str : "LLVM SDK"_str;
        if (command.is_List()) {
            auto result = android ? lito::list_android_ndks() : lito::list_llvm_sdks();
            if (result.is_err()) {
                auto error = rstd::move(result).unwrap_err();
                report_error(error);
                return 1;
            }
            render_sdk_list(*result, sdk_name);
            return 0;
        }
        if (command.is_Activate()) {
            auto options = rstd::move(command).as_Activate().options;
            auto request = lito::SdkActivateRequest { .version = rstd::move(options.version) };
            auto result  = android ? lito::activate_android_ndk(rstd::move(request))
                                   : lito::activate_llvm_sdk(rstd::move(request));
            if (result.is_err()) {
                auto error = rstd::move(result).unwrap_err();
                report_error(error);
                return 1;
            }
            auto summary = rstd::move(result).unwrap();
            if (summary.unchanged) {
                rstd::io::println("{} {} for {} is already active at {}",
                                  sdk_name,
                                  summary.version,
                                  summary.host,
                                  summary.prefix.as_path());
            } else {
                rstd::io::println("activated {} {} for {} at {}",
                                  sdk_name,
                                  summary.version,
                                  summary.host,
                                  summary.prefix.as_path());
            }
            return 0;
        }
        if (command.is_Deactivate()) {
            auto result = android ? lito::deactivate_android_ndk() : lito::deactivate_llvm_sdk();
            if (result.is_err()) {
                auto error = rstd::move(result).unwrap_err();
                report_error(error);
                return 1;
            }
            auto summary = rstd::move(result).unwrap();
            if (summary.unchanged) {
                rstd::io::println("no {} is active", sdk_name);
            } else if (summary.invalid_state) {
                rstd::io::println("cleared invalid {} activation state", sdk_name);
            } else {
                rstd::io::println(
                    "deactivated {} {} for {}", sdk_name, *summary.version, *summary.host);
            }
            return 0;
        }
        if (command.is_Uninstall()) {
            auto options = rstd::move(command).as_Uninstall().options;
            auto request = lito::SdkUninstallRequest { .version = rstd::move(options.version) };
            auto result  = android ? lito::uninstall_android_ndk(rstd::move(request))
                                   : lito::uninstall_llvm_sdk(rstd::move(request));
            if (result.is_err()) {
                auto error = rstd::move(result).unwrap_err();
                report_error(error);
                return 1;
            }
            auto summary = rstd::move(result).unwrap();
            auto status  = summary.was_active ? "uninstalled active"_str : "uninstalled"_str;
            if (summary.recovered) {
                rstd::io::println("completed interrupted {} {} removal from {}",
                                  sdk_name,
                                  summary.version,
                                  summary.prefix.as_path());
            } else if (summary.invalid_entry) {
                rstd::io::println("{} invalid {} {} from {}",
                                  status,
                                  sdk_name,
                                  summary.version,
                                  summary.prefix.as_path());
            } else {
                rstd::io::println("{} {} {} for {} from {}",
                                  status,
                                  sdk_name,
                                  summary.version,
                                  *summary.host,
                                  summary.prefix.as_path());
            }
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
        auto reporter      = Some(lito::tools::HostToolResolutionSink {
            .context = rstd::addressof(event_context),
            .notify  = report_host_tool_resolution,
        });
        auto observer      = Some(lito::SdkEventSink { .notify = observe_sdk });
        auto result        = android ? lito::install_android_ndk(lito::AndroidNdkInstallRequest {
                                           .version        = rstd::move(options.version),
                                           .accept_license = options.accept_license,
                                           .environment    = rstd::move(config.environment),
                                           .tools          = rstd::move(config.tools),
                                           .tool_reporter  = rstd::move(reporter),
                                           .observer       = rstd::move(observer),
                                       })
                                     : lito::install_llvm_sdk(lito::SdkInstallRequest {
                                           .version       = rstd::move(options.version),
                                           .environment   = rstd::move(config.environment),
                                           .tools         = rstd::move(config.tools),
                                           .toolchain     = rstd::move(config.toolchain),
                                           .tool_reporter = rstd::move(reporter),
                                           .observer      = rstd::move(observer),
                                       });
        if (result.is_err()) {
            auto error = rstd::move(result).unwrap_err();
            report_error(error);
            return 1;
        }
        auto summary = rstd::move(result).unwrap();
        rstd::io::println("{} {} {} for {} at {}",
                          summary.reused ? "reused"_str : "installed"_str,
                          sdk_name,
                          summary.version,
                          summary.host,
                          summary.prefix.as_path());
        return 0;
    }
    if (invocation.command.is_Add()) {
        auto options = rstd::move(invocation.command).as_Add().options;
        auto spec    = lito::registry::RegistryPackageSpec::parse(options.source.as_str());
        if (spec.is_err()) {
            rstd::io::eprintln("lito: invalid Registry dependency '{}': {}",
                               options.source.as_str(),
                               rstd::move(spec).unwrap_err());
            return 1;
        }
        if (! spec->selector.is_Requirement()) {
            rstd::io::eprintln(
                "lito: Registry tags are only accepted by install; add requires a version range");
            return 1;
        }
        if (options.registry.is_some()) {
            auto bootstrap = lito::config::load_registry_bootstrap_config(
                lito::config::RegistryBootstrapConfigRequest {
                    .mode = invocation.no_config ? lito::config::ConfigLoadMode::LocalDisabled
                                                 : lito::config::ConfigLoadMode::Enabled,
                });
            if (bootstrap.is_err()) {
                report_error(rstd::move(bootstrap).unwrap_err());
                return 1;
            }
            if (bootstrap->registry(options.registry->as_str()).is_none()) {
                rstd::io::eprintln("lito: Registry '{}' is not configured",
                                   options.registry->as_str());
                return 1;
            }
        }
        auto edited =
            lito::manifest::add_registry_dependency(invocation.working_directory.as_path(),
                                                    spec->package,
                                                    spec->selector.as_Requirement().requirement,
                                                    rstd::move(options.registry));
        if (edited.is_err()) {
            rstd::io::eprintln("lito: {}", rstd::move(edited).unwrap_err());
            return 1;
        }
        rstd::io::println("added {} to {}", edited->package, edited->path.as_path());
        return 0;
    }
    if (invocation.command.is_Pack()) {
        auto options   = rstd::move(invocation.command).as_Pack().options;
        auto bootstrap = lito::config::load_registry_bootstrap_config(
            lito::config::RegistryBootstrapConfigRequest {
                .mode = invocation.no_config ? lito::config::ConfigLoadMode::LocalDisabled
                                             : lito::config::ConfigLoadMode::Enabled,
            });
        if (bootstrap.is_err()) {
            auto error = rstd::move(bootstrap).unwrap_err();
            report_error(error);
            return 1;
        }
        auto registries = rstd::move(bootstrap).unwrap();
        auto selected = options.registry.is_some() ? registries.registry(options.registry->as_str())
                                                   : registries.default_registry();
        if (selected.is_none()) {
            rstd::io::eprintln("lito: {} Registry is not configured",
                               options.registry.is_some() ? options.registry->as_str()
                                                          : "default"_str);
            return 1;
        }
        auto aliases = Vec<lito::manifest::StandaloneRegistryAlias>::make();
        for (const auto& registry : *registries.registries()) {
            aliases.push(lito::manifest::StandaloneRegistryAlias {
                .name     = registry.name.clone(),
                .identity = registry.identity.clone(),
            });
        }
        auto result = lito::pack_package(lito::PackPackageRequest {
            .root    = invocation.working_directory.clone(),
            .package = rstd::move(options.package),
            .output  = rstd::move(options.output),
            .list    = options.list,
            .registry =
                lito::PackageRegistryContext {
                    .owner   = (**selected).identity.clone(),
                    .aliases = rstd::move(aliases),
                },
        });
        if (result.is_err()) {
            auto error = rstd::move(result).unwrap_err();
            report_error(error);
            return 1;
        }
        auto summary = rstd::move(result).unwrap();
        if (options.list) {
            for (const auto& path : summary.files) rstd::io::println("{}", path.as_str());
            return 0;
        }
        const auto& artifact = *summary.artifact;
        rstd::io::println("packed {} {} to {}",
                          summary.package.name.as_str(),
                          summary.version.text(),
                          summary.output.as_path());
        rstd::io::println("blob={} source={} manifest={} files={}",
                          artifact.blob.digest.text(),
                          artifact.candidate.source_digest.text(),
                          artifact.candidate.manifest_digest.text(),
                          summary.files.len());
        return 0;
    }
    if (invocation.command.is_Publish()) {
        auto options   = rstd::move(invocation.command).as_Publish().options;
        auto bootstrap = lito::config::load_registry_bootstrap_config(
            lito::config::RegistryBootstrapConfigRequest {
                .mode = invocation.no_config ? lito::config::ConfigLoadMode::LocalDisabled
                                             : lito::config::ConfigLoadMode::Enabled,
            });
        if (bootstrap.is_err()) {
            report_error(rstd::move(bootstrap).unwrap_err());
            return 1;
        }
        auto registries = rstd::move(bootstrap).unwrap();
        auto selected = options.registry.is_some() ? registries.registry(options.registry->as_str())
                                                   : registries.default_registry();
        if (selected.is_none()) {
            rstd::io::eprintln("lito: {} Registry is not configured",
                               options.registry.is_some() ? options.registry->as_str()
                                                          : "default"_str);
            return 1;
        }
        auto credentials = lito::config::load_registry_credentials();
        if (credentials.is_err()) {
            report_error(rstd::move(credentials).unwrap_err());
            return 1;
        }
        auto credential_store = rstd::move(credentials).unwrap();
        auto token            = credential_store.token((**selected).name.as_str());
        if (token.is_none()) {
            rstd::io::eprintln("lito: Registry '{}' has no publish credential",
                               (**selected).name.as_str());
            return 1;
        }
        auto temporary = rstd::fs::TempDir::make("lito-publish"_str);
        if (temporary.is_err()) {
            rstd::io::eprintln("lito: cannot create publish staging directory: {}",
                               rstd::move(temporary).unwrap_err());
            return 1;
        }
        auto staging = rstd::move(temporary).unwrap();
        auto output  = rstd::path::PathBuf::from(staging.path())
                           .join(rstd::path::PathBuf::from("package.tar.zst"_str).as_path());
        auto aliases = Vec<lito::manifest::StandaloneRegistryAlias>::make();
        for (const auto& registry : *registries.registries()) {
            aliases.push(lito::manifest::StandaloneRegistryAlias {
                .name     = registry.name.clone(),
                .identity = registry.identity.clone(),
            });
        }
        auto packed = lito::pack_package(lito::PackPackageRequest {
            .root    = invocation.working_directory.clone(),
            .package = rstd::move(options.package),
            .output  = Some(rstd::move(output)),
            .registry =
                lito::PackageRegistryContext {
                    .owner   = (**selected).identity.clone(),
                    .aliases = rstd::move(aliases),
                },
        });
        if (packed.is_err()) {
            report_error(rstd::move(packed).unwrap_err());
            return 1;
        }
        auto package        = rstd::move(packed).unwrap();
        auto command_config = lito::config::load_host_tool_command_config(
            invocation.working_directory.as_path(),
            lito::config::ProjectConfigRequest {
                .mode = invocation.no_config ? lito::config::ConfigLoadMode::LocalDisabled
                                             : lito::config::ConfigLoadMode::Enabled,
            });
        if (command_config.is_err()) {
            report_error(rstd::move(command_config).unwrap_err());
            return 1;
        }
        auto host        = rstd::move(command_config).unwrap();
        auto environment = lito::system::ResolvedProcessEnvironment::resolve(host.environment);
        if (environment.is_err()) {
            report_error(rstd::move(environment).unwrap_err());
            return 1;
        }
        auto event_context = EventContext {};
        auto resolver = lito::tools::ToolResolver(*environment,
                                                  rstd::move(host.tools),
                                                  Some(lito::tools::HostToolResolutionSink {
                                                      .context = rstd::addressof(event_context),
                                                      .notify  = report_host_tool_resolution,
                                                  }));
        auto curl =
            resolver.require(lito::tools::Tool::Curl,
                             lito::tools::command_tool_requirement(
                                 lito::tools::HostToolCapability::RegistryPublish, "publish"_str));
        if (curl.is_err()) {
            report_error(rstd::move(curl).unwrap_err());
            return 1;
        }
        auto transport =
            lito::registry::CurlRegistryPublishTransport(curl->executable.clone(), *environment);
        auto client    = lito::registry::RegistryPublishClient(transport.transport());
        auto published = client.publish(lito::registry::RegistryPublishRequest {
            .api     = (**selected).api.clone(),
            .token   = rstd::addressof(**token),
            .package = package.package.clone(),
            .version = package.version.clone(),
            .blob    = package.artifact->blob.clone(),
            .archive = package.output.clone(),
        });
        if (published.is_err()) {
            auto error = rstd::move(published).unwrap_err();
            rstd::io::eprintln("lito: {}", error.message.as_str());
            return 1;
        }
        auto session = rstd::move(published).unwrap();
        rstd::io::println("published {} {} as {}",
                          session.package.name.as_str(),
                          session.version.text(),
                          session.release->text());
        return 0;
    }
    auto       registry_bootstrap = Option<lito::config::LitoBootstrapConfig> {};
    auto       install_source     = Option<lito::ResolvedInstallSource> {};
    const auto reuse_install =
        invocation.command.is_Install() && invocation.command.as_Install().options.no_build;
    if (invocation.command.is_Install()) {
        const auto& options = invocation.command.as_Install().options;
        if (options.source.is_some()) {
            auto spec = lito::registry::RegistryPackageSpec::parse(options.source->as_str());
            if (spec.is_err()) {
                rstd::io::eprintln("lito: invalid Registry package '{}': {}",
                                   options.source->as_str(),
                                   rstd::move(spec).unwrap_err());
                return 1;
            }
            auto bootstrap = lito::config::load_registry_bootstrap_config(
                lito::config::RegistryBootstrapConfigRequest {
                    .mode = invocation.no_config ? lito::config::ConfigLoadMode::LocalDisabled
                                                 : lito::config::ConfigLoadMode::Enabled,
                });
            if (bootstrap.is_err()) {
                report_error(rstd::move(bootstrap).unwrap_err());
                return 1;
            }
            registry_bootstrap = Some(rstd::move(bootstrap).unwrap());
            auto host          = lito::config::load_host_tool_command_config(
                invocation.working_directory.as_path(),
                lito::config::ProjectConfigRequest {
                    .mode = invocation.no_config ? lito::config::ConfigLoadMode::LocalDisabled
                                                 : lito::config::ConfigLoadMode::Enabled,
                });
            if (host.is_err()) {
                report_error(rstd::move(host).unwrap_err());
                return 1;
            }
            auto command_config = rstd::move(host).unwrap();
            auto environment =
                lito::system::ResolvedProcessEnvironment::resolve(command_config.environment);
            if (environment.is_err()) {
                report_error(rstd::move(environment).unwrap_err());
                return 1;
            }
            auto http          = lito::registry::RegistryHttpTransport {};
            auto blobs         = lito::registry::RegistryBlobTransport {};
            auto http_owner    = Option<lito::registry::CurlRegistryHttpTransport> {};
            auto blob_owner    = Option<lito::registry::CurlRegistryBlobTransport> {};
            auto event_context = EventContext {};
            if (! options.offline) {
                auto resolver =
                    lito::tools::ToolResolver(*environment,
                                              rstd::move(command_config.tools),
                                              Some(lito::tools::HostToolResolutionSink {
                                                  .context = rstd::addressof(event_context),
                                                  .notify  = report_host_tool_resolution,
                                              }));
                auto curl = resolver.require(
                    lito::tools::Tool::Curl,
                    lito::tools::command_tool_requirement(
                        lito::tools::HostToolCapability::HttpDownload, "Registry install"_str));
                if (curl.is_err()) {
                    report_error(rstd::move(curl).unwrap_err());
                    return 1;
                }
                http_owner = Some(lito::registry::CurlRegistryHttpTransport(
                    curl->executable.clone(), *environment));
                blob_owner = Some(lito::registry::CurlRegistryBlobTransport(
                    curl->executable.clone(), *environment));
                http       = http_owner->transport();
                blobs      = blob_owner->transport();
            }
            auto data = lito::system::LitoDataRoot::resolve();
            if (data.is_err()) {
                report_error(rstd::move(data).unwrap_err());
                return 1;
            }
            auto registry_client = lito::registry::RegistryGraphClient(
                rstd::path::PathBuf::from(data->root()),
                *registry_bootstrap,
                options.offline ? lito::registry::RegistryNetworkPolicy::Offline
                                : lito::registry::RegistryNetworkPolicy::Online,
                http,
                blobs,
                false);
            auto graph = registry_client.resolve_package(
                *spec,
                options.registry.is_some() ? Some(options.registry->clone()) : Option<String> {},
                "install package argument"_str);
            if (graph.is_err()) {
                rstd::io::eprintln("lito: {}", rstd::move(graph).unwrap_err().message.as_str());
                return 1;
            }
            auto resolved = lito::resolve_registry_install_source(
                spec->package, rstd::move(graph).unwrap(), data->root());
            if (resolved.is_err()) {
                report_error(rstd::move(resolved).unwrap_err());
                return 1;
            }
            install_source = Some(rstd::move(resolved).unwrap());
        } else {
            auto resolved = lito::resolve_install_source(
                lito::InstallSourceRequirement::LocalProject(invocation.working_directory.clone()));
            if (resolved.is_err()) {
                auto error = rstd::move(resolved).unwrap_err();
                report_error(error);
                return 1;
            }
            install_source = Some(rstd::move(resolved).unwrap());
        }
    }
    auto config_root = install_source.is_some() ? install_source->project.root.as_path()
                                                : invocation.working_directory.as_path();
    auto active_sdk  = Option<lito::ActiveSdkLease> {};
    if (! reuse_install) {
        auto active_sdk_result = lito::acquire_active_llvm_sdk();
        if (active_sdk_result.is_err()) {
            auto error = rstd::move(active_sdk_result).unwrap_err();
            report_error(error);
            return 1;
        }
        active_sdk = rstd::move(active_sdk_result).unwrap();
    }
    auto defaults = Option<lito::config::ProjectConfigDefaults> {};
    if (active_sdk.is_some()) defaults = Some(active_sdk->project_defaults());
    auto loaded_config = lito::config::load_project_config(
        config_root,
        lito::config::ProjectConfigRequest {
            .mode              = invocation.no_config ? lito::config::ConfigLoadMode::LocalDisabled
                                                      : lito::config::ConfigLoadMode::Enabled,
            .overrides         = rstd::move(invocation.config_overrides),
            .environment_flags = invocation.use_env_flags && ! reuse_install
                                     ? lito::config::EnvironmentFlagPolicy::Append
                                     : lito::config::EnvironmentFlagPolicy::Ignore,
            .defaults          = rstd::move(defaults),
        });
    if (loaded_config.is_err()) {
        auto error = rstd::move(loaded_config).unwrap_err();
        report_error(error);
        return 1;
    }
    auto       project        = rstd::move(loaded_config).unwrap();
    auto       active_android = Option<lito::AndroidNdkLease> {};
    const auto build_command  = invocation.command.is_Build() ||
                                (invocation.command.is_Install() && ! reuse_install) ||
                                invocation.command.is_Test() || invocation.command.is_Bench() ||
                                invocation.command.is_Doc() || invocation.command.is_Scan() ||
                                invocation.command.is_Fetch();
    if (build_command && project.build_target.is_Android() && project.toolchain.sdk.is_none()) {
        auto active_android_result = lito::acquire_active_android_ndk();
        if (active_android_result.is_err()) {
            auto error = rstd::move(active_android_result).unwrap_err();
            report_error(error);
            return 1;
        }
        active_android = rstd::move(active_android_result).unwrap();
        if (active_android.is_some()) {
            project.toolchain.sdk = active_android->project_defaults().toolchain.sdk;
        }
    }
    if ((build_command || invocation.command.is_Update() || invocation.command.is_Install()) &&
        registry_bootstrap.is_none()) {
        auto loaded = lito::config::load_registry_bootstrap_config(
            lito::config::RegistryBootstrapConfigRequest {
                .mode = invocation.no_config ? lito::config::ConfigLoadMode::LocalDisabled
                                             : lito::config::ConfigLoadMode::Enabled,
            });
        if (loaded.is_err()) {
            report_error(rstd::move(loaded).unwrap_err());
            return 1;
        }
        registry_bootstrap = Some(rstd::move(loaded).unwrap());
    }

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
                             project.cargo.offline,
                             project.root.as_path(),
                             options.offline || options.no_build,
                             options.frozen || options.no_build,
                             rstd::move(options.source_bundles));
        auto destination = lito::resolve_install_destination(invocation.working_directory.as_path(),
                                                             rstd::move(options.destination),
                                                             project.install);
        if (destination.is_err()) {
            auto error = rstd::move(destination).unwrap_err();
            report_error(error);
            return 1;
        }
        auto timing             = make_timing_output(project.root.as_path(),
                                                     rstd::move(options.timing_file),
                                                     options.verbose && ! options.no_timing);
        auto managed_build_root = install_source->managed_build_root.is_some()
                                      ? Some(install_source->managed_build_root->clone())
                                      : Option<rstd::path::PathBuf> {};
        auto request            = lito::InstallRequest {
            .source      = rstd::move(install_source).unwrap(),
            .destination = rstd::move(destination).unwrap(),
        };
        request.binaries             = rstd::move(options.binaries);
        request.build_mode           = options.no_build ? lito::InstallBuildMode::ReuseCompleted
                                                        : lito::InstallBuildMode::Build;
        request.force                = options.force;
        request.build.selection.root = project.root.clone();
        request.build.environment    = rstd::move(project.environment);
        request.build.tools          = rstd::move(project.tools);
        request.build.registries     = rstd::move(registry_bootstrap);
        request.build.configuration  = build_configuration(rstd::move(project.toolchain),
                                                           project.standard_library,
                                                           project.standard_library_runtime,
                                                           rstd::move(project.build_options),
                                                           rstd::move(project.build_target));
        request.build.lock           = rstd::move(project.lock);
        request.build.sources        = rstd::move(project.sources);
        request.build.cargo          = project.cargo;
        request.build.pkg_config     = rstd::move(project.pkg_config);
        request.build.cmake          = rstd::move(project.cmake);
        request.build.cmake_build_overrides = rstd::move(project.cmake_build_overrides);
        request.build.selection.packages    = rstd::move(options.packages);
        request.build.selection.features    = rstd::move(options.features);
        request.build.locked                = options.locked || options.frozen || options.no_build;
        if (options.profile.is_some()) request.build.profile = Some(options.profile->clone());
        if (managed_build_root.is_some()) {
            auto created = rstd::fs::create_dir_all(managed_build_root->as_path());
            if (created.is_err()) {
                rstd::io::eprintln("lito: cannot create Registry build cache '{}': {}",
                                   managed_build_root->as_path(),
                                   rstd::move(created).unwrap_err());
                return 1;
            }
            request.build.lock.path =
                managed_build_root->join(rstd::path::PathBuf::from("lito.lock"_str).as_path());
        }
        if (options.build_directory.is_some()) {
            request.build.build_directory =
                options.build_directory->as_path().is_absolute() || managed_build_root.is_none()
                    ? rstd::move(*options.build_directory)
                    : invocation.working_directory.join(options.build_directory->as_path());
        } else if (managed_build_root.is_some()) {
            auto profile = options.profile.is_some() ? options.profile->as_str() : "release"_str;
            request.build.build_directory =
                managed_build_root->join(rstd::path::PathBuf::from(profile).as_path());
        }
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
        if (summary.build.is_Built()) {
            auto emitted = lito::timing_output::emit(summary.build.as_Built().summary, timing);
            if (emitted.is_err()) {
                auto error = rstd::move(emitted).unwrap_err();
                report_error(error);
                return 1;
            }
        } else if (options.verbose) {
            const auto& product = summary.build.as_Reused().product;
            rstd::io::println("[reuse] build product {} {}",
                              product.generation.as_str(),
                              product.build_directory.as_path());
        }
        rstd::io::println("installed {} entries from {} packages ({}{}) to {} {}",
                          summary.entries.len(),
                          summary.packages.len(),
                          summary.build.profile(),
                          summary.build.is_Reused() ? ", reused build"_str : ""_str,
                          summary.destination.is_Managed() ? "managed root"_str
                                                           : "untracked prefix"_str,
                          summary.destination.path());
        return 0;
    }

    if (invocation.command.is_Update()) {
        auto options = rstd::move(invocation.command).as_Update().options;
        apply_source_options(project.sources,
                             project.cargo.offline,
                             project.root.as_path(),
                             options.offline,
                             false,
                             rstd::move(options.source_bundles));
        auto event_context = EventContext {};
        auto request       = lito::UpdateRequest {
            .root          = rstd::move(project.root),
            .environment   = rstd::move(project.environment),
            .tools         = rstd::move(project.tools),
            .registries    = rstd::move(registry_bootstrap),
            .lock          = rstd::move(project.lock),
            .sources       = rstd::move(project.sources),
            .observer      = Some(lito::BuildEventSink {
                .context = rstd::addressof(event_context),
                .notify  = observe,
            }),
            .tool_reporter = Some(lito::tools::HostToolResolutionSink {
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

    if (invocation.command.is_Fetch()) {
        auto options = rstd::move(invocation.command).as_Fetch().options;
        apply_source_options(project.sources,
                             project.cargo.offline,
                             project.root.as_path(),
                             options.offline,
                             options.frozen,
                             {});
        project.sources.source_bundles.clear();
        auto destination = lito::FetchDestination::GlobalCache();
        if (options.output.is_some()) {
            auto output = rstd::move(options.output).unwrap();
            if (output.as_path().is_relative()) output = project.root.join(output.as_path());
            destination = lito::FetchDestination::SourceBundle(rstd::move(output));
        }
        auto jobs = usize(1);
        if (options.jobs.is_some()) {
            jobs = *options.jobs;
        } else {
            auto available = rstd::thread::available_parallelism();
            if (available.is_ok()) jobs = available->get();
        }
        auto event_context = EventContext {};
        auto request       = lito::FetchRequest {
            .selection =
                lito::package::PackageSelection {
                    .root     = project.root.clone(),
                    .features = rstd::move(options.features),
                },
            .environment           = rstd::move(project.environment),
            .tools                 = rstd::move(project.tools),
            .registries            = rstd::move(registry_bootstrap),
            .configuration         = build_configuration(rstd::move(project.toolchain),
                                                         project.standard_library,
                                                         project.standard_library_runtime,
                                                         rstd::move(project.build_options),
                                                         rstd::move(project.build_target)),
            .lock                  = rstd::move(project.lock),
            .sources               = rstd::move(project.sources),
            .cargo                 = rstd::move(project.cargo),
            .cmake_build_overrides = rstd::move(project.cmake_build_overrides),
            .destination           = rstd::move(destination),
            .locked                = options.locked || options.frozen,
            .jobs                  = jobs,
            .observer              = Some(lito::BuildEventSink {
                .context = rstd::addressof(event_context),
                .notify  = observe,
            }),
            .tool_reporter         = Some(lito::tools::HostToolResolutionSink {
                .context = rstd::addressof(event_context),
                .notify  = report_host_tool_resolution,
            }),
        };
        auto result = lito::fetch_dependencies(request);
        if (result.is_err()) {
            report_error(rstd::move(result).unwrap_err());
            return 1;
        }
        auto summary = rstd::move(result).unwrap();
        if (summary.destination.is_SourceBundle()) {
            rstd::io::println("fetched {} source entries to {}",
                              summary.entries,
                              summary.destination.as_SourceBundle().path.as_path());
        } else {
            rstd::io::println("fetched {} source entries", summary.entries);
        }
        if (summary.lock == lito::lock::LockStatus::Updated) {
            rstd::io::println("updated {}", request.lock.path.as_path());
        }
        return 0;
    }

    if (invocation.command.is_Format()) {
        auto options          = rstd::move(invocation.command).as_Format().options;
        auto request          = lito::FormatRequest {};
        request.root          = rstd::move(project.root);
        request.environment   = rstd::move(project.environment);
        request.tools         = rstd::move(project.tools);
        request.packages      = rstd::move(options.packages);
        request.mode          = options.check ? lito::FormatMode::Check : lito::FormatMode::Write;
        auto event_context    = EventContext {};
        request.tool_reporter = Some(lito::tools::HostToolResolutionSink {
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
                             project.cargo.offline,
                             project.root.as_path(),
                             options.offline,
                             options.frozen,
                             rstd::move(options.source_bundles));
        auto request                  = lito::ScanRequest {};
        request.selection.root        = rstd::move(project.root);
        request.environment           = rstd::move(project.environment);
        request.tools                 = rstd::move(project.tools);
        request.registries            = rstd::move(registry_bootstrap);
        request.configuration         = build_configuration(rstd::move(project.toolchain),
                                                            project.standard_library,
                                                            project.standard_library_runtime,
                                                            rstd::move(project.build_options),
                                                            rstd::move(project.build_target));
        request.lock                  = rstd::move(project.lock);
        request.sources               = rstd::move(project.sources);
        request.cargo                 = project.cargo;
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
        request.tool_reporter  = Some(lito::tools::HostToolResolutionSink {
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
                             project.cargo.offline,
                             project.root.as_path(),
                             options.offline,
                             options.frozen,
                             rstd::move(options.source_bundles));
        auto timing                  = make_timing_output(project.root.as_path(),
                                                          rstd::move(options.timing_file),
                                                          options.verbose && ! options.no_timing);
        auto request                 = lito::DocRequest {};
        request.build.selection.root = project.root.clone();
        request.build.environment    = rstd::move(project.environment);
        request.build.tools          = rstd::move(project.tools);
        request.build.registries     = rstd::move(registry_bootstrap);
        request.build.configuration  = build_configuration(rstd::move(project.toolchain),
                                                           project.standard_library,
                                                           project.standard_library_runtime,
                                                           rstd::move(project.build_options),
                                                           rstd::move(project.build_target));
        request.build.lock           = rstd::move(project.lock);
        request.build.sources        = rstd::move(project.sources);
        request.build.cargo          = project.cargo;
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
                             project.cargo.offline,
                             project.root.as_path(),
                             options.offline,
                             options.frozen,
                             rstd::move(options.source_bundles));
        auto timing                  = make_timing_output(project.root.as_path(),
                                                          rstd::move(options.timing_file),
                                                          options.verbose && ! options.no_timing);
        auto request                 = lito::TestRequest {};
        request.build.selection.root = rstd::move(project.root);
        request.build.environment    = rstd::move(project.environment);
        request.build.tools          = rstd::move(project.tools);
        request.build.registries     = rstd::move(registry_bootstrap);
        request.build.configuration  = build_configuration(rstd::move(project.toolchain),
                                                           project.standard_library,
                                                           project.standard_library_runtime,
                                                           rstd::move(project.build_options),
                                                           rstd::move(project.build_target));
        request.build.lock           = rstd::move(project.lock);
        request.build.sources        = rstd::move(project.sources);
        request.build.cargo          = project.cargo;
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
        if (options.build_directory.is_some()) {
            request.build.build_directory = rstd::move(*options.build_directory);
        }
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
                              summary.build.product.profile.as_str(),
                              summary.build.product.build_directory.as_path(),
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
                             project.cargo.offline,
                             project.root.as_path(),
                             options.offline,
                             options.frozen,
                             rstd::move(options.source_bundles));
        auto timing                  = make_timing_output(project.root.as_path(),
                                                          rstd::move(options.timing_file),
                                                          options.verbose && ! options.no_timing);
        auto request                 = lito::BenchRequest {};
        request.build.selection.root = rstd::move(project.root);
        request.build.environment    = rstd::move(project.environment);
        request.build.tools          = rstd::move(project.tools);
        request.build.registries     = rstd::move(registry_bootstrap);
        request.build.configuration  = build_configuration(rstd::move(project.toolchain),
                                                           project.standard_library,
                                                           project.standard_library_runtime,
                                                           rstd::move(project.build_options),
                                                           rstd::move(project.build_target));
        request.build.lock           = rstd::move(project.lock);
        request.build.sources        = rstd::move(project.sources);
        request.build.cargo          = project.cargo;
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
        if (options.build_directory.is_some()) {
            request.build.build_directory = rstd::move(*options.build_directory);
        }
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
                              summary.build.product.profile.as_str(),
                              summary.build.product.build_directory.as_path(),
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
                         project.cargo.offline,
                         project.root.as_path(),
                         options.offline,
                         options.frozen,
                         rstd::move(options.source_bundles));
    auto timing                   = make_timing_output(project.root.as_path(),
                                                       rstd::move(options.timing_file),
                                                       options.verbose && ! options.no_timing);
    auto request                  = lito::BuildRequest {};
    request.selection.root        = rstd::move(project.root);
    request.environment           = rstd::move(project.environment);
    request.tools                 = rstd::move(project.tools);
    request.registries            = rstd::move(registry_bootstrap);
    request.configuration         = build_configuration(rstd::move(project.toolchain),
                                                        project.standard_library,
                                                        project.standard_library_runtime,
                                                        rstd::move(project.build_options),
                                                        rstd::move(project.build_target));
    request.lock                  = rstd::move(project.lock);
    request.sources               = rstd::move(project.sources);
    request.cargo                 = project.cargo;
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
    if (options.build_directory.is_some()) {
        request.build_directory = rstd::move(*options.build_directory);
    }
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
                      "{} archives, {} shared libraries, {} executables, {} tests",
                      summary.package.as_str(),
                      summary.product.profile.as_str(),
                      summary.product.build_directory.as_path(),
                      summary.scanned,
                      summary.compiled,
                      summary.reused,
                      counts.archives,
                      counts.shared_libraries,
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
