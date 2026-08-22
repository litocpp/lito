module;
#include <rstd/enum.hpp>
#include <rstd/macro.hpp>

export module lito.toolchain.android:ndk;

import rstd;
import lito.crypto;
import rstd.json;
import lito.core;

using namespace rstd::prelude;
using namespace rstd::literals;
using Json = rstd::json::Value;

export namespace lito
{

class AndroidNdkError {
    RSTD_ENUM(AndroidNdkError,
              (Json, (PathBuf path; rstd::json::Error source;)),
              (Io, (String operation; PathBuf path; rstd::io::error::Error source;)),
              (Platform, (lito::system::PlatformError source;)),
              (Message, (String message;)))
};

template<typename T>
using AndroidNdkResult = Result<T, AndroidNdkError>;

struct AndroidNdkRevision {
    u64    major {};
    u64    minor {};
    u64    build {};
    String text;

    auto clone() const -> AndroidNdkRevision {
        return AndroidNdkRevision {
            .major = major,
            .minor = minor,
            .build = build,
            .text  = text.clone(),
        };
    }
};

struct AndroidNdkAbi {
    String name;
    String triple;
    String llvm_triple;
    u32    minimum_api {};
    bool   default_supported { false };
    bool   deprecated { false };

    auto clone() const -> AndroidNdkAbi {
        return AndroidNdkAbi {
            .name              = name.clone(),
            .triple            = triple.clone(),
            .llvm_triple       = llvm_triple.clone(),
            .minimum_api       = minimum_api,
            .default_supported = default_supported,
            .deprecated        = deprecated,
        };
    }
};

struct AndroidNdkPlatforms {
    u32 minimum {};
    u32 maximum {};
};

struct AndroidNdkPaths {
    PathBuf cc;
    PathBuf cxx;
    PathBuf linker;
    PathBuf archiver;
    PathBuf strip;
    PathBuf readelf;
    PathBuf sysroot;
    PathBuf cmake_toolchain;

    auto clone() const -> AndroidNdkPaths {
        return AndroidNdkPaths {
            .cc              = cc.clone(),
            .cxx             = cxx.clone(),
            .linker          = linker.clone(),
            .archiver        = archiver.clone(),
            .strip           = strip.clone(),
            .readelf         = readelf.clone(),
            .sysroot         = sysroot.clone(),
            .cmake_toolchain = cmake_toolchain.clone(),
        };
    }
};

class AndroidNdkDistribution {
    PathBuf             root_;
    AndroidNdkRevision  revision_;
    String              release_name_;
    String              host_tag_;
    AndroidNdkPaths     paths_;
    Vec<AndroidNdkAbi>  abis_;
    AndroidNdkPlatforms platforms_;
    String              identity_;

public:
    AndroidNdkDistribution(PathBuf             root,
                           AndroidNdkRevision  revision,
                           String              release_name,
                           String              host_tag,
                           AndroidNdkPaths     paths,
                           Vec<AndroidNdkAbi>  abis,
                           AndroidNdkPlatforms platforms,
                           String              identity)
        : root_(rstd::move(root)),
          revision_(rstd::move(revision)),
          release_name_(rstd::move(release_name)),
          host_tag_(rstd::move(host_tag)),
          paths_(rstd::move(paths)),
          abis_(rstd::move(abis)),
          platforms_(platforms),
          identity_(rstd::move(identity)) {}

    auto clone() const -> AndroidNdkDistribution {
        auto abis = Vec<AndroidNdkAbi>::with_capacity(abis_.len());
        for (const auto& abi : abis_) abis.push(abi.clone());
        return AndroidNdkDistribution(root_.clone(),
                                      revision_.clone(),
                                      release_name_.clone(),
                                      host_tag_.clone(),
                                      paths_.clone(),
                                      rstd::move(abis),
                                      platforms_,
                                      identity_.clone());
    }

