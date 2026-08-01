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
    rstd::io::println("       tenon [-C <directory>] format [--package <name>]");
}

auto missing_value(ref<str> option) -> int {
    rstd::io::eprintln("tenon: {} requires a value", option);
    return 2;
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
    if (*command != "build"_str && *command != "format"_str) {
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

    auto request                            = tenon::BuildRequest {};
    request.selection.root                  = rstd::move(project.root);
    request.configuration.toolchain         = rstd::move(project.toolchain);
    request.configuration.standard_library  = tenon::StandardLibrary::Libcxx;
    request.configuration.bmi_mode          = tenon::BmiMode::Reduced;
    request.configuration.language_standard = tenon::String::make("c++20"_str);
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
    return 0;
}
