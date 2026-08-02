export module tenon.executable;

import rstd;
import tenon;

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

void print_help() {
    rstd::io::println("tenon - module-first C++ builder");
    rstd::io::println("");
    rstd::io::println("Usage: tenon [-C <directory>] build [--package <name>] [--profile "
                      "<debug|release>] [--target <name>] [--out <dir>] [--locked] [--verbose]");
    rstd::io::println("       tenon [-C <directory>] scan <source> [--package <name>] [--profile "
                      "<debug|release>] [--target <name>] [--locked]");
    rstd::io::println("       tenon [-C <directory>] format [--package <name>]");
}

auto missing_value(ref<str> option) -> int {
    rstd::io::eprintln("tenon: {} requires a value", option);
    return 2;
}

auto build_configuration(tenon::ToolchainSpec toolchain) -> tenon::BuildConfiguration {
    return tenon::BuildConfiguration {
        .toolchain         = rstd::move(toolchain),
        .standard_library  = tenon::StandardLibrary::Libcxx,
        .bmi_mode          = tenon::BmiMode::Reduced,
        .language_standard = tenon::String::make("c++20"_str),
    };
}

} // namespace

extern "C++" int main() {
    auto arguments = rstd::env::args();
    (void)arguments.next();
    auto working_directory = tenon::PathBuf::from("."_str);
    auto command           = arguments.next();
    while (command.is_some() && *command == "-C"_str) {
        auto value = arguments.next();
        if (value.is_none()) return missing_value("-C"_str);
        working_directory = tenon::PathBuf::from(rstd::move(value).unwrap());
        command           = arguments.next();
    }
    if (command.is_none() || (*command == "--help"_str) || (*command == "-h"_str)) {
        print_help();
        return 0;
    }
    if (*command != "build"_str && *command != "scan"_str && *command != "format"_str) {
        rstd::io::eprintln("tenon: unknown command '{}'", command->as_str());
        print_help();
        return 2;
    }

    auto loaded_config = tenon::load_project_config(working_directory.as_path());
    if (loaded_config.is_err()) {
        auto error = rstd::move(loaded_config).unwrap_err();
        rstd::io::eprintln("tenon: {}", error.message.as_str());
        return 1;
    }
    auto project = rstd::move(loaded_config).unwrap();

    if (*command == "format"_str) {
        auto request           = tenon::FormatRequest {};
        request.selection.root = rstd::move(project.root);
        request.toolchain      = rstd::move(project.toolchain);
        request.sources        = rstd::move(project.sources);
        while (auto option = arguments.next()) {
            if (*option == "--package"_str) {
                auto value = arguments.next();
                if (value.is_none()) return missing_value("--package"_str);
                request.selection.packages.push(rstd::move(value).unwrap());
            } else if (*option == "--help"_str || *option == "-h"_str) {
                print_help();
                return 0;
            } else {
                rstd::io::eprintln("tenon: unknown format option '{}'", option->as_str());
                return 2;
            }
        }

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

    if (*command == "scan"_str) {
        auto request             = tenon::ScanRequest {};
        request.selection.root   = rstd::move(project.root);
        request.configuration    = build_configuration(rstd::move(project.toolchain));
        request.sources          = rstd::move(project.sources);
        auto source              = Option<tenon::String> {};
        while (auto option = arguments.next()) {
            if (*option == "--profile"_str) {
                auto value = arguments.next();
                if (value.is_none()) return missing_value("--profile"_str);
                auto profile = tenon::parse_build_profile(value->as_str());
                if (profile.is_err()) {
                    auto error = rstd::move(profile).unwrap_err();
                    rstd::io::eprintln("tenon: {}", error.message.as_str());
                    return 2;
                }
                request.configuration.profile = rstd::move(profile).unwrap();
            } else if (*option == "--target"_str) {
                auto value = arguments.next();
                if (value.is_none()) return missing_value("--target"_str);
                request.targets.push(rstd::move(value).unwrap());
            } else if (*option == "--package"_str) {
                auto value = arguments.next();
                if (value.is_none()) return missing_value("--package"_str);
                request.selection.packages.push(rstd::move(value).unwrap());
            } else if (*option == "--locked"_str) {
                request.locked = true;
            } else if (*option == "--help"_str || *option == "-h"_str) {
                print_help();
                return 0;
            } else if (option->as_str().starts_with("-"_str)) {
                rstd::io::eprintln("tenon: unknown scan option '{}'", option->as_str());
                return 2;
            } else if (source.is_some()) {
                rstd::io::eprintln("tenon: scan accepts exactly one source");
                return 2;
            } else {
                source = Some(rstd::move(option).unwrap());
            }
        }
        if (source.is_none()) return missing_value("scan <source>"_str);
        request.source = tenon::PathBuf::from(rstd::move(source).unwrap());

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

    auto request                            = tenon::BuildRequest {};
    request.selection.root                  = rstd::move(project.root);
    request.configuration                   = build_configuration(rstd::move(project.toolchain));
    request.sources                         = rstd::move(project.sources);
    auto event_context                      = EventContext {};
    while (auto option = arguments.next()) {
        if (*option == "--profile"_str) {
            auto value = arguments.next();
            if (value.is_none()) return missing_value("--profile"_str);
            auto profile = tenon::parse_build_profile(value->as_str());
            if (profile.is_err()) {
                auto error = rstd::move(profile).unwrap_err();
                rstd::io::eprintln("tenon: {}", error.message.as_str());
                return 2;
            }
            request.configuration.profile = rstd::move(profile).unwrap();
        } else if (*option == "--target"_str) {
            auto value = arguments.next();
            if (value.is_none()) return missing_value("--target"_str);
            request.targets.push(rstd::move(value).unwrap());
        } else if (*option == "--package"_str) {
            auto value = arguments.next();
            if (value.is_none()) return missing_value("--package"_str);
            request.selection.packages.push(rstd::move(value).unwrap());
        } else if (*option == "--out"_str) {
            auto value = arguments.next();
            if (value.is_none()) return missing_value("--out"_str);
            request.output = tenon::PathBuf::from(rstd::move(value).unwrap());
        } else if (*option == "--verbose"_str) {
            event_context.verbose = true;
        } else if (*option == "--locked"_str) {
            request.locked = true;
        } else if (*option == "--help"_str || *option == "-h"_str) {
            print_help();
            return 0;
        } else {
            rstd::io::eprintln("tenon: unknown option '{}'", option->as_str());
            return 2;
        }
    }

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
    rstd::io::println(
        "built {} ({}) in {}: {} scanned, {} compiled, {} reused, {} archives, {} executables",
        summary.package.as_str(),
        summary.profile.as_str(),
        summary.output.as_path(),
        summary.scanned,
        summary.compiled,
        summary.reused,
        summary.archives.len(),
        summary.executables.len());
    if (event_context.verbose) {
        rstd::io::println(
            "frontend: {} source requests, {} hits, {} stats, {} reads, {} bytes, {} lexed, {} analyzed, {} analysis hits",
            summary.frontend.source_requests,
            summary.frontend.source_hits,
            summary.frontend.source_stats,
            summary.frontend.source_reads,
            summary.frontend.source_bytes,
            summary.frontend.lex_builds,
            summary.frontend.analyze_builds,
            summary.frontend.analyze_hits);
    }
    return 0;
}