    auto root() const noexcept -> ref<rstd::path::Path> { return root_.as_path(); }
    auto revision() const noexcept -> const AndroidNdkRevision& { return revision_; }
    auto release_name() const noexcept -> ref<str> { return release_name_.as_str(); }
    auto host_tag() const noexcept -> ref<str> { return host_tag_.as_str(); }
    auto paths() const noexcept -> const AndroidNdkPaths& { return paths_; }
    auto abis() const noexcept -> const Vec<AndroidNdkAbi>& { return abis_; }
    auto platforms() const noexcept -> const AndroidNdkPlatforms& { return platforms_; }
    auto identity() const noexcept -> ref<str> { return identity_.as_str(); }

    auto toolchain_spec() const -> lito::config::ToolchainSpec {
        return lito::config::ToolchainSpec {
            .cc  = paths_.cc.clone(),
            .cxx = paths_.cxx.clone(),
            .ld  = paths_.linker.clone(),
            .ar  = paths_.archiver.clone(),
        };
    }
};

struct ResolvedAndroidTarget {
    String                   abi;
    u32                      minimum_api {};
    String                   clang_target;
    String                   library_triple;
    lito::system::TargetInfo target_info;
    PathBuf                  sysroot;
    String                   output_key;

    auto clone() const -> ResolvedAndroidTarget {
        return ResolvedAndroidTarget {
            .abi            = abi.clone(),
            .minimum_api    = minimum_api,
            .clang_target   = clang_target.clone(),
            .library_triple = library_triple.clone(),
            .target_info    = target_info.clone(),
            .sysroot        = sysroot.clone(),
            .output_key     = output_key.clone(),
        };
    }
};

struct AndroidCmakeProjection {
    PathBuf toolchain_file;
    String  abi;
    String  platform;
    String  standard_library;
    String  identity;

    auto clone() const -> AndroidCmakeProjection {
        return AndroidCmakeProjection {
            .toolchain_file   = toolchain_file.clone(),
            .abi              = abi.clone(),
            .platform         = platform.clone(),
            .standard_library = standard_library.clone(),
            .identity         = identity.clone(),
        };
    }
};

struct AndroidRuntimeArtifact {
    String  name;
    PathBuf path;
    String  identity;

    auto clone() const -> AndroidRuntimeArtifact {
        return AndroidRuntimeArtifact {
            .name     = name.clone(),
            .path     = path.clone(),
            .identity = identity.clone(),
        };
    }
};

struct ResolvedAndroidToolchain {
    AndroidNdkDistribution         distribution;
    ResolvedAndroidTarget          target;
    lito::config::ToolchainSpec    tools;
    AndroidCmakeProjection         cmake;
    Option<AndroidRuntimeArtifact> shared_runtime;

    auto clone() const -> ResolvedAndroidToolchain {
        return ResolvedAndroidToolchain {
            .distribution   = distribution.clone(),
            .target         = target.clone(),
            .tools          = tools.clone(),
            .cmake          = cmake.clone(),
            .shared_runtime = shared_runtime.is_some() ? Some(shared_runtime->clone())
                                                       : Option<AndroidRuntimeArtifact> {},
        };
    }
};

auto parse_android_ndk_revision(ref<str> value) -> AndroidNdkResult<AndroidNdkRevision>;
auto open_android_ndk(ref<rstd::path::Path> root, const lito::system::HostInfo& host)
    -> AndroidNdkResult<AndroidNdkDistribution>;
auto resolve_android_target(const AndroidNdkDistribution&             distribution,
                            const lito::config::AndroidTargetRequest& request)
    -> AndroidNdkResult<ResolvedAndroidTarget>;
auto resolve_android_toolchain(AndroidNdkDistribution                    distribution,
                               const lito::config::AndroidTargetRequest& request,
                               lito::config::StandardLibraryRuntime      runtime)
    -> AndroidNdkResult<ResolvedAndroidToolchain>;
auto resolve_android_build_platform(const lito::system::HostInfo&   host,
                                    const lito::system::TargetInfo& compiler_default,
                                    const ResolvedAndroidTarget&    target)
    -> lito::system::BuildPlatform;

} // namespace lito

