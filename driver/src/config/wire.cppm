module;
#include <rstd/macro.hpp>

export module lito.driver:config.wire;

import rstd;
import rstd.serde;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::config::wire
{

struct Environment {
    Option<Vec<String>> append_path;
};

struct Sdk {
    String         kind;
    Option<String> version;
    Option<String> path;
};

struct Toolchain {
    Option<String> cc;
    Option<String> cxx;
    Option<String> ld;
    Option<String> ar;
    Option<String> standard_library;
    Option<String> standard_library_runtime;
    Option<Sdk>    sdk;
};

struct Cargo {
    Option<String> executable;
    Option<bool>   offline;
};

struct CMakeOverride {
    String source;
};

struct CMake {
    Option<String>                                             executable;
    Option<String>                                             generator;
    Option<Vec<String>>                                        search_path;
    Option<rstd::collections::BTreeMap<String, CMakeOverride>> overrides;
};

struct PkgConfig {
    Option<String>      executable;
    Option<Vec<String>> search_path;
    Option<Vec<String>> library_path;
    Option<String>      sysroot;
};

struct Tools {
    Option<Cargo>     cargo;
    Option<CMake>     cmake;
    Option<String>    tar;
    Option<String>    bsdtar;
    Option<String>    clang_format;
    Option<String>    curl;
    Option<String>    git;
    Option<PkgConfig> pkg_config;
    Option<String>    strip;
};

struct BuildTarget {
    String kind;
    String abi;
    i64    minimum_api {};
};

struct CBuild {
    Option<Vec<String>> options;
};

struct Build {
    Option<Vec<String>> options;
    Option<Vec<String>> linker_options;
    Option<CBuild>      c;
    Option<BuildTarget> target;
};

struct Patch {
    String path;
};

struct Lock {
    String path;
};

struct Install {
    String root;
};

struct Doc {
    Option<String> litodoc_path;
};

struct Document {
    Option<Environment>                                environment;
    Option<Tools>                                      tools;
    Option<Toolchain>                                  toolchain;
    Option<rstd::collections::BTreeMap<String, Patch>> patch;
    Option<Lock>                                       lock;
    Option<Install>                                    install;
    Option<Build>                                      build;
    Option<Doc>                                        doc;
};

struct HostDocument {
    Option<Environment>          environment;
    Option<Tools>                tools;
    Option<Toolchain>            toolchain;
    Option<rstd::serde::Ignored> patch;
    Option<rstd::serde::Ignored> lock;
    Option<rstd::serde::Ignored> install;
    Option<rstd::serde::Ignored> build;
    Option<rstd::serde::Ignored> doc;
};

} // namespace lito::config::wire

export namespace rstd
{

template<>
struct Impl<serde::Deserialize, lito::config::wire::Environment> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<lito::config::wire::Environment, typename Deserializer::error_type> {
        auto append_path = serde::OptionalField<Vec<String>>("append-path"_str);
        rstd_try(serde::deserialize_record(
            deserializer, serde::UnknownFieldPolicy::Reject, append_path));
        return Ok(lito::config::wire::Environment { .append_path = append_path.take() });
    }
};

template<>
struct Impl<serde::Deserialize, lito::config::wire::Sdk> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<lito::config::wire::Sdk, typename Deserializer::error_type> {
        auto kind    = serde::RequiredField<String>("kind"_str);
        auto version = serde::OptionalField<String>("version"_str);
        auto path    = serde::OptionalField<String>("path"_str);
        rstd_try(serde::deserialize_record(
            deserializer, serde::UnknownFieldPolicy::Reject, kind, version, path));
        return Ok(lito::config::wire::Sdk {
            .kind    = rstd_try(kind.take(deserializer)),
            .version = version.take(),
            .path    = path.take(),
        });
    }
};

