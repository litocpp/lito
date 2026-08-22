module;
#include <rstd/macro.hpp>

export module lito.core:manifest.wire;

import rstd;
import rstd.serde;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::manifest::wire
{

struct Script {
    Vec<String> supports;
};

struct BuildToolArchive {
    String url;
    String sha256;
};

struct BuildTool {
    String                                                version;
    String                                                executable;
    rstd::collections::BTreeMap<String, BuildToolArchive> archives;
};

using BuildTools = rstd::collections::BTreeMap<String, BuildTool>;

struct TargetSourceCondition {
    String      condition;
    Vec<String> source_groups;
};

struct TestAttachment {
    String      package;
    Vec<String> sources;
};

struct SourceGroup {
    Option<String> root;
    Option<String> external_source;
    Vec<String>    sources;
};

using SourceGroups = rstd::collections::BTreeMap<String, SourceGroup>;

struct RuntimeResource {
    String name;
    String root;
    String path;
};

struct CompileTestCase {
    String      name;
    String      source;
    String      outcome;
    Vec<String> options;
    Vec<String> diagnostic_contains;
    Vec<String> diagnostic_contains_any;
};

struct Feature {
    bool default_enabled {};
};

using Features = rstd::collections::BTreeMap<String, Feature>;

struct CMakeTarget {
    String name;
    String visibility;
};

struct CMakeHostTool {
    String name;
    String target;
};

struct BaseProfile {
    Option<bool> exceptions;
    Option<bool> rtti;
};

} // namespace lito::manifest::wire

export namespace rstd
{

template<>
struct Impl<serde::Deserialize, lito::manifest::wire::Script> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<lito::manifest::wire::Script, typename Deserializer::error_type> {
        auto supports = serde::RequiredField<Vec<String>>("supports"_str);
        rstd_try(
            serde::deserialize_record(deserializer, serde::UnknownFieldPolicy::Reject, supports));
        return Ok(lito::manifest::wire::Script {
            .supports = rstd_try(supports.take(deserializer)),
        });
    }
};

template<>
struct Impl<serde::Deserialize, lito::manifest::wire::BuildToolArchive> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<lito::manifest::wire::BuildToolArchive, typename Deserializer::error_type> {
        auto url    = serde::RequiredField<String>("url"_str);
        auto sha256 = serde::RequiredField<String>("sha256"_str);
        rstd_try(serde::deserialize_record(
            deserializer, serde::UnknownFieldPolicy::Reject, url, sha256));
        return Ok(lito::manifest::wire::BuildToolArchive {
            .url    = rstd_try(url.take(deserializer)),
            .sha256 = rstd_try(sha256.take(deserializer)),
        });
    }
};

template<>
struct Impl<serde::Deserialize, lito::manifest::wire::BuildTool> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<lito::manifest::wire::BuildTool, typename Deserializer::error_type> {
        auto version    = serde::RequiredField<String>("version"_str);
        auto executable = serde::RequiredField<String>("executable"_str);
        auto archives   = serde::RequiredField<
            rstd::collections::BTreeMap<String, lito::manifest::wire::BuildToolArchive>>(
            "archives"_str);
        rstd_try(serde::deserialize_record(
            deserializer, serde::UnknownFieldPolicy::Reject, version, executable, archives));
        return Ok(lito::manifest::wire::BuildTool {
            .version    = rstd_try(version.take(deserializer)),
            .executable = rstd_try(executable.take(deserializer)),
            .archives   = rstd_try(archives.take(deserializer)),
        });
    }
};

template<>
struct Impl<serde::Deserialize, lito::manifest::wire::TargetSourceCondition> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<lito::manifest::wire::TargetSourceCondition, typename Deserializer::error_type> {
        auto condition     = serde::RequiredField<String>("condition"_str);
        auto source_groups = serde::RequiredField<Vec<String>>("source-groups"_str);
        rstd_try(serde::deserialize_record(
            deserializer, serde::UnknownFieldPolicy::Reject, condition, source_groups));
        return Ok(lito::manifest::wire::TargetSourceCondition {
            .condition     = rstd_try(condition.take(deserializer)),
            .source_groups = rstd_try(source_groups.take(deserializer)),
        });
    }
};

template<>
struct Impl<serde::Deserialize, lito::manifest::wire::TestAttachment> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<lito::manifest::wire::TestAttachment, typename Deserializer::error_type> {
        auto package = serde::RequiredField<String>("package"_str);
        auto sources = serde::RequiredField<Vec<String>>("sources"_str);
        rstd_try(serde::deserialize_record(
            deserializer, serde::UnknownFieldPolicy::Reject, package, sources));
        return Ok(lito::manifest::wire::TestAttachment {
            .package = rstd_try(package.take(deserializer)),
            .sources = rstd_try(sources.take(deserializer)),
        });
    }
};