export namespace rstd
{

template<>
struct Impl<fmt::Display, lito::AndroidNdkError> : ImplBase<lito::AndroidNdkError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        const auto& error = this->self();
        if (error.is_Json()) {
            return formatter.write_fmt(fmt::Arguments::make(
                "cannot parse Android NDK metadata '{}'", error.as_Json().path.as_path()));
        }
        if (error.is_Io()) {
            return formatter.write_fmt(fmt::Arguments::make(
                "cannot {} '{}'", error.as_Io().operation, error.as_Io().path.as_path()));
        }
        if (error.is_Platform()) return formatter.write_str(error.as_Platform().source.message());
        return formatter.write_str(error.as_Message().message.as_str());
    }
};

template<>
struct Impl<fmt::Debug, lito::AndroidNdkError> : ImplBase<lito::AndroidNdkError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, lito::AndroidNdkError> : ImplBase<lito::AndroidNdkError> {
    auto source() const noexcept -> Option<error::ErrorRef> {
        const auto& error = this->self();
        if (error.is_Json()) return Some(dyn<error::Error>::from_ref(error.as_Json().source));
        if (error.is_Io()) return Some(dyn<error::Error>::from_ref(error.as_Io().source));
        if (error.is_Platform()) {
            return Some(dyn<error::Error>::from_ref(error.as_Platform().source));
        }
        return None();
    }
};

} // namespace rstd