template<>
struct Impl<serde::Deserialize, lito::config::wire::Toolchain> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<lito::config::wire::Toolchain, typename Deserializer::error_type> {
        auto cc      = serde::OptionalField<String>("cc"_str);
        auto cxx     = serde::OptionalField<String>("cxx"_str);
        auto ld      = serde::OptionalField<String>("ld"_str);
        auto ar      = serde::OptionalField<String>("ar"_str);
        auto stdlib  = serde::OptionalField<String>("stdlib"_str);
        auto runtime = serde::OptionalField<String>("stdlib-runtime"_str);
        auto sdk     = serde::OptionalField<lito::config::wire::Sdk>("sdk"_str);
        rstd_try(serde::deserialize_record(deserializer,
                                           serde::UnknownFieldPolicy::Reject,
                                           cc,
                                           cxx,
                                           ld,
                                           ar,
                                           stdlib,
                                           runtime,
                                           sdk));
        return Ok(lito::config::wire::Toolchain {
            .cc                       = cc.take(),
            .cxx                      = cxx.take(),
            .ld                       = ld.take(),
            .ar                       = ar.take(),
            .standard_library         = stdlib.take(),
            .standard_library_runtime = runtime.take(),
            .sdk                      = sdk.take(),
        });
    }
};

template<>
struct Impl<serde::Deserialize, lito::config::wire::CMakeOverride> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<lito::config::wire::CMakeOverride, typename Deserializer::error_type> {
        auto source = serde::RequiredField<String>("source"_str);
        rstd_try(
            serde::deserialize_record(deserializer, serde::UnknownFieldPolicy::Reject, source));
        return Ok(lito::config::wire::CMakeOverride {
            .source = rstd_try(source.take(deserializer)),
        });
    }
};

template<>
struct Impl<serde::Deserialize, lito::config::wire::Cargo> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<lito::config::wire::Cargo, typename Deserializer::error_type> {
        auto executable = serde::OptionalField<String>("executable"_str);
        auto offline    = serde::OptionalField<bool>("offline"_str);
        rstd_try(serde::deserialize_record(
            deserializer, serde::UnknownFieldPolicy::Reject, executable, offline));
        return Ok(lito::config::wire::Cargo {
            .executable = executable.take(),
            .offline    = offline.take(),
        });
    }
};

template<>
struct Impl<serde::Deserialize, lito::config::wire::CMake> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<lito::config::wire::CMake, typename Deserializer::error_type> {
        auto executable  = serde::OptionalField<String>("executable"_str);
        auto generator   = serde::OptionalField<String>("generator"_str);
        auto search_path = serde::OptionalField<Vec<String>>("search-path"_str);
        auto overrides   = serde::OptionalField<
            rstd::collections::BTreeMap<String, lito::config::wire::CMakeOverride>>(
            "overrides"_str);
        rstd_try(serde::deserialize_record(deserializer,
                                           serde::UnknownFieldPolicy::Reject,
                                           executable,
                                           generator,
                                           search_path,
                                           overrides));
        return Ok(lito::config::wire::CMake {
            .executable  = executable.take(),
            .generator   = generator.take(),
            .search_path = search_path.take(),
            .overrides   = overrides.take(),
        });
    }
};

template<>
struct Impl<serde::Deserialize, lito::config::wire::PkgConfig> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<lito::config::wire::PkgConfig, typename Deserializer::error_type> {
        auto executable   = serde::OptionalField<String>("executable"_str);
        auto search_path  = serde::OptionalField<Vec<String>>("search-path"_str);
        auto library_path = serde::OptionalField<Vec<String>>("library-path"_str);
        auto sysroot      = serde::OptionalField<String>("sysroot"_str);
        rstd_try(serde::deserialize_record(deserializer,
                                           serde::UnknownFieldPolicy::Reject,
                                           executable,
                                           search_path,
                                           library_path,
                                           sysroot));
        return Ok(lito::config::wire::PkgConfig {
            .executable   = executable.take(),
            .search_path  = search_path.take(),
            .library_path = library_path.take(),
            .sysroot      = sysroot.take(),
        });
    }
};

