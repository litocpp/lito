module;
#include <rstd/macro.hpp>

export module lito.core:manifest.wire.target;

import rstd;
import rstd.serde;
import :manifest.wire;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::manifest::wire
{

enum class TargetKind
{
    Library,
    Module,
    Binary,
    Test,
    Benchmark
};

struct TargetSource {
    Option<String>             module;
    Option<Vec<String>>        sources;
    Option<Vec<String>>        source_groups;
    Vec<TargetSourceCondition> when;
};

struct TargetFields : TargetSource {
    String                        name;
    Option<String>                kind;
    Option<String>                archive;
    Option<String>                artifact;
    Vec<String>                   linker_options;
    bool                          link_stdlib { true };
    bool                          host_tool {};
    Option<Vec<RuntimeResource>>  resources;
    Option<Vec<TargetAttachment>> attach;
};

template<TargetKind Kind>
struct Target : TargetFields {};

using LibraryTarget   = Target<TargetKind::Library>;
using ModuleTarget    = Target<TargetKind::Module>;
using BinaryTarget    = Target<TargetKind::Binary>;
using TestTarget      = Target<TargetKind::Test>;
using BenchmarkTarget = Target<TargetKind::Benchmark>;

} // namespace lito::manifest::wire

export namespace rstd
{

template<lito::manifest::wire::TargetKind Kind>
struct Impl<serde::Deserialize, lito::manifest::wire::Target<Kind>> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<lito::manifest::wire::Target<Kind>, typename Deserializer::error_type> {
        using TargetKind   = lito::manifest::wire::TargetKind;
        auto module        = serde::OptionalField<String>("module"_str);
        auto sources       = serde::OptionalField<Vec<String>>("sources"_str);
        auto source_groups = serde::OptionalField<Vec<String>>("source-groups"_str);
        auto when =
            serde::DefaultedField<Vec<lito::manifest::wire::TargetSourceCondition>>("when"_str, {});
        auto read = [&](auto&... fields) {
            return serde::deserialize_record(deserializer,
                                             serde::UnknownFieldPolicy::Reject,
                                             module,
                                             sources,
                                             source_groups,
                                             when,
                                             fields...);
        };
        auto result = lito::manifest::wire::Target<Kind> {};
        if constexpr (Kind == TargetKind::Module) {
            rstd_try(read());
        } else {
            auto name = serde::RequiredField<String>("name"_str);
            if constexpr (Kind == TargetKind::Library) {
                auto kind           = serde::OptionalField<String>("kind"_str);
                auto archive        = serde::OptionalField<String>("archive"_str);
                auto artifact       = serde::OptionalField<String>("artifact"_str);
                auto linker_options = serde::DefaultedField<Vec<String>>("linker-options"_str, {});
                rstd_try(read(name, kind, archive, artifact, linker_options));
                result.kind           = kind.take();
                result.archive        = archive.take();
                result.artifact       = artifact.take();
                result.linker_options = linker_options.take();
            } else {
                auto link_stdlib = serde::DefaultedField<bool>("link-stdlib"_str, true);
                if constexpr (Kind == TargetKind::Binary) {
                    auto host_tool = serde::DefaultedField<bool>("host-tool"_str, false);
                    auto resources =
                        serde::OptionalField<Vec<lito::manifest::wire::RuntimeResource>>(
                            "resources"_str);
                    rstd_try(read(name, link_stdlib, host_tool, resources));
                    result.host_tool = host_tool.take();
                    result.resources = resources.take();
                } else if constexpr (Kind == TargetKind::Test || Kind == TargetKind::Benchmark) {
                    auto attach = serde::OptionalField<Vec<lito::manifest::wire::TargetAttachment>>(
                        "attach"_str);
                    rstd_try(read(name, link_stdlib, attach));
                    result.attach = attach.take();
                } else {
                    rstd_try(read(name, link_stdlib));
                }
                result.link_stdlib = link_stdlib.take();
            }
            result.name = rstd_try(name.take(deserializer));
        }
        result.module        = module.take();
        result.sources       = sources.take();
        result.source_groups = source_groups.take();
        result.when          = when.take();
        return Ok(rstd::move(result));
    }
};

} // namespace rstd
