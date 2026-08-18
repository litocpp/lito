module;
#include <rstd/enum.hpp>

export module lito.cpp:link;

import rstd;
import lito.core;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::link
{

struct ArgumentSequence {
    Vec<String> tokens;
    String      source;
    String      identity;

    auto clone() const -> ArgumentSequence {
        return ArgumentSequence {
            .tokens   = as<Clone>(tokens).clone(),
            .source   = source.clone(),
            .identity = identity.clone(),
        };
    }
};

struct SystemLibraryRequirement : DefaultInClass<SystemLibraryRequirement, Clone> {
    String name;
    String source;

    auto clone() const -> SystemLibraryRequirement {
        return SystemLibraryRequirement { .name = name.clone(), .source = source.clone() };
    }
};

struct RuntimeSearchRequirement : DefaultInClass<RuntimeSearchRequirement, Clone> {
    String path;
    String source;

    auto clone() const -> RuntimeSearchRequirement {
        return RuntimeSearchRequirement { .path = path.clone(), .source = source.clone() };
    }
};

struct Requirements {
    bool                          posix_threads { false };
    Vec<String>                   thread_sources;
    Vec<SystemLibraryRequirement> system_libraries;
    Vec<RuntimeSearchRequirement> runtime_search_paths;

    auto clone() const -> Requirements {
        return Requirements {
            .posix_threads        = posix_threads,
            .thread_sources       = as<Clone>(thread_sources).clone(),
            .system_libraries     = as<Clone>(system_libraries).clone(),
            .runtime_search_paths = as<Clone>(runtime_search_paths).clone(),
        };
    }
};

class ArgumentError {
    RSTD_ENUM(ArgumentError,
              (InvalidRuntimeSearchPath, (String source; String token; String reason;)),
              (LegacyRpath, (String source; String token;)))
};

template<typename T>
using ArgumentResult = Result<T, ArgumentError>;

struct NormalizedArguments {
    ArgumentSequence arguments;
    Requirements     requirements;
};

auto requirements_identity(const Requirements& requirements) -> String;
auto normalize_arguments(ArgumentSequence input) -> ArgumentResult<NormalizedArguments>;
auto append_requirements(Requirements& output, const Requirements& input) -> void;
auto replace_runtime_search_paths(Requirements&                     output,
                                  const lito::artifact::ElfRunpath& runpath,
                                  ref<str>                          source) -> void;

} // namespace lito::link

export namespace rstd
{

template<>
struct Impl<fmt::Display, lito::link::ArgumentError> : ImplBase<lito::link::ArgumentError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        const auto& error = this->self();
        if (error.is_LegacyRpath()) {
            return formatter.write_fmt(
                fmt::Arguments::make("linker option '{}' from {} requests legacy DT_RPATH",
                                     error.as_LegacyRpath().token,
                                     error.as_LegacyRpath().source));
        }
        return formatter.write_fmt(
            fmt::Arguments::make("linker runtime search option '{}' from {} is invalid: {}",
                                 error.as_InvalidRuntimeSearchPath().token,
                                 error.as_InvalidRuntimeSearchPath().source,
                                 error.as_InvalidRuntimeSearchPath().reason));
    }
};

template<>
struct Impl<fmt::Debug, lito::link::ArgumentError> : ImplBase<lito::link::ArgumentError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, lito::link::ArgumentError>
    : DefaultInImpl<error::Error, lito::link::ArgumentError> {};

} // namespace rstd