template<>
struct Impl<serde::Deserialize, lito::config::wire::Tools> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<lito::config::wire::Tools, typename Deserializer::error_type> {
        auto cmake        = serde::OptionalField<lito::config::wire::CMake>("cmake"_str);
        auto cargo        = serde::OptionalField<lito::config::wire::Cargo>("cargo"_str);
        auto tar          = serde::OptionalField<String>("tar"_str);
        auto bsdtar       = serde::OptionalField<String>("bsdtar"_str);
        auto clang_format = serde::OptionalField<String>("clang-format"_str);
        auto curl         = serde::OptionalField<String>("curl"_str);
        auto git          = serde::OptionalField<String>("git"_str);
        auto pkg_config   = serde::OptionalField<lito::config::wire::PkgConfig>("pkg-config"_str);
        auto strip        = serde::OptionalField<String>("strip"_str);
        rstd_try(serde::deserialize_record(deserializer,
                                           serde::UnknownFieldPolicy::Reject,
                                           cargo,
                                           cmake,
                                           tar,
                                           bsdtar,
                                           clang_format,
                                           curl,
                                           git,
                                           pkg_config,
                                           strip));
        return Ok(lito::config::wire::Tools {
            .cargo        = cargo.take(),
            .cmake        = cmake.take(),
            .tar          = tar.take(),
            .bsdtar       = bsdtar.take(),
            .clang_format = clang_format.take(),
            .curl         = curl.take(),
            .git          = git.take(),
            .pkg_config   = pkg_config.take(),
            .strip        = strip.take(),
        });
    }
};

template<>
struct Impl<serde::Deserialize, lito::config::wire::BuildTarget> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<lito::config::wire::BuildTarget, typename Deserializer::error_type> {
        auto kind        = serde::RequiredField<String>("kind"_str);
        auto abi         = serde::RequiredField<String>("abi"_str);
        auto minimum_api = serde::RequiredField<i64>("min-api"_str);
        rstd_try(serde::deserialize_record(
            deserializer, serde::UnknownFieldPolicy::Reject, kind, abi, minimum_api));
        return Ok(lito::config::wire::BuildTarget {
            .kind        = rstd_try(kind.take(deserializer)),
            .abi         = rstd_try(abi.take(deserializer)),
            .minimum_api = rstd_try(minimum_api.take(deserializer)),
        });
    }
};

template<>
struct Impl<serde::Deserialize, lito::config::wire::CBuild> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<lito::config::wire::CBuild, typename Deserializer::error_type> {
        auto options = serde::OptionalField<Vec<String>>("options"_str);
        rstd_try(
            serde::deserialize_record(deserializer, serde::UnknownFieldPolicy::Reject, options));
        return Ok(lito::config::wire::CBuild { .options = options.take() });
    }
};

template<>
struct Impl<serde::Deserialize, lito::config::wire::Build> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<lito::config::wire::Build, typename Deserializer::error_type> {
        auto options        = serde::OptionalField<Vec<String>>("options"_str);
        auto linker_options = serde::OptionalField<Vec<String>>("linker-options"_str);
        auto c              = serde::OptionalField<lito::config::wire::CBuild>("c"_str);
        auto target         = serde::OptionalField<lito::config::wire::BuildTarget>("target"_str);
        rstd_try(serde::deserialize_record(
            deserializer, serde::UnknownFieldPolicy::Reject, options, linker_options, c, target));
        return Ok(lito::config::wire::Build {
            .options        = options.take(),
            .linker_options = linker_options.take(),
            .c              = c.take(),
            .target         = target.take(),
        });
    }
};

template<>
struct Impl<serde::Deserialize, lito::config::wire::Patch> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<lito::config::wire::Patch, typename Deserializer::error_type> {
        auto path = serde::RequiredField<String>("path"_str);
        rstd_try(serde::deserialize_record(deserializer, serde::UnknownFieldPolicy::Reject, path));
        return Ok(lito::config::wire::Patch { .path = rstd_try(path.take(deserializer)) });
    }
};