namespace lito
{

template<typename T>
auto android_failure(String message) -> AndroidNdkResult<T> {
    return Err(AndroidNdkError::Message(rstd::move(message)));
}

template<typename T>
auto android_failure(ref<str> message) -> AndroidNdkResult<T> {
    return android_failure<T>(String::make(message));
}

template<typename T>
auto android_io_failure(ref<str>               operation,
                        ref<rstd::path::Path>  path,
                        rstd::io::error::Error source) -> AndroidNdkResult<T> {
    return Err(
        AndroidNdkError::Io(String::make(operation), PathBuf::from(path), rstd::move(source)));
}

auto parse_decimal(ref<str> value, ref<str> context) -> AndroidNdkResult<u64> {
    if (value.is_empty()) return android_failure<u64>(rstd::format("{} is empty", context));
    if (value.len() > usize(1) && value.as_bytes()[usize {}] == u8('0')) {
        return android_failure<u64>(rstd::format("{} has a leading zero", context));
    }
    auto result = u64 {};
    for (const auto byte : value.as_bytes()) {
        if (byte < u8('0') || byte > u8('9')) {
            return android_failure<u64>(rstd::format("{} is not numeric", context));
        }
        const auto digit = u64((byte - u8('0')).to_primitive());
        if (result > (u64::MAX - digit) / u64(10)) {
            return android_failure<u64>(rstd::format("{} is out of range", context));
        }
        result = result * u64(10) + digit;
    }
    return Ok(result);
}

auto property(ref<str> document, ref<str> key) -> Option<String> {
    auto remaining = document;
    while (! remaining.is_empty()) {
        auto newline = remaining.find("\n"_str);
        auto line    = newline.is_some() ? remaining.split_at(*newline).get<0>() : remaining;
        line         = line.trim_ascii();
        if (! line.is_empty() && ! line.starts_with("#"_str)) {
            auto pair = line.split_once("="_str);
            if (pair.is_some() && pair->get<0>().trim_ascii() == key) {
                return Some(String::make(pair->get<1>().trim_ascii()));
            }
        }
        if (newline.is_none()) break;
        remaining = remaining.split_at(*newline + usize(1)).get<1>();
    }
    return None();
}

auto read_text(ref<rstd::path::Path> path, ref<str> description) -> AndroidNdkResult<String> {
    auto contents = rstd::fs::read_to_string(path);
    if (contents.is_err()) {
        return android_io_failure<String>(
            rstd::format("read {}", description).as_str(), path, rstd::move(contents).unwrap_err());
    }
    return Ok(rstd::move(contents).unwrap());
}

auto require_directory(ref<rstd::path::Path> path, ref<str> description)
    -> AndroidNdkResult<empty> {
    auto metadata = rstd::fs::metadata(path);
    if (metadata.is_err()) {
        return android_io_failure<empty>(rstd::format("inspect {}", description).as_str(),
                                         path,
                                         rstd::move(metadata).unwrap_err());
    }
    if (! metadata->is_dir()) {
        return android_failure<empty>(
            rstd::format("{} '{}' is not a directory", description, path));
    }
    return Ok(empty {});
}

auto require_contained_file(ref<rstd::path::Path> root,
                            ref<rstd::path::Path> path,
                            ref<str>              description) -> AndroidNdkResult<empty> {
    auto metadata = rstd::fs::metadata(path);
    if (metadata.is_err()) {
        return android_io_failure<empty>(rstd::format("inspect {}", description).as_str(),
                                         path,
                                         rstd::move(metadata).unwrap_err());
    }
    if (! metadata->is_file()) {
        return android_failure<empty>(rstd::format("{} '{}' is not a file", description, path));
    }
    auto canonical = rstd::fs::canonicalize(path);
    if (canonical.is_err()) {
        return android_io_failure<empty>(rstd::format("resolve {}", description).as_str(),
                                         path,
                                         rstd::move(canonical).unwrap_err());
    }
    if (! canonical->as_path().starts_with(root)) {
        return android_failure<empty>(
            rstd::format("{} '{}' escapes Android NDK root '{}'", description, path, root));
    }
    return Ok(empty {});
}

auto append_file_identity(String& output, ref<str> label, ref<rstd::path::Path> path)
    -> AndroidNdkResult<empty> {
    auto metadata = rstd::fs::metadata(path);
    if (metadata.is_err()) {
        return android_io_failure<empty>(
            "inspect NDK identity file"_str, path, rstd::move(metadata).unwrap_err());
    }
    auto modified = metadata->modified();
    if (modified.is_err()) {
        return android_io_failure<empty>(
            "read NDK identity file timestamp"_str, path, rstd::move(modified).unwrap_err());
    }
    auto timestamp = modified->as_unix_time();
    output.push_str(
        rstd::format(
            "{}={}:{}:{}\n", label, metadata->size(), timestamp.seconds, timestamp.nanoseconds)
            .as_str());
    return Ok(empty {});
}

auto json_member(const Json& value, ref<str> key, ref<str> context) -> AndroidNdkResult<ref<Json>> {
    auto member = value.get(key);
    if (member.is_none()) {
        return android_failure<ref<Json>>(rstd::format("{} is missing '{}'", context, key));
    }
    return Ok(*member);
}

auto json_string(const Json& value, ref<str> key, ref<str> context) -> AndroidNdkResult<String> {
    auto member = rstd_try(json_member(value, key, context));
    auto text   = member->as_str();
    if (text.is_none() || text->is_empty()) {
        return android_failure<String>(
            rstd::format("{}.{} must be a non-empty string", context, key));
    }
    return Ok(String::make(*text));
}

auto json_bool(const Json& value, ref<str> key, ref<str> context) -> AndroidNdkResult<bool> {
    auto member = rstd_try(json_member(value, key, context));
    auto result = member->as_bool();
    if (result.is_none()) {
        return android_failure<bool>(rstd::format("{}.{} must be a bool", context, key));
    }
    return Ok(*result);
}

auto json_u32(const Json& value, ref<str> key, ref<str> context) -> AndroidNdkResult<u32> {
    auto member = rstd_try(json_member(value, key, context));
    auto result = member->as_u64();
    if (result.is_none() || *result > u64(u32::MAX.to_primitive())) {
        return android_failure<u32>(
            rstd::format("{}.{} must be a 32-bit unsigned integer", context, key));
    }
    return Ok(u32(result->to_primitive()));
}

auto parse_abis(ref<str> text, ref<rstd::path::Path> path) -> AndroidNdkResult<Vec<AndroidNdkAbi>> {
    auto parsed = rstd::json::from_str(text);
    if (parsed.is_err()) {
        return Err(AndroidNdkError::Json(PathBuf::from(path), rstd::move(parsed).unwrap_err()));
    }
    auto document = rstd::move(parsed).unwrap();
    auto object   = document.as_object();
    if (object.is_none())
        return android_failure<Vec<AndroidNdkAbi>>("NDK ABI metadata root must be an object"_str);
    auto result = Vec<AndroidNdkAbi>::make();
    auto keys   = (**object).keys();
    for (auto key : keys) {
        auto value   = document.get((*key).as_str());
        auto context = rstd::format("NDK ABI '{}'", (*key).as_str());
        if ((**value).as_object().is_none()) {
            return android_failure<Vec<AndroidNdkAbi>>(
                rstd::format("{} must be an object", context.as_str()));
        }
        result.push(AndroidNdkAbi {
            .name        = (*key).clone(),
            .triple      = rstd_try(json_string(**value, "triple"_str, context.as_str())),
            .llvm_triple = rstd_try(json_string(**value, "llvm_triple"_str, context.as_str())),
            .minimum_api = rstd_try(json_u32(**value, "min_os_version"_str, context.as_str())),
            .default_supported = rstd_try(json_bool(**value, "default"_str, context.as_str())),
            .deprecated        = rstd_try(json_bool(**value, "deprecated"_str, context.as_str())),
        });
    }
    if (result.is_empty())
        return android_failure<Vec<AndroidNdkAbi>>("NDK ABI metadata is empty"_str);
    rstd::slice_::sort_unstable_by(result.as_mut_slice().as_mut_ref(),
                                   [](const AndroidNdkAbi& left, const AndroidNdkAbi& right) {
                                       return left.name < right.name;
                                   });
    return Ok(rstd::move(result));
}

auto parse_platforms(ref<str> text, ref<rstd::path::Path> path)
    -> AndroidNdkResult<AndroidNdkPlatforms> {
    auto parsed = rstd::json::from_str(text);
    if (parsed.is_err()) {
        return Err(AndroidNdkError::Json(PathBuf::from(path), rstd::move(parsed).unwrap_err()));
    }
    auto document = rstd::move(parsed).unwrap();
    if (document.as_object().is_none()) {
        return android_failure<AndroidNdkPlatforms>(
            "NDK platform metadata root must be an object"_str);
    }
    auto minimum = rstd_try(json_u32(document, "min"_str, "NDK platform metadata"_str));
    auto maximum = rstd_try(json_u32(document, "max"_str, "NDK platform metadata"_str));
    if (minimum == u32 {} || minimum > maximum) {
        return android_failure<AndroidNdkPlatforms>(
            "NDK platform metadata has an invalid API range"_str);
    }
    return Ok(AndroidNdkPlatforms { .minimum = minimum, .maximum = maximum });
}

auto host_tag(const lito::system::HostInfo& host) -> AndroidNdkResult<String> {
    if (host.os.as_str() == "linux"_str && host.architecture.as_str() == "x86_64"_str) {
        return Ok(String::make("linux-x86_64"_str));
    }
    return android_failure<String>(rstd::format("Android NDK is not certified for host '{}-{}'",
                                                host.os.as_str(),
                                                host.architecture.as_str()));
}

auto official_clang_prefix(ref<str> abi) -> Option<ref<str>> {
    if (abi == "armeabi-v7a"_str) return Some("armv7a-linux-androideabi"_str);
    if (abi == "arm64-v8a"_str) return Some("aarch64-linux-android"_str);
    if (abi == "x86"_str) return Some("i686-linux-android"_str);
    if (abi == "x86_64"_str) return Some("x86_64-linux-android"_str);
    return None();
}

} // namespace lito

