export module lito.driver:registry.http;

import rstd;
import lito.core;
import lito.system;
import :registry.index;

using namespace rstd::prelude;
using namespace rstd::literals;
using PathBuf = rstd::path::PathBuf;

export namespace lito::registry
{

class CurlRegistryHttpTransport {
    PathBuf                                         executable_;
    const lito::system::ResolvedProcessEnvironment* environment_ {};

    static auto get_callback(void* context, const RegistryHttpRequest& request) noexcept
        -> RegistryHttpResult;

public:
    CurlRegistryHttpTransport(PathBuf                                         executable,
                              const lito::system::ResolvedProcessEnvironment& environment)
        : executable_(rstd::move(executable)), environment_(rstd::addressof(environment)) {}

    auto get(const RegistryHttpRequest& request) -> RegistryHttpResult;
    auto transport() noexcept -> RegistryHttpTransport {
        return RegistryHttpTransport {
            .context = this,
            .get     = get_callback,
        };
    }
};

} // namespace lito::registry

namespace
{

using namespace lito::registry;

inline constexpr auto CURL_TRAILER = "\nLITO_REGISTRY_HTTP_V1\n"_str;

template<typename T>
auto http_failure(const RegistryPackageId& package, String message)
    -> Result<T, RegistryIndexError> {
    return Err(RegistryIndexError {
        .kind    = RegistryIndexErrorKind::Network,
        .package = package.clone(),
        .message = rstd::move(message),
    });
}

template<typename T>
auto http_failure(const RegistryPackageId& package, ref<str> message)
    -> Result<T, RegistryIndexError> {
    return http_failure<T>(package, String::make(message));
}

auto push_path(Vec<String>& arguments, ref<rstd::path::Path> path, const RegistryPackageId& package)
    -> Result<empty, RegistryIndexError> {
    auto text = path.to_str();
    if (text.is_none()) {
        return http_failure<empty>(
            package, rstd::format("curl executable path '{}' is not valid UTF-8", path));
    }
    arguments.push(String::make(*text));
    return Ok(empty {});
}

auto parse_curl_response(String output, const RegistryPackageId& package) -> RegistryHttpResult {
    auto trailer = output.as_str().rfind(CURL_TRAILER);
    if (trailer.is_none()) {
        return http_failure<RegistryHttpResponse>(
            package, "curl output has no Registry HTTP status trailer"_str);
    }
    auto body      = output.as_str().get(usize {}, *trailer).unwrap();
    auto metadata  = output.as_str().get(*trailer + CURL_TRAILER.len(), output.len()).unwrap();
    auto separated = metadata.split_once("\n"_str);
    auto status_text =
        separated.is_some() ? separated->template get<0>().trim_ascii() : metadata.trim_ascii();
    auto parsed = lito::parse::parse_canonical_u64_decimal(status_text);
    if (parsed.is_err() || *parsed > u64(999)) {
        return http_failure<RegistryHttpResponse>(
            package, "curl output has an invalid Registry HTTP status"_str);
    }
    auto etag = Option<String> {};
    if (separated.is_some()) {
        auto value = separated->template get<1>().trim_ascii();
        if (! value.is_empty()) etag = Some(String::make(value));
    }
    return Ok(RegistryHttpResponse {
        .status = as_cast<u16>(*parsed),
        .body   = String::make(body),
        .etag   = rstd::move(etag),
    });
}

} // namespace

auto lito::registry::CurlRegistryHttpTransport::get_callback(
    void*                      context,
    const RegistryHttpRequest& request) noexcept -> RegistryHttpResult {
    return static_cast<CurlRegistryHttpTransport*>(context)->get(request);
}

auto lito::registry::CurlRegistryHttpTransport::get(const RegistryHttpRequest& request)
    -> RegistryHttpResult {
    if (environment_ == nullptr) {
        return http_failure<RegistryHttpResponse>(
            request.package, "curl Registry transport has no process environment"_str);
    }
    auto arguments = Vec<String>::make();
    auto path      = push_path(arguments, executable_.as_path(), request.package);
    if (path.is_err()) return Err(rstd::move(path).unwrap_err());
    arguments.push(String::make("--silent"_str));
    arguments.push(String::make("--show-error"_str));
    arguments.push(String::make("--location"_str));
    arguments.push(String::make("--globoff"_str));
    arguments.push(String::make("--proto"_str));
    arguments.push(String::make("=https"_str));
    arguments.push(String::make("--proto-redir"_str));
    arguments.push(String::make("=https"_str));
    arguments.push(String::make("--connect-timeout"_str));
    arguments.push(String::make("30"_str));
    arguments.push(String::make("--max-filesize"_str));
    arguments.push(String::make("16777216"_str));
    arguments.push(String::make("--header"_str));
    arguments.push(String::make("Accept: application/json"_str));
    if (request.if_none_match.is_some()) {
        arguments.push(String::make("--header"_str));
        arguments.push(rstd::format("If-None-Match: {}", request.if_none_match->as_str()));
    }
    arguments.push(String::make("--write-out"_str));
    arguments.push(String::make("\nLITO_REGISTRY_HTTP_V1\n%{response_code}\n%header{etag}"_str));
    arguments.push(String::make("--"_str));
    arguments.push(request.url.clone());
    auto executed = lito::system::run_command(arguments, *environment_);
    if (executed.is_err()) {
        return http_failure<RegistryHttpResponse>(
            request.package,
            rstd::format("cannot execute Registry HTTP request: {}",
                         rstd::move(executed).unwrap_err()));
    }
    auto output = rstd::move(executed).unwrap();
    if (output.exit_code != i32 {}) {
        return http_failure<RegistryHttpResponse>(
            request.package,
            rstd::format("Registry HTTP request failed with curl exit code {}: {}",
                         output.exit_code,
                         output.standard_error.as_str().trim_ascii()));
    }
    return parse_curl_response(rstd::move(output.standard_output), request.package);
}
