module;
#include <rstd/macro.hpp>

module lito.driver:build.generated.depfile;

import rstd;
import :build.generated.model;

using namespace rstd::prelude;
using namespace rstd::literals;
using PathBuf = rstd::path::PathBuf;

namespace lito
{

auto make_depfile_paths(ref<str>              text,
                        ref<rstd::path::Path> working_directory,
                        ActionDepfileFormat   format) -> BuildScriptResult<Vec<PathBuf>> {
    auto       bytes        = text.as_bytes();
    auto       result       = Vec<PathBuf>::make();
    auto       token        = Vec<u8>::make();
    auto       targets      = false;
    auto       dependencies = false;
    auto       saw_rule     = false;
    auto       quoted       = false;
    const auto nmake        = format == ActionDepfileFormat::NMake;
    auto       invalid      = [](ref<str> message) {
        return BuildScriptError::BuildToolAction(
            BuildToolActionError::InvalidRequest(message.into()));
    };
    auto publish = [&]() -> BuildScriptResult<empty> {
        if (token.is_empty()) return Ok(empty {});
        if (! dependencies) {
            targets = true;
            token.clear();
            return Ok(empty {});
        }
        auto decoded = String::from_utf8(rstd::move(token));
        token        = Vec<u8>::make();
        if (decoded.is_err())
            return Err(invalid("build-tool depfile contains a non-UTF-8 dependency path"_str));
        auto path = PathBuf::from(rstd::move(decoded).unwrap());
        if (! path.as_path().is_absolute())
            path = PathBuf::from(working_directory).join(path.as_path());
        result.push(rstd::move(path));
        return Ok(empty {});
    };
    auto finish_rule = [&]() -> BuildScriptResult<empty> {
        if (quoted) return Err(invalid("build-tool depfile contains an unterminated quote"_str));
        rstd_try(publish());
        if (targets && ! dependencies)
            return Err(invalid("build-tool depfile rule does not contain a target separator"_str));
        targets      = false;
        dependencies = false;
        return Ok(empty {});
    };
    auto newline_length = [&](usize index) -> usize {
        if (index >= bytes.len()) return usize {};
        if (bytes[index] == u8('\n')) return usize(1);
        if (bytes[index] == u8('\r') && index + usize(1) < bytes.len() &&
            bytes[index + usize(1)] == u8('\n'))
            return usize(2);
        return usize {};
    };
    for (usize index {}; index < bytes.len();) {
        const auto byte = bytes[index];
        if (byte == u8(0)) return Err(invalid("build-tool depfile contains a NUL byte"_str));
        const auto newline = newline_length(index);
        if (newline != usize {}) {
            rstd_try(finish_rule());
            index += newline;
            continue;
        }
        if (byte == u8('\r'))
            return Err(invalid("build-tool depfile contains a bare carriage return"_str));
        if (nmake && byte == u8('"')) {
            quoted = ! quoted;
            ++index;
            continue;
        }
        if (! quoted && byte == u8('#')) {
            rstd_try(publish());
            while (index < bytes.len() && newline_length(index) == usize {}) {
                if (bytes[index] == u8(0))
                    return Err(invalid("build-tool depfile contains a NUL byte"_str));
                if (bytes[index] == u8('\\') && newline_length(index + usize(1)) != usize {}) {
                    index += usize(1) + newline_length(index + usize(1));
                } else {
                    ++index;
                }
            }
            continue;
        }
        if (! quoted && (byte == u8(' ') || byte == u8('\t'))) {
            rstd_try(publish());
            ++index;
            continue;
        }
        if (! quoted && ! dependencies && byte == u8(':')) {
            const auto drive =
                token.len() == usize(1) &&
                ((token[usize {}] >= u8('A') && token[usize {}] <= u8('Z')) ||
                 (token[usize {}] >= u8('a') && token[usize {}] <= u8('z'))) &&
                index + usize(1) < bytes.len() &&
                (bytes[index + usize(1)] == u8('/') || bytes[index + usize(1)] == u8('\\'));
            if (! drive) {
                rstd_try(publish());
                if (! targets) return Err(invalid("build-tool depfile rule has no target"_str));
                dependencies = true;
                saw_rule     = true;
                ++index;
                continue;
            }
        }
        if (byte == u8('$') && ! nmake) {
            if (index + usize(1) >= bytes.len() || bytes[index + usize(1)] != u8('$'))
                return Err(invalid("build-tool depfile cannot expand Make variables"_str));
            token.push(u8('$'));
            index += usize(2);
            continue;
        }
        if (! quoted && (byte == u8(';') || byte == u8('|')))
            return Err(invalid("build-tool depfile only supports compiler dependency rules"_str));
        if (byte == u8('\\')) {
            auto end = index;
            while (end < bytes.len() && bytes[end] == u8('\\')) ++end;
            auto       count     = end - index;
            const auto continued = newline_length(end);
            if (continued != usize {}) {
                for (usize offset(1); offset < count; ++offset) token.push(u8('\\'));
                rstd_try(publish());
                index = end + continued;
                continue;
            }
            if (! nmake && end < bytes.len()) {
                const auto next = bytes[end];
                const auto target_separator =
                    ! dependencies && next == u8(':') &&
                    (end + usize(1) == bytes.len() || bytes[end + usize(1)] == u8(' ') ||
                     bytes[end + usize(1)] == u8('\t') ||
                     newline_length(end + usize(1)) != usize {});
                if (target_separator) {
                    for (usize offset {}; offset < count; ++offset) token.push(u8('\\'));
                    index = end;
                    continue;
                }
                if ((next == u8(' ') || next == u8('\t')) && count % usize(2) != usize {}) {
                    for (usize offset {}; offset < count / usize(2); ++offset) token.push(u8('\\'));
                    token.push(u8(next.to_primitive()));
                    index = end + usize(1);
                    continue;
                }
                if (next == u8('#') || next == u8(':')) {
                    for (usize offset(1); offset < count; ++offset) token.push(u8('\\'));
                    token.push(u8(next.to_primitive()));
                    index = end + usize(1);
                    continue;
                }
            }
            // Compiler depfiles preserve backslashes that do not escape delimiters.
            for (usize offset {}; offset < count; ++offset) token.push(u8('\\'));
            index = end;
            continue;
        }
        token.push(u8(byte.to_primitive()));
        ++index;
    }
    rstd_try(finish_rule());
    if (! saw_rule)
        return Err(invalid("build-tool depfile does not contain a dependency rule"_str));
    return Ok(rstd::move(result));
}

auto dxc_depfile_paths(ref<str> text, ref<rstd::path::Path> working_directory)
    -> BuildScriptResult<Vec<PathBuf>> {
    for (const auto byte : text.as_bytes()) {
        if (byte == u8(0)) {
            return Err(BuildScriptError::BuildToolAction(
                BuildToolActionError::InvalidRequest("DXC depfile contains a NUL byte"_Str)));
        }
    }
    auto separator = text.find(": "_str);
    if (separator.is_none() || *separator == usize {}) {
        return Err(BuildScriptError::BuildToolAction(BuildToolActionError::InvalidRequest(
            "DXC depfile does not contain a target separator"_Str)));
    }
    auto bytes = text.as_bytes();
    auto end   = bytes.len();
    if (end > usize {} && bytes[end - usize(1)] == u8('\n')) --end;
    if (end > usize {} && bytes[end - usize(1)] == u8('\r')) --end;
    auto token   = Vec<u8>::make();
    auto result  = Vec<PathBuf>::make();
    auto publish = [&]() -> BuildScriptResult<empty> {
        if (token.is_empty()) {
            return Err(BuildScriptError::BuildToolAction(BuildToolActionError::InvalidRequest(
                "DXC depfile contains an empty dependency"_Str)));
        }
        auto decoded = String::from_utf8(rstd::move(token));
        token        = Vec<u8>::make();
        if (decoded.is_err()) {
            return Err(BuildScriptError::BuildToolAction(BuildToolActionError::InvalidRequest(
                "DXC depfile contains a non-UTF-8 dependency path"_Str)));
        }
        auto path = PathBuf::from(rstd::move(decoded).unwrap());
        if (! path.as_path().is_absolute())
            path = PathBuf::from(working_directory).join(path.as_path());
        result.push(rstd::move(path));
        return Ok(empty {});
    };
    // DXC writes raw paths separated by " \\\n ", without Make escaping.
    for (auto index = *separator + usize(2); index < end; ++index) {
        if (bytes[index] == u8(' ') && index + usize(3) < end &&
            bytes[index + usize(1)] == u8('\\')) {
            auto newline = index + usize(2);
            if (bytes[newline] == u8('\r')) ++newline;
            if (newline + usize(1) < end && bytes[newline] == u8('\n') &&
                bytes[newline + usize(1)] == u8(' ')) {
                rstd_try(publish());
                index = newline + usize(1);
                continue;
            }
        }
        if (bytes[index] == u8('\n') || bytes[index] == u8('\r')) {
            return Err(BuildScriptError::BuildToolAction(BuildToolActionError::InvalidRequest(
                "DXC depfile contains an invalid dependency separator"_Str)));
        }
        token.push(u8(bytes[index].to_primitive()));
    }
    rstd_try(publish());
    return Ok(rstd::move(result));
}

auto action_depfile_paths(ref<str>              text,
                          ref<rstd::path::Path> working_directory,
                          ActionDepfileFormat   format) -> BuildScriptResult<Vec<PathBuf>> {
    return format == ActionDepfileFormat::Dxc ? dxc_depfile_paths(text, working_directory)
                                              : make_depfile_paths(text, working_directory, format);
}

} // namespace lito