export namespace lito
{

auto parse_android_ndk_revision(ref<str> value) -> AndroidNdkResult<AndroidNdkRevision> {
    auto first = value.split_once("."_str);
    if (first.is_none()) {
        return android_failure<AndroidNdkRevision>(
            rstd::format("Android NDK revision '{}' must have three numeric components", value));
    }
    auto second = first->get<1>().split_once("."_str);
    if (second.is_none()) {
        return android_failure<AndroidNdkRevision>(
            rstd::format("Android NDK revision '{}' must have three numeric components", value));
    }
    if (second->get<1>().contains("."_str)) {
        return android_failure<AndroidNdkRevision>(
            rstd::format("Android NDK revision '{}' must have three numeric components", value));
    }
    return Ok(AndroidNdkRevision {
        .major = rstd_try(parse_decimal(first->get<0>(), "Android NDK major revision"_str)),
        .minor = rstd_try(parse_decimal(second->get<0>(), "Android NDK minor revision"_str)),
        .build = rstd_try(parse_decimal(second->get<1>(), "Android NDK build revision"_str)),
        .text  = String::make(value),
    });
}

auto open_android_ndk(ref<rstd::path::Path> root, const lito::system::HostInfo& host)
    -> AndroidNdkResult<AndroidNdkDistribution> {
    auto canonical = rstd::fs::canonicalize(root);
    if (canonical.is_err()) {
        return android_io_failure<AndroidNdkDistribution>(
            "resolve Android NDK root"_str, root, rstd::move(canonical).unwrap_err());
    }
    rstd_try(require_directory(canonical->as_path(), "Android NDK root"_str));
    auto source_properties = canonical->join(PathBuf::from("source.properties"_str).as_path());
    auto abis_path         = canonical->join(PathBuf::from("meta/abis.json"_str).as_path());
    auto platforms_path    = canonical->join(PathBuf::from("meta/platforms.json"_str).as_path());
    auto source_text =
        rstd_try(read_text(source_properties.as_path(), "NDK source properties"_str));
    auto abis_text = rstd_try(read_text(abis_path.as_path(), "NDK ABI metadata"_str));
    auto platforms_text =
        rstd_try(read_text(platforms_path.as_path(), "NDK platform metadata"_str));
    auto revision_text = property(source_text.as_str(), "Pkg.Revision"_str);
    if (revision_text.is_none()) {
        return android_failure<AndroidNdkDistribution>(
            "Android NDK source.properties is missing Pkg.Revision"_str);
    }
    auto release_name = property(source_text.as_str(), "Pkg.ReleaseName"_str);
    if (release_name.is_none()) release_name = Some(String::make("unknown"_str));
    auto revision = rstd_try(parse_android_ndk_revision(revision_text->as_str()));
    auto tag      = rstd_try(host_tag(host));
    auto prebuilt = canonical->join(PathBuf::from("toolchains/llvm/prebuilt"_str).as_path())
                        .join(PathBuf::from(tag.as_str()).as_path());
    auto bin      = prebuilt.join(PathBuf::from("bin"_str).as_path());
    auto paths    = AndroidNdkPaths {
        .cc       = bin.join(PathBuf::from("clang"_str).as_path()),
        .cxx      = bin.join(PathBuf::from("clang++"_str).as_path()),
        .linker   = bin.join(PathBuf::from("ld.lld"_str).as_path()),
        .archiver = bin.join(PathBuf::from("llvm-ar"_str).as_path()),
        .strip    = bin.join(PathBuf::from("llvm-strip"_str).as_path()),
        .readelf  = bin.join(PathBuf::from("llvm-readelf"_str).as_path()),
        .sysroot  = prebuilt.join(PathBuf::from("sysroot"_str).as_path()),
        .cmake_toolchain =
            canonical->join(PathBuf::from("build/cmake/android.toolchain.cmake"_str).as_path()),
    };
    rstd_try(require_directory(paths.sysroot.as_path(), "Android NDK sysroot"_str));
    rstd_try(require_contained_file(canonical->as_path(), paths.cc.as_path(), "NDK clang"_str));
    rstd_try(require_contained_file(canonical->as_path(), paths.cxx.as_path(), "NDK clang++"_str));
    rstd_try(
        require_contained_file(canonical->as_path(), paths.linker.as_path(), "NDK ld.lld"_str));
    rstd_try(
        require_contained_file(canonical->as_path(), paths.archiver.as_path(), "NDK llvm-ar"_str));
    rstd_try(
        require_contained_file(canonical->as_path(), paths.strip.as_path(), "NDK llvm-strip"_str));
    rstd_try(require_contained_file(
        canonical->as_path(), paths.readelf.as_path(), "NDK llvm-readelf"_str));
    rstd_try(require_contained_file(
        canonical->as_path(), paths.cmake_toolchain.as_path(), "Android CMake toolchain"_str));
    auto abis      = rstd_try(parse_abis(abis_text.as_str(), abis_path.as_path()));
    auto platforms = rstd_try(parse_platforms(platforms_text.as_str(), platforms_path.as_path()));
    auto identity_document =
        rstd::format("android-ndk-distribution-v2\nrevision={}\nhost={}\n{}\n{}",
                     revision.text.as_str(),
                     tag.as_str(),
                     abis_text.as_str(),
                     platforms_text.as_str());
    rstd_try(append_file_identity(identity_document, "clang"_str, paths.cc.as_path()));
    rstd_try(append_file_identity(identity_document, "clang++"_str, paths.cxx.as_path()));
    rstd_try(append_file_identity(identity_document, "lld"_str, paths.linker.as_path()));
    rstd_try(append_file_identity(identity_document, "llvm-ar"_str, paths.archiver.as_path()));
    rstd_try(append_file_identity(identity_document, "llvm-strip"_str, paths.strip.as_path()));
    rstd_try(append_file_identity(identity_document, "llvm-readelf"_str, paths.readelf.as_path()));
    auto cmake_text =
        rstd_try(read_text(paths.cmake_toolchain.as_path(), "Android CMake toolchain"_str));
    identity_document.push_str("cmake="_str);
    identity_document.push_str(lito::crypto::sha256_hex(cmake_text.as_str()).as_str());
    identity_document.push_ascii('\n');
    auto identity = lito::crypto::sha256_hex(identity_document.as_str());
    return Ok(AndroidNdkDistribution(rstd::move(canonical).unwrap(),
                                     rstd::move(revision),
                                     rstd::move(release_name).unwrap(),
                                     rstd::move(tag),
                                     rstd::move(paths),
                                     rstd::move(abis),
                                     platforms,
                                     rstd::move(identity)));
}

auto resolve_android_target(const AndroidNdkDistribution&             distribution,
                            const lito::config::AndroidTargetRequest& request)
    -> AndroidNdkResult<ResolvedAndroidTarget> {
    auto prefix = official_clang_prefix(request.abi.as_str());
    if (prefix.is_none()) {
        return android_failure<ResolvedAndroidTarget>(rstd::format(
            "Android ABI '{}' is not supported; expected armeabi-v7a, arm64-v8a, x86, or x86_64",
            request.abi.as_str()));
    }
    const AndroidNdkAbi* metadata = nullptr;
    for (const auto& abi : distribution.abis()) {
        if (abi.name == request.abi.as_str()) metadata = rstd::addressof(abi);
    }
    if (metadata == nullptr || ! metadata->default_supported || metadata->deprecated) {
        return android_failure<ResolvedAndroidTarget>(
            rstd::format("Android ABI '{}' is not enabled by NDK {}",
                         request.abi.as_str(),
                         distribution.revision().text.as_str()));
    }
    auto minimum = distribution.platforms().minimum;
    if (metadata->minimum_api > minimum) minimum = metadata->minimum_api;
    if (request.minimum_api < minimum || request.minimum_api > distribution.platforms().maximum) {
        return android_failure<ResolvedAndroidTarget>(
            rstd::format("Android ABI '{}' API {} is outside NDK {} supported range {}..{}",
                         request.abi.as_str(),
                         request.minimum_api,
                         distribution.revision().text.as_str(),
                         minimum,
                         distribution.platforms().maximum));
    }
    auto clang_target = rstd::format("{}{}", *prefix, request.minimum_api);
    auto target_info  = lito::system::parse_target_info(clang_target.as_str());
    if (target_info.is_err()) {
        return Err(AndroidNdkError::Platform(rstd::move(target_info).unwrap_err()));
    }
    return Ok(ResolvedAndroidTarget {
        .abi            = request.abi.clone(),
        .minimum_api    = request.minimum_api,
        .clang_target   = rstd::move(clang_target),
        .library_triple = metadata->triple.clone(),
        .target_info    = rstd::move(target_info).unwrap(),
        .sysroot        = distribution.paths().sysroot.clone(),
        .output_key = rstd::format("android-{}-api{}", request.abi.as_str(), request.minimum_api),
    });
}

auto resolve_android_toolchain(AndroidNdkDistribution                    distribution,
                               const lito::config::AndroidTargetRequest& request,
                               lito::config::StandardLibraryRuntime      runtime)
    -> AndroidNdkResult<ResolvedAndroidToolchain> {
    auto target   = rstd_try(resolve_android_target(distribution, request));
    auto platform = rstd::format("android-{}", target.minimum_api);
    auto cmake_stdlib =
        String::make(runtime == lito::config::StandardLibraryRuntime::Dynamic ? "c++_shared"_str
                                                                              : "c++_static"_str);
    auto cmake_toolchain = distribution.paths().cmake_toolchain.clone();
    auto identity        = rstd::format("android-cmake-v1\nndk={}\nabi={}\nplatform={}\nstdlib={}",
                                        distribution.identity(),
                                        target.abi.as_str(),
                                        platform.as_str(),
                                        cmake_stdlib.as_str());
    auto shared_runtime  = Option<AndroidRuntimeArtifact> {};
    if (runtime == lito::config::StandardLibraryRuntime::Dynamic) {
        auto runtime_path = distribution.paths()
                                .sysroot.join(PathBuf::from("usr/lib"_str).as_path())
                                .join(PathBuf::from(target.library_triple.as_str()).as_path())
                                .join(PathBuf::from("libc++_shared.so"_str).as_path());
        rstd_try(require_contained_file(
            distribution.root(), runtime_path.as_path(), "Android libc++ shared runtime"_str));
        auto runtime_identity = rstd::format("android-runtime-v1\nndk={}\nabi={}\npath={}\n",
                                             distribution.identity(),
                                             target.abi.as_str(),
                                             runtime_path.as_path());
        rstd_try(
            append_file_identity(runtime_identity, "libc++_shared.so"_str, runtime_path.as_path()));
        shared_runtime = Some(AndroidRuntimeArtifact {
            .name     = String::make("libc++_shared.so"_str),
            .path     = rstd::move(runtime_path),
            .identity = lito::crypto::sha256_hex(runtime_identity.as_str()),
        });
    }
    auto tools = distribution.toolchain_spec();
    return Ok(ResolvedAndroidToolchain {
        .distribution = rstd::move(distribution),
        .target       = rstd::move(target),
        .tools        = rstd::move(tools),
        .cmake =
            AndroidCmakeProjection {
                .toolchain_file   = rstd::move(cmake_toolchain),
                .abi              = request.abi.clone(),
                .platform         = rstd::move(platform),
                .standard_library = rstd::move(cmake_stdlib),
                .identity         = rstd::move(identity),
            },
        .shared_runtime = rstd::move(shared_runtime),
    });
}

auto resolve_android_build_platform(const lito::system::HostInfo&   host,
                                    const lito::system::TargetInfo& compiler_default,
                                    const ResolvedAndroidTarget&    target)
    -> lito::system::BuildPlatform {
    return lito::system::BuildPlatform {
        .host                = host.clone(),
        .compiler_default    = compiler_default.clone(),
        .effective_target    = target.target_info.clone(),
        .intent              = lito::system::BuildTargetIntent::ExplicitTarget,
        .cross               = true,
        .sysroot             = Some(target.sysroot.clone()),
        .android_abi         = Some(target.abi.clone()),
        .android_minimum_api = Some(u32(target.minimum_api.to_primitive())),
        .sdk_kind            = Some(String::make("android-ndk"_str)),
        .output_key          = target.output_key.clone(),
    };
}

} // namespace lito
