module;
#include <rstd/macro.hpp>

export module lito.package_embed;

import rstd;
import lito.pack;

using namespace rstd::prelude;
using namespace rstd::literals;
using PathBuf = rstd::path::PathBuf;

namespace
{

struct Options {
    PathBuf        root;
    Option<String> package;
    String         registry;
    PathBuf        archive;
    PathBuf        descriptor;
    PathBuf        depfile;
};

auto usage_failure(ref<str> message) -> Result<Options, String> {
    return Err(rstd::format("{}\nusage: lito-package-embed --root <path> [--package <name>] "
                            "--registry <identity> --archive <path> --descriptor <path> "
                            "--depfile <path>",
                            message));
}

auto parse_options() -> Result<Options, String> {
    auto root       = Option<PathBuf> {};
    auto package    = Option<String> {};
    auto registry   = Option<String> {};
    auto archive    = Option<PathBuf> {};
    auto descriptor = Option<PathBuf> {};
    auto depfile    = Option<PathBuf> {};
    auto arguments  = rstd::env::args();
    (void)arguments.next();
    while (auto argument = arguments.next()) {
        auto value = arguments.next();
        if (value.is_none()) {
            return usage_failure(
                rstd::format("option '{}' requires a value", argument->as_str()).as_str());
        }
        const auto repeated = [](const auto& current, ref<str> name) -> Option<String> {
            if (current.is_some()) return Some(rstd::format("option '{}' is repeated", name));
            return None();
        };
        if (argument->as_str() == "--root"_str) {
            if (auto error = repeated(root, "--root"_str); error.is_some())
                return usage_failure(error->as_str());
            root = Some(PathBuf::from(rstd::move(value).unwrap()));
        } else if (argument->as_str() == "--package"_str) {
            if (auto error = repeated(package, "--package"_str); error.is_some())
                return usage_failure(error->as_str());
            package = Some(rstd::move(value).unwrap());
        } else if (argument->as_str() == "--registry"_str) {
            if (auto error = repeated(registry, "--registry"_str); error.is_some())
                return usage_failure(error->as_str());
            registry = Some(rstd::move(value).unwrap());
        } else if (argument->as_str() == "--archive"_str) {
            if (auto error = repeated(archive, "--archive"_str); error.is_some())
                return usage_failure(error->as_str());
            archive = Some(PathBuf::from(rstd::move(value).unwrap()));
        } else if (argument->as_str() == "--descriptor"_str) {
            if (auto error = repeated(descriptor, "--descriptor"_str); error.is_some())
                return usage_failure(error->as_str());
            descriptor = Some(PathBuf::from(rstd::move(value).unwrap()));
        } else if (argument->as_str() == "--depfile"_str) {
            if (auto error = repeated(depfile, "--depfile"_str); error.is_some())
                return usage_failure(error->as_str());
            depfile = Some(PathBuf::from(rstd::move(value).unwrap()));
        } else {
            return usage_failure(rstd::format("unknown option '{}'", argument->as_str()).as_str());
        }
    }
    if (root.is_none() || registry.is_none() || archive.is_none() || descriptor.is_none() ||
        depfile.is_none()) {
        return usage_failure("required options are missing"_str);
    }
    return Ok(Options {
        .root       = rstd::move(root).unwrap(),
        .package    = rstd::move(package),
        .registry   = rstd::move(registry).unwrap(),
        .archive    = rstd::move(archive).unwrap(),
        .descriptor = rstd::move(descriptor).unwrap(),
        .depfile    = rstd::move(depfile).unwrap(),
    });
}

auto depfile_path(ref<rstd::path::Path> path) -> Result<String, String> {
    auto text = path.to_str();
    if (text.is_none()) {
        return Err(rstd::format("depfile path '{}' is not valid UTF-8", path));
    }
    auto result = String::make();
    for (auto byte : text->as_bytes()) {
        if (byte == u8('$')) {
            result.push_str("$$"_str);
        } else {
            if (byte == u8(' ') || byte == u8('#') || byte == u8(':') || byte == u8('\\')) {
                result.push_ascii(u8('\\'));
            }
            result.push_ascii(byte);
        }
    }
    return Ok(rstd::move(result));
}

auto make_depfile(const Options& options, const lito::PackPackageSummary& package)
    -> Result<String, String> {
    auto dependencies = rstd::collections::BTreeMap<String, empty>::make();
    for (const auto& file : package.files) {
        auto path = options.root.join(PathBuf::from(file.as_str()).as_path());
        dependencies.insert(rstd_try(depfile_path(path.as_path())), empty {});
    }
    for (const auto& directory : package.directories) {
        dependencies.insert(rstd_try(depfile_path(directory.as_path())), empty {});
    }
    auto result = rstd_try(depfile_path(options.archive.as_path()));
    result.push_str(":\\\n"_str);
    auto paths = dependencies.keys();
    auto index = usize {};
    for (auto path : paths) {
        result.push_str("  "_str);
        result.push_str((*path).as_str());
        if (index + usize(1) != paths.len()) result.push_str(" \\\n"_str);
        ++index;
    }
    result.push_ascii(u8('\n'));
    return Ok(rstd::move(result));
}

auto write_output(ref<rstd::path::Path> path, ref<str> contents) -> Result<empty, String> {
    auto parent = path.parent();
    if (parent.is_some()) {
        auto created = rstd::fs::create_dir_all(*parent);
        if (created.is_err()) {
            return Err(rstd::format("cannot create output directory '{}': {}",
                                    *parent,
                                    rstd::move(created).unwrap_err()));
        }
    }
    auto written = rstd::fs::write_atomic(path, contents.as_bytes());
    if (written.is_err()) {
        return Err(
            rstd::format("cannot write output '{}': {}", path, rstd::move(written).unwrap_err()));
    }
    return Ok(empty {});
}

auto pack_error_text(const lito::package::PackageError& error) -> String {
    auto result = rstd::format("{}", error);
    auto source = as<rstd::error::Error>(error).source();
    while (source.is_some()) {
        result.push_str(rstd::format("\n  caused by: {}", *source).as_str());
        source = (*source)->source();
    }
    return result;
}

auto run(Options options) -> Result<empty, String> {
    auto registry = lito::registry::RegistryId::parse(options.registry.as_str());
    if (registry.is_err()) {
        return Err(rstd::format("invalid Registry identity '{}': {}",
                                options.registry.as_str(),
                                rstd::move(registry).unwrap_err()));
    }
    auto packed = lito::pack_package(lito::PackPackageRequest {
        .root    = options.root.clone(),
        .package = as<Clone>(options.package).clone(),
        .output  = Some(options.archive.clone()),
        .registry =
            lito::PackageRegistryContext {
                .owner = rstd::move(registry).unwrap(),
            },
    });
    if (packed.is_err()) {
        auto error = rstd::move(packed).unwrap_err();
        return Err(pack_error_text(error));
    }
    auto package = rstd::move(packed).unwrap();
    if (package.artifact.is_none())
        return Err(String::make("package archive was not produced"_str));
    auto descriptor = lito::registry::serialize_verified_publish_candidate(*package.artifact);
    descriptor.push_ascii(u8('\n'));
    auto depfile = rstd_try(make_depfile(options, package));
    rstd_try(write_output(options.descriptor.as_path(), descriptor.as_str()));
    rstd_try(write_output(options.depfile.as_path(), depfile.as_str()));
    return Ok(empty {});
}

} // namespace

extern "C++" int main() {
    auto options = parse_options();
    if (options.is_err()) {
        rstd::io::eprintln("lito-package-embed: {}", rstd::move(options).unwrap_err());
        return 2;
    }
    auto result = run(rstd::move(options).unwrap());
    if (result.is_err()) {
        rstd::io::eprintln("lito-package-embed: {}", rstd::move(result).unwrap_err());
        return 1;
    }
    return 0;
}
