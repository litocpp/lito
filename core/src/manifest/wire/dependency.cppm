module;
#include <rstd/macro.hpp>
export module lito.core:manifest.wire.dependency;
import rstd;
import rstd.serde;
import :manifest.wire.common;
using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::manifest::wire
{
enum class DependencyMode
{
    Normal,
    Development,
    Runtime,
    Workspace
};
struct DependencyFields {
    Option<String>      path;
    Option<String>      git;
    Option<String>      branch;
    Option<String>      tag;
    Option<String>      rev;
    Option<String>      commit;
    Option<String>      builtin;
    Option<String>      version;
    Option<String>      registry;
    Option<bool>        workspace;
    Option<String>      visibility;
    Option<TextList>    usage;
    Option<bool>        pub;
    Option<Vec<String>> features;
    Option<bool>        default_features;
    Option<String>      condition;
};
template<DependencyMode Mode>
struct Dependency : DependencyFields {};
} // namespace lito::manifest::wire
export namespace rstd
{
template<lito::manifest::wire::DependencyMode Mode>
struct Impl<serde::Deserialize, lito::manifest::wire::Dependency<Mode>> {
    template<typename D>
    static auto deserialize(D& d)
        -> Result<lito::manifest::wire::Dependency<Mode>, typename D::error_type> {
        using namespace lito::manifest::wire;
        auto result = Dependency<Mode> {};
        if constexpr (Mode == DependencyMode::Normal || Mode == DependencyMode::Workspace) {
            if (d.kind() == serde::ValueKind::String) {
                result.version = Some(rstd_try(d.deserialize_string()));
                return Ok(rstd::move(result));
            }
        }
        auto path       = serde::OptionalField<String>("path"_str);
        auto git        = serde::OptionalField<String>("git"_str);
        auto branch     = serde::OptionalField<String>("branch"_str);
        auto tag        = serde::OptionalField<String>("tag"_str);
        auto rev        = serde::OptionalField<String>("rev"_str);
        auto commit     = serde::OptionalField<String>("commit"_str);
        auto builtin    = serde::OptionalField<String>("builtin"_str);
        auto version    = serde::OptionalField<String>("version"_str);
        auto registry   = serde::OptionalField<String>("registry"_str);
        auto workspace  = serde::OptionalField<WorkspaceReference>("workspace"_str);
        auto visibility = serde::OptionalField<String>("visibility"_str);
        auto usage      = serde::OptionalField<TextList>("usage"_str);
        auto pub        = serde::OptionalField<bool>("pub"_str);
        using FeatureInput =
            mtp::cond<Mode == DependencyMode::Runtime, serde::Ignored, Vec<String>>;
        using DefaultFeatureInput =
            mtp::cond<Mode == DependencyMode::Runtime, serde::Ignored, bool>;
        auto features         = serde::OptionalField<FeatureInput>("features"_str);
        auto default_features = serde::OptionalField<DefaultFeatureInput>("default-features"_str);
        auto condition        = serde::OptionalField<String>("condition"_str);
        rstd_try(serde::deserialize_record(d,
                                           serde::UnknownFieldPolicy::Reject,
                                           path,
                                           git,
                                           branch,
                                           tag,
                                           rev,
                                           commit,
                                           builtin,
                                           version,
                                           registry,
                                           workspace,
                                           visibility,
                                           usage,
                                           pub,
                                           features,
                                           default_features,
                                           condition));
        result.condition = condition.take();
        if (result.condition.is_some() && Mode == DependencyMode::Workspace)
            return Err(d.unknown_field(condition.names().canonical()));
        result.path               = path.take();
        result.git                = git.take();
        result.branch             = branch.take();
        result.tag                = tag.take();
        result.rev                = rev.take();
        result.commit             = commit.take();
        result.builtin            = builtin.take();
        result.version            = version.take();
        result.registry           = registry.take();
        result.workspace          = workspace.take().is_some() ? Some(true) : Option<bool> {};
        result.visibility         = visibility.take();
        result.usage              = usage.take();
        result.pub                = pub.take();
        auto       feature_values = features.take();
        auto       default_feature_value = default_features.take();
        const auto has_features          = feature_values.is_some();
        const auto has_default_features  = default_feature_value.is_some();
        if constexpr (Mode != DependencyMode::Runtime) {
            result.features         = rstd::move(feature_values);
            result.default_features = rstd::move(default_feature_value);
        }
        const auto inherited = result.workspace.is_some();
        if (result.path.is_some() && inherited)
            return Err(d.unknown_field(path.names().canonical()));
        if (result.git.is_some() && inherited) return Err(d.unknown_field(git.names().canonical()));
        if (result.branch.is_some() && inherited)
            return Err(d.unknown_field(branch.names().canonical()));
        if (result.tag.is_some() && inherited) return Err(d.unknown_field(tag.names().canonical()));
        if (result.rev.is_some() && inherited) return Err(d.unknown_field(rev.names().canonical()));
        if (result.commit.is_some() && inherited)
            return Err(d.unknown_field(commit.names().canonical()));
        if (result.builtin.is_some() && inherited)
            return Err(d.unknown_field(builtin.names().canonical()));
        if (result.version.is_some() && inherited)
            return Err(d.unknown_field(version.names().canonical()));
        if (result.registry.is_some() && inherited)
            return Err(d.unknown_field(registry.names().canonical()));
        if (result.workspace.is_some() && Mode == DependencyMode::Workspace)
            return Err(d.unknown_field(workspace.names().canonical()));
        if (result.visibility.is_some() && Mode != DependencyMode::Normal)
            return Err(d.unknown_field(visibility.names().canonical()));
        if (result.usage.is_some() &&
            ! (Mode == DependencyMode::Normal || Mode == DependencyMode::Development))
            return Err(d.unknown_field(usage.names().canonical()));
        if (result.pub.is_some() &&
            ! (Mode == DependencyMode::Normal || Mode == DependencyMode::Development))
            return Err(d.unknown_field(pub.names().canonical()));
        if (has_features && ! (Mode != DependencyMode::Workspace &&
                               ! (Mode == DependencyMode::Runtime && inherited)))
            return Err(d.unknown_field(features.names().canonical()));
        if (has_default_features && ! (Mode != DependencyMode::Workspace &&
                                       ! (Mode == DependencyMode::Runtime && inherited)))
            return Err(d.unknown_field(default_features.names().canonical()));
        return Ok(rstd::move(result));
    }
};
} // namespace rstd