template<>
struct Impl<serde::Deserialize, lito::config::wire::Lock> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<lito::config::wire::Lock, typename Deserializer::error_type> {
        auto path = serde::RequiredField<String>("path"_str);
        rstd_try(serde::deserialize_record(deserializer, serde::UnknownFieldPolicy::Reject, path));
        return Ok(lito::config::wire::Lock { .path = rstd_try(path.take(deserializer)) });
    }
};

template<>
struct Impl<serde::Deserialize, lito::config::wire::Install> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<lito::config::wire::Install, typename Deserializer::error_type> {
        auto root = serde::RequiredField<String>("root"_str);
        rstd_try(serde::deserialize_record(deserializer, serde::UnknownFieldPolicy::Reject, root));
        return Ok(lito::config::wire::Install { .root = rstd_try(root.take(deserializer)) });
    }
};

template<>
struct Impl<serde::Deserialize, lito::config::wire::Doc> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<lito::config::wire::Doc, typename Deserializer::error_type> {
        auto path = serde::OptionalField<String>("litodoc-path"_str);
        rstd_try(serde::deserialize_record(deserializer, serde::UnknownFieldPolicy::Reject, path));
        return Ok(lito::config::wire::Doc { .litodoc_path = path.take() });
    }
};

template<>
struct Impl<serde::Deserialize, lito::config::wire::Document> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<lito::config::wire::Document, typename Deserializer::error_type> {
        auto environment = serde::OptionalField<lito::config::wire::Environment>("environment"_str);
        auto tools       = serde::OptionalField<lito::config::wire::Tools>("tools"_str);
        auto toolchain   = serde::OptionalField<lito::config::wire::Toolchain>("toolchain"_str);
        auto patch =
            serde::OptionalField<rstd::collections::BTreeMap<String, lito::config::wire::Patch>>(
                "patch"_str);
        auto lock    = serde::OptionalField<lito::config::wire::Lock>("lock"_str);
        auto install = serde::OptionalField<lito::config::wire::Install>("install"_str);
        auto build   = serde::OptionalField<lito::config::wire::Build>("build"_str);
        auto doc     = serde::OptionalField<lito::config::wire::Doc>("doc"_str);
        rstd_try(serde::deserialize_record(deserializer,
                                           serde::UnknownFieldPolicy::Reject,
                                           environment,
                                           tools,
                                           toolchain,
                                           patch,
                                           lock,
                                           install,
                                           build,
                                           doc));
        return Ok(lito::config::wire::Document {
            .environment = environment.take(),
            .tools       = tools.take(),
            .toolchain   = toolchain.take(),
            .patch       = patch.take(),
            .lock        = lock.take(),
            .install     = install.take(),
            .build       = build.take(),
            .doc         = doc.take(),
        });
    }
};

template<>
struct Impl<serde::Deserialize, lito::config::wire::HostDocument> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<lito::config::wire::HostDocument, typename Deserializer::error_type> {
        auto environment = serde::OptionalField<lito::config::wire::Environment>("environment"_str);
        auto tools       = serde::OptionalField<lito::config::wire::Tools>("tools"_str);
        auto toolchain   = serde::OptionalField<lito::config::wire::Toolchain>("toolchain"_str);
        auto patch       = serde::OptionalField<serde::Ignored>("patch"_str);
        auto lock        = serde::OptionalField<serde::Ignored>("lock"_str);
        auto install     = serde::OptionalField<serde::Ignored>("install"_str);
        auto build       = serde::OptionalField<serde::Ignored>("build"_str);
        auto doc         = serde::OptionalField<serde::Ignored>("doc"_str);
        rstd_try(serde::deserialize_record(deserializer,
                                           serde::UnknownFieldPolicy::Reject,
                                           environment,
                                           tools,
                                           toolchain,
                                           patch,
                                           lock,
                                           install,
                                           build,
                                           doc));
        return Ok(lito::config::wire::HostDocument {
            .environment = environment.take(),
            .tools       = tools.take(),
            .toolchain   = toolchain.take(),
            .patch       = patch.take(),
            .lock        = lock.take(),
            .install     = install.take(),
            .build       = build.take(),
            .doc         = doc.take(),
        });
    }
};

} // namespace rstd