template<>
struct Impl<serde::Deserialize, lito::manifest::wire::SourceGroup> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<lito::manifest::wire::SourceGroup, typename Deserializer::error_type> {
        auto root            = serde::OptionalField<String>("root"_str);
        auto external_source = serde::OptionalField<String>("external-source"_str);
        auto sources         = serde::RequiredField<Vec<String>>("sources"_str);
        rstd_try(serde::deserialize_record(
            deserializer, serde::UnknownFieldPolicy::Reject, root, external_source, sources));
        return Ok(lito::manifest::wire::SourceGroup {
            .root            = root.take(),
            .external_source = external_source.take(),
            .sources         = rstd_try(sources.take(deserializer)),
        });
    }
};

template<>
struct Impl<serde::Deserialize, lito::manifest::wire::RuntimeResource> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<lito::manifest::wire::RuntimeResource, typename Deserializer::error_type> {
        auto name = serde::RequiredField<String>("name"_str);
        auto root = serde::RequiredField<String>("root"_str);
        auto path = serde::RequiredField<String>("path"_str);
        rstd_try(serde::deserialize_record(
            deserializer, serde::UnknownFieldPolicy::Reject, name, root, path));
        return Ok(lito::manifest::wire::RuntimeResource {
            .name = rstd_try(name.take(deserializer)),
            .root = rstd_try(root.take(deserializer)),
            .path = rstd_try(path.take(deserializer)),
        });
    }
};

template<>
struct Impl<serde::Deserialize, lito::manifest::wire::CompileTestCase> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<lito::manifest::wire::CompileTestCase, typename Deserializer::error_type> {
        auto name    = serde::RequiredField<String>("name"_str);
        auto source  = serde::RequiredField<String>("source"_str);
        auto outcome = serde::RequiredField<String>("outcome"_str);
        auto options = serde::DefaultedField<Vec<String>>("options"_str, Vec<String>::make());
        auto diagnostic_contains =
            serde::DefaultedField<Vec<String>>("diagnostic-contains"_str, Vec<String>::make());
        auto diagnostic_contains_any =
            serde::DefaultedField<Vec<String>>("diagnostic-contains-any"_str, Vec<String>::make());
        rstd_try(serde::deserialize_record(deserializer,
                                           serde::UnknownFieldPolicy::Reject,
                                           name,
                                           source,
                                           outcome,
                                           options,
                                           diagnostic_contains,
                                           diagnostic_contains_any));
        return Ok(lito::manifest::wire::CompileTestCase {
            .name                    = rstd_try(name.take(deserializer)),
            .source                  = rstd_try(source.take(deserializer)),
            .outcome                 = rstd_try(outcome.take(deserializer)),
            .options                 = options.take(),
            .diagnostic_contains     = diagnostic_contains.take(),
            .diagnostic_contains_any = diagnostic_contains_any.take(),
        });
    }
};

template<>
struct Impl<serde::Deserialize, lito::manifest::wire::Feature> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<lito::manifest::wire::Feature, typename Deserializer::error_type> {
        auto default_enabled = serde::DefaultedField<bool>("default"_str, false);
        rstd_try(serde::deserialize_record(
            deserializer, serde::UnknownFieldPolicy::Reject, default_enabled));
        return Ok(lito::manifest::wire::Feature { .default_enabled = default_enabled.take() });
    }
};

template<>
struct Impl<serde::Deserialize, lito::manifest::wire::CMakeTarget> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<lito::manifest::wire::CMakeTarget, typename Deserializer::error_type> {
        auto name       = serde::RequiredField<String>("name"_str);
        auto visibility = serde::RequiredField<String>("visibility"_str);
        rstd_try(serde::deserialize_record(
            deserializer, serde::UnknownFieldPolicy::Reject, name, visibility));
        return Ok(lito::manifest::wire::CMakeTarget {
            .name       = rstd_try(name.take(deserializer)),
            .visibility = rstd_try(visibility.take(deserializer)),
        });
    }
};

template<>
struct Impl<serde::Deserialize, lito::manifest::wire::CMakeHostTool> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<lito::manifest::wire::CMakeHostTool, typename Deserializer::error_type> {
        auto name   = serde::RequiredField<String>("name"_str);
        auto target = serde::RequiredField<String>("target"_str);
        rstd_try(serde::deserialize_record(
            deserializer, serde::UnknownFieldPolicy::Reject, name, target));
        return Ok(lito::manifest::wire::CMakeHostTool {
            .name   = rstd_try(name.take(deserializer)),
            .target = rstd_try(target.take(deserializer)),
        });
    }
};

template<>
struct Impl<serde::Deserialize, lito::manifest::wire::BaseProfile> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<lito::manifest::wire::BaseProfile, typename Deserializer::error_type> {
        auto exceptions = serde::OptionalField<bool>("exceptions"_str);
        auto rtti       = serde::OptionalField<bool>("rtti"_str);
        rstd_try(serde::deserialize_record(
            deserializer, serde::UnknownFieldPolicy::Reject, exceptions, rtti));
        return Ok(lito::manifest::wire::BaseProfile {
            .exceptions = exceptions.take(),
            .rtti       = rtti.take(),
        });
    }
};

} // namespace rstd
