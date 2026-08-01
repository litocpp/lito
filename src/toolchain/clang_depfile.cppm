export module tenon.toolchain.clang_depfile;

import rstd;
import tenon.model;

using namespace rstd::prelude;
using namespace rstd::literals;
using StringSet = rstd::collections::BTreeMap<String, empty>;

namespace tenon::toolchain
{

template<typename T>
auto depfile_failure(String message) -> Result<T> {
    return Err(Error::make(ErrorKind::Toolchain, rstd::move(message)));
}

} // namespace tenon::toolchain

export namespace tenon::toolchain
{

auto parse_depfile(ref<rstd::path::Path> depfile, ref<rstd::path::Path> working_directory)
    -> Result<Vec<PathBuf>> {
    auto contents_result = rstd::fs::read_to_string(depfile);
    if (contents_result.is_err()) {
        return depfile_failure<Vec<PathBuf>>(rstd::format(
            "cannot read '{}': {}", depfile, rstd::move(contents_result).unwrap_err()));
    }
    auto contents = rstd::move(contents_result).unwrap();
    auto bytes    = contents.as_str().as_bytes();

    auto colon   = Option<usize> {};
    auto escaped = false;
    for (auto index = usize {}; index < contents.len(); ++index) {
        const auto value = bytes[index];
        if (! escaped && value == u8(':')) {
            colon = Some(index);
            break;
        }
        if (! escaped && value == u8('\\')) {
            escaped = true;
        } else {
            escaped = false;
        }
    }
    if (colon.is_none()) {
        return depfile_failure<Vec<PathBuf>>(
            rstd::format("invalid dependency file '{}': missing ':'", depfile));
    }

    auto tokens  = Vec<String>::make();
    auto current = String::make();
    for (auto index = *colon + usize(1); index < contents.len(); ++index) {
        const auto value = bytes[index];
        if (value == u8('\\') && index + usize(1) < contents.len()) {
            const auto next = bytes[index + usize(1)];
            if (next == u8('\n')) {
                ++index;
                continue;
            }
            if (next == u8('\r') && index + usize(2) < contents.len() &&
                bytes[index + usize(2)] == u8('\n')) {
                index += usize(2);
                continue;
            }
            current.push_ascii(next);
            ++index;
            continue;
        }
        if (value == u8(' ') || value == u8('\t') || value == u8('\r') || value == u8('\n')) {
            if (! current.is_empty()) {
                tokens.push(rstd::move(current));
                current = String::make();
            }
            continue;
        }
        current.push_ascii(value);
    }
    if (! current.is_empty()) tokens.push(rstd::move(current));

    auto result = Vec<PathBuf>::make();
    auto seen   = StringSet::make();
    for (const auto& token : tokens) {
        auto path = PathBuf::from(token.as_str());
        if (path.as_path().is_relative()) {
            path = PathBuf::from(working_directory).join(path.as_path());
        }
        auto canonical = rstd::fs::canonicalize(path.as_path());
        if (canonical.is_err()) {
            return depfile_failure<Vec<PathBuf>>(
                rstd::format("cannot resolve dependency input '{}': {}",
                             path.as_path(),
                             rstd::move(canonical).unwrap_err()));
        }
        auto resolved = rstd::move(canonical).unwrap();
        auto text     = resolved.as_path().to_str();
        if (text.is_none()) {
            return depfile_failure<Vec<PathBuf>>(
                rstd::format("dependency input '{}' is not valid UTF-8", resolved.as_path()));
        }
        if (! seen.contains_key(*text)) {
            seen.insert(String::make(*text), empty {});
            result.push(rstd::move(resolved));
        }
    }
    return Ok(rstd::move(result));
}

} // namespace tenon::toolchain