namespace lito::link
{

auto append_unique(Vec<String>& output, const Vec<String>& input) -> void {
    for (const auto& value : input) {
        auto present = false;
        for (const auto& existing : output) {
            if (existing == value.as_str()) present = true;
        }
        if (! present) output.push(value.clone());
    }
}

auto append_runtime_search(Requirements& output, ref<str> path, ref<str> source) -> void {
    for (const auto& existing : output.runtime_search_paths) {
        if (existing.path == path) return;
    }
    output.runtime_search_paths.push(RuntimeSearchRequirement {
        .path   = String::make(path),
        .source = String::make(source),
    });
}

auto runtime_search_value(ref<str> token) -> Option<ref<str>> {
    constexpr ref<str> prefixes[] = {
        "-Wl,--rpath,"_str,
        "-Wl,-rpath,"_str,
        "-Wl,--rpath="_str,
        "-Wl,-rpath="_str,
    };
    for (auto prefix : prefixes) {
        auto value = token.strip_prefix(prefix);
        if (value.is_some()) return value;
    }
    return None();
}

auto validate_runtime_search(ref<str> value, ref<str> source, ref<str> token)
    -> ArgumentResult<empty> {
    if (value.is_empty()) {
        return Err(ArgumentError::InvalidRuntimeSearchPath(
            String::make(source), String::make(token), String::make("path is empty"_str)));
    }
    for (auto byte : value.as_bytes()) {
        if (byte == u8 {}) {
            return Err(ArgumentError::InvalidRuntimeSearchPath(
                String::make(source), String::make(token), String::make("path contains NUL"_str)));
        }
    }
    return Ok(empty {});
}

auto append_runtime_search(Requirements& output, ref<str> value, ref<str> source, ref<str> token)
    -> ArgumentResult<empty> {
    auto remaining = value;
    while (true) {
        auto separated = remaining.split_once(":"_str);
        auto path      = separated.is_some() ? separated->get<0>() : remaining;
        rstd_try(validate_runtime_search(path, source, token));
        append_runtime_search(output, path, source);
        if (separated.is_none()) break;
        remaining = separated->get<1>();
    }
    return Ok(empty {});
}

auto requirements_identity(const Requirements& requirements) -> String {
    auto result = String::make("lito-link-requirements-v2\n"_str);
    result.push_str(requirements.posix_threads ? "posix-threads=true\n"_str
                                               : "posix-threads=false\n"_str);
    for (const auto& requirement : requirements.system_libraries) {
        result.push_str("system-library="_str);
        result.push_str(requirement.name.as_str());
        result.push_ascii('\n');
    }
    for (const auto& requirement : requirements.runtime_search_paths) {
        result.push_str("runtime-search="_str);
        result.push_str(requirement.path.as_str());
        result.push_ascii('\n');
    }
    return result;
}

auto normalize_arguments(ArgumentSequence input) -> ArgumentResult<NormalizedArguments> {
    auto tokens       = Vec<String>::make();
    auto requirements = Requirements {};
    for (auto index = usize {}; index < input.tokens.len(); ++index) {
        auto token = input.tokens[index].as_str();
        if (token == "-pthread"_str) {
            requirements.posix_threads = true;
            requirements.thread_sources.push(input.source.clone());
            continue;
        }
        if (token == "-ldl"_str || (token == "-l"_str && index + usize(1) < input.tokens.len() &&
                                    input.tokens[index + usize(1)].as_str() == "dl"_str)) {
            requirements.system_libraries.push(SystemLibraryRequirement {
                .name   = String::make("dl"_str),
                .source = input.source.clone(),
            });
            if (token == "-l"_str) ++index;
            continue;
        }
        if (token == "-Wl,--disable-new-dtags"_str || token == "--disable-new-dtags"_str) {
            return Err(
                ArgumentError::LegacyRpath(input.source.clone(), input.tokens[index].clone()));
        }
        if (token == "-Wl,--enable-new-dtags"_str || token == "--enable-new-dtags"_str) {
            continue;
        }
        auto runtime = runtime_search_value(token);
        if (runtime.is_some()) {
            rstd_try(append_runtime_search(requirements, **runtime, input.source.as_str(), token));
            continue;
        }
        if (token == "-Xlinker"_str && index + usize(1) < input.tokens.len()) {
            auto linker = input.tokens[index + usize(1)].as_str();
            if (linker == "--disable-new-dtags"_str) {
                return Err(ArgumentError::LegacyRpath(input.source.clone(),
                                                      input.tokens[index + usize(1)].clone()));
            }
            if (linker == "--enable-new-dtags"_str) {
                ++index;
                continue;
            }
            auto equals = linker.strip_prefix("--rpath="_str);
            if (equals.is_none()) equals = linker.strip_prefix("-rpath="_str);
            if (equals.is_some()) {
                rstd_try(
                    append_runtime_search(requirements, **equals, input.source.as_str(), linker));
                ++index;
                continue;
            }
            if ((linker == "--rpath"_str || linker == "-rpath"_str) &&
                index + usize(3) < input.tokens.len() &&
                input.tokens[index + usize(2)].as_str() == "-Xlinker"_str) {
                auto value = input.tokens[index + usize(3)].as_str();
                rstd_try(append_runtime_search(requirements, value, input.source.as_str(), linker));
                index += usize(3);
                continue;
            }
        }
        tokens.push(input.tokens[index].clone());
    }
    input.tokens = rstd::move(tokens);
    return Ok(NormalizedArguments {
        .arguments    = rstd::move(input),
        .requirements = rstd::move(requirements),
    });
}

auto append_requirements(Requirements& output, const Requirements& input) -> void {
    if (input.posix_threads) {
        output.posix_threads = true;
        append_unique(output.thread_sources, input.thread_sources);
    }
    for (const auto& requirement : input.system_libraries) {
        auto present = false;
        for (const auto& existing : output.system_libraries) {
            if (existing.name == requirement.name.as_str()) present = true;
        }
        if (! present) output.system_libraries.push(requirement.clone());
    }
    for (const auto& requirement : input.runtime_search_paths) {
        append_runtime_search(output, requirement.path.as_str(), requirement.source.as_str());
    }
}

auto replace_runtime_search_paths(Requirements&                     output,
                                  const lito::artifact::ElfRunpath& runpath,
                                  ref<str>                          source) -> void {
    output.runtime_search_paths.clear();
    for (const auto& path : runpath.paths) {
        auto value = String::make("$ORIGIN"_str);
        auto text  = path.path.as_path().to_string_lossy();
        if (text.as_str() != "."_str) {
            value.push_ascii('/');
            value.push_str(text.as_str());
        }
        append_runtime_search(output, value.as_str(), source);
    }
}

} // namespace lito::link
