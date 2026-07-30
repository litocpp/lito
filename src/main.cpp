import rstd;
import tenon;

namespace
{

using namespace rstd::literals;

struct EventContext {
    bool verbose { false };
};

auto event_name(tenon::BuildEventKind kind) -> rstd::ref<rstd::str> {
    switch (kind) {
    case tenon::BuildEventKind::Scan: return "scan"_str;
    case tenon::BuildEventKind::Compile: return "compile"_str;
    case tenon::BuildEventKind::Reuse: return "reuse"_str;
    case tenon::BuildEventKind::Archive: return "archive"_str;
    case tenon::BuildEventKind::Link: return "link"_str;
    }
    return "unknown"_str;
}

auto configured_path(const char* value) -> tenon::PathBuf {
    return tenon::PathBuf::from(rstd::ffi::CStr::from_ptr(value).to_str().unwrap());
}

void observe(void* raw_context, const tenon::BuildEvent& event) noexcept {
    auto& context = *static_cast<EventContext*>(raw_context);
    if (! context.verbose && event.kind != tenon::BuildEventKind::Compile &&
        event.kind != tenon::BuildEventKind::Archive &&
        event.kind != tenon::BuildEventKind::Link) {
        return;
    }
    rstd::io::println("[{}] {} {}", event_name(event.kind), event.target, event.path);
}

void print_help() {
    rstd::io::println("tenon - module-first C++ builder");
    rstd::io::println("");
    rstd::io::println(
        "Usage: tenon build <manifest> [--profile <name>] [--target <name>] [--out <dir>] [--locked] [--verbose]");
    rstd::io::println("");
    rstd::io::println("Supported target toolchains: clang++ with libstdc++ or libc++");
    rstd::io::println("All builds use -fno-rtti -fno-exceptions");
}

auto missing_value(rstd::ref<rstd::str> option) -> int {
    rstd::io::eprintln("tenon: {} requires a value", option);
    return 2;
}

} // namespace

int main() {
    auto arguments = rstd::env::args();
    (void)arguments.next();
    auto command = arguments.next();
    if (command.is_none() || (*command == "--help"_str) || (*command == "-h"_str)) {
        print_help();
        return 0;
    }
    if (*command != "build"_str) {
        rstd::io::eprintln("tenon: unknown command '{}'", command->as_str());
        print_help();
        return 2;
    }

    auto manifest = arguments.next();
    if (manifest.is_none()) {
        rstd::io::eprintln("tenon: build requires a manifest path");
        return 2;
    }

    auto request = tenon::BuildRequest {};
    request.manifest = tenon::PathBuf::from(rstd::move(manifest).unwrap());
    request.configuration.profile_name = tenon::String::make("default"_str);
    request.configuration.toolchain = tenon::ToolchainSpec {
        .compiler = configured_path(TENON_DEFAULT_CLANGXX),
        .scanner = configured_path(TENON_DEFAULT_CLANG_SCAN_DEPS),
        .archiver = configured_path(TENON_DEFAULT_LLVM_AR),
    };
    request.configuration.standard_library = tenon::StandardLibrary::Libcxx;
    request.configuration.bmi_mode = tenon::BmiMode::Reduced;
    request.configuration.language_standard = tenon::String::make("c++20"_str);
    auto event_context = EventContext {};
    while (auto option = arguments.next()) {
        if (*option == "--profile"_str) {
            auto value = arguments.next();
            if (value.is_none()) return missing_value("--profile"_str);
            request.profile = rstd::move(value).unwrap();
        } else if (*option == "--target"_str) {
            auto value = arguments.next();
            if (value.is_none()) return missing_value("--target"_str);
            request.targets.push(rstd::move(value).unwrap());
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

    request.observer = rstd::Some(tenon::BuildObserver {
        .context = rstd::addressof(event_context),
        .notify = observe,
    });
    auto result = tenon::build(request);
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
