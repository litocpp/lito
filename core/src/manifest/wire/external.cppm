module;
#include <rstd/macro.hpp>
export module lito.core:manifest.wire.external;
import rstd;
import rstd.serde;
import :manifest.wire;
import :manifest.wire.common;
using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::manifest::wire
{
struct CacheValue {
    String text;
};
} // namespace lito::manifest::wire
export namespace rstd
{
template<>
struct Impl<serde::Deserialize, lito::manifest::wire::CacheValue> {
    template<typename D>
    static auto deserialize(D& d)
        -> Result<lito::manifest::wire::CacheValue, typename D::error_type> {
        auto text = String::make();
        if (d.kind() == serde::ValueKind::String)
            text = rstd_try(d.deserialize_string());
        else if (d.kind() == serde::ValueKind::Boolean)
            text = String::make(rstd_try(d.deserialize_bool()) ? "ON"_str : "OFF"_str);
        else
            text = rstd::format("{}", rstd_try(serde::deserialize<i64>(d)));
        return Ok(lito::manifest::wire::CacheValue { .text = rstd::move(text) });
    }
};
} // namespace rstd

export namespace lito::manifest::wire
{
struct ArchiveVariant {
    Option<String> archive;
    Option<String> sha256;
};

} // namespace lito::manifest::wire
export namespace rstd
{
template<>
struct Impl<serde::Deserialize, lito::manifest::wire::ArchiveVariant> {
    template<typename D>
    static auto deserialize(D& d)
        -> Result<lito::manifest::wire::ArchiveVariant, typename D::error_type> {
        using namespace lito::manifest::wire;
        auto archive = serde::OptionalField<String>("archive"_str);
        auto sha256  = serde::OptionalField<String>("sha256"_str);
        rstd_try(serde::deserialize_record(d, serde::UnknownFieldPolicy::Reject, archive, sha256));
        auto result    = ArchiveVariant {};
        result.archive = archive.take();
        result.sha256  = sha256.take();

        if (true && result.archive.is_none())
            return Err(d.missing_field(archive.names().canonical()));
        if (true && result.sha256.is_none())
            return Err(d.missing_field(sha256.names().canonical()));
        return Ok(rstd::move(result));
    }
};
} // namespace rstd

export namespace lito::manifest::wire
{
struct CMakeTarget {
    Option<String>   name;
    Option<TextList> usage;
    Option<String>   visibility;
    Option<bool>     pub;
};

} // namespace lito::manifest::wire
export namespace rstd
{
template<>
struct Impl<serde::Deserialize, lito::manifest::wire::CMakeTarget> {
    template<typename D>
    static auto deserialize(D& d)
        -> Result<lito::manifest::wire::CMakeTarget, typename D::error_type> {
        using namespace lito::manifest::wire;
        auto name       = serde::OptionalField<String>("name"_str);
        auto usage      = serde::OptionalField<TextList>("usage"_str);
        auto visibility = serde::OptionalField<String>("visibility"_str);
        auto pub        = serde::OptionalField<bool>("pub"_str);
        rstd_try(serde::deserialize_record(
            d, serde::UnknownFieldPolicy::Reject, name, usage, visibility, pub));
        auto result       = CMakeTarget {};
        result.name       = name.take();
        result.usage      = usage.take();
        result.visibility = visibility.take();
        result.pub        = pub.take();

        if (true && result.name.is_none()) return Err(d.missing_field(name.names().canonical()));
        return Ok(rstd::move(result));
    }
};
} // namespace rstd

export namespace lito::manifest::wire
{
struct ExternalSourceFields {
    Option<String>                                              path;
    Option<String>                                              git;
    Option<String>                                              branch;
    Option<String>                                              tag;
    Option<String>                                              rev;
    Option<String>                                              commit;
    Option<String>                                              archive;
    Option<String>                                              sha256;
    Option<rstd::collections::BTreeMap<String, ArchiveVariant>> archives;
    Option<bool>                                                workspace;
};
template<bool Workspace>
struct ExternalSource : ExternalSourceFields {};
} // namespace lito::manifest::wire
export namespace rstd
{
template<bool Workspace>
struct Impl<serde::Deserialize, lito::manifest::wire::ExternalSource<Workspace>> {
    template<typename D>
    static auto deserialize(D& d)
        -> Result<lito::manifest::wire::ExternalSource<Workspace>, typename D::error_type> {
        using namespace lito::manifest::wire;
        auto path     = serde::OptionalField<String>("path"_str);
        auto git      = serde::OptionalField<String>("git"_str);
        auto branch   = serde::OptionalField<String>("branch"_str);
        auto tag      = serde::OptionalField<String>("tag"_str);
        auto rev      = serde::OptionalField<String>("rev"_str);
        auto commit   = serde::OptionalField<String>("commit"_str);
        auto archive  = serde::OptionalField<String>("archive"_str);
        auto sha256   = serde::OptionalField<String>("sha256"_str);
        auto archives = serde::OptionalField<rstd::collections::BTreeMap<String, ArchiveVariant>>(
            "archives"_str);
        auto workspace = serde::OptionalField<WorkspaceReference>("workspace"_str);
        rstd_try(serde::deserialize_record(d,
                                           serde::UnknownFieldPolicy::Reject,
                                           path,
                                           git,
                                           branch,
                                           tag,
                                           rev,
                                           commit,
                                           archive,
                                           sha256,
                                           archives,
                                           workspace));
        auto result          = ExternalSource<Workspace> {};
        result.path          = path.take();
        result.git           = git.take();
        result.branch        = branch.take();
        result.tag           = tag.take();
        result.rev           = rev.take();
        result.commit        = commit.take();
        result.archive       = archive.take();
        result.sha256        = sha256.take();
        result.archives      = archives.take();
        result.workspace     = workspace.take().is_some() ? Some(true) : Option<bool> {};
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

        if (result.archive.is_some() && inherited)
            return Err(d.unknown_field(archive.names().canonical()));

        if (result.sha256.is_some() && inherited)
            return Err(d.unknown_field(sha256.names().canonical()));

        if (result.archives.is_some() && inherited)
            return Err(d.unknown_field(archives.names().canonical()));

        if (result.workspace.is_some() && Workspace)
            return Err(d.unknown_field(workspace.names().canonical()));

        return Ok(rstd::move(result));
    }
};
} // namespace rstd

export namespace lito::manifest::wire
{
struct PkgConfigFields {
    Option<String>   module;
    Option<String>   version;
    Option<bool>     static_mode;
    Option<TextList> usage;
    Option<String>   visibility;
    Option<bool>     pub;
    Option<String>   condition;
    Option<bool>     workspace;
};
template<bool Workspace>
struct PkgConfig : PkgConfigFields {};
} // namespace lito::manifest::wire
export namespace rstd
{
template<bool Workspace>
struct Impl<serde::Deserialize, lito::manifest::wire::PkgConfig<Workspace>> {
    template<typename D>
    static auto deserialize(D& d)
        -> Result<lito::manifest::wire::PkgConfig<Workspace>, typename D::error_type> {
        using namespace lito::manifest::wire;
        auto module      = serde::OptionalField<String>("module"_str);
        auto version     = serde::OptionalField<String>("version"_str);
        auto static_mode = serde::OptionalField<bool>("static"_str);
        auto usage       = serde::OptionalField<TextList>("usage"_str);
        auto visibility  = serde::OptionalField<String>("visibility"_str);
        auto pub         = serde::OptionalField<bool>("pub"_str);
        auto condition   = serde::OptionalField<String>("condition"_str);
        auto workspace   = serde::OptionalField<WorkspaceReference>("workspace"_str);
        rstd_try(serde::deserialize_record(d,
                                           serde::UnknownFieldPolicy::Reject,
                                           module,
                                           version,
                                           static_mode,
                                           usage,
                                           visibility,
                                           pub,
                                           condition,
                                           workspace));
        auto result          = PkgConfig<Workspace> {};
        result.module        = module.take();
        result.version       = version.take();
        result.static_mode   = static_mode.take();
        result.usage         = usage.take();
        result.visibility    = visibility.take();
        result.pub           = pub.take();
        result.condition     = condition.take();
        result.workspace     = workspace.take().is_some() ? Some(true) : Option<bool> {};
        const auto inherited = result.workspace.is_some();
        if (result.module.is_some() && inherited)
            return Err(d.unknown_field(module.names().canonical()));
        if (! inherited && result.module.is_none())
            return Err(d.missing_field(module.names().canonical()));
        if (result.version.is_some() && inherited)
            return Err(d.unknown_field(version.names().canonical()));

        if (result.static_mode.is_some() && inherited)
            return Err(d.unknown_field(static_mode.names().canonical()));

        if (result.usage.is_some() && Workspace)
            return Err(d.unknown_field(usage.names().canonical()));

        if (result.visibility.is_some() && Workspace)
            return Err(d.unknown_field(visibility.names().canonical()));

        if (result.pub.is_some() && Workspace) return Err(d.unknown_field(pub.names().canonical()));

        if (result.condition.is_some() && Workspace)
            return Err(d.unknown_field(condition.names().canonical()));

        if (result.workspace.is_some() && Workspace)
            return Err(d.unknown_field(workspace.names().canonical()));

        return Ok(rstd::move(result));
    }
};
} // namespace rstd

export namespace lito::manifest::wire
{
struct CMakeExternalFields {
    Option<String>                                          package;
    Option<String>                                          source;
    Option<String>                                          adapter;
    Option<rstd::collections::BTreeMap<String, CacheValue>> cache;
    Option<String>                                          config_directory;
    Option<Vec<CMakeTarget>>                                targets;
    Option<Vec<String>>                                     components;
    Option<String>                                          condition;
    Option<Vec<CMakeHostTool>>                              host_tools;
    Option<bool>                                            workspace;
};
template<bool Workspace>
struct CMakeExternal : CMakeExternalFields {};
} // namespace lito::manifest::wire
export namespace rstd
{
template<bool Workspace>
struct Impl<serde::Deserialize, lito::manifest::wire::CMakeExternal<Workspace>> {
    template<typename D>
    static auto deserialize(D& d)
        -> Result<lito::manifest::wire::CMakeExternal<Workspace>, typename D::error_type> {
        using namespace lito::manifest::wire;
        auto package = serde::OptionalField<String>("package"_str);
        auto source  = serde::OptionalField<String>("source"_str);
        auto adapter = serde::OptionalField<String>("adapter"_str);
        auto cache =
            serde::OptionalField<rstd::collections::BTreeMap<String, CacheValue>>("cache"_str);
        auto config_directory = serde::OptionalField<String>("config-directory"_str);
        auto targets          = serde::OptionalField<Vec<CMakeTarget>>("targets"_str);
        auto components       = serde::OptionalField<Vec<String>>("components"_str);
        auto condition        = serde::OptionalField<String>("condition"_str);
        auto host_tools       = serde::OptionalField<Vec<CMakeHostTool>>("host-tools"_str);
        auto workspace        = serde::OptionalField<WorkspaceReference>("workspace"_str);
        rstd_try(serde::deserialize_record(d,
                                           serde::UnknownFieldPolicy::Reject,
                                           package,
                                           source,
                                           adapter,
                                           cache,
                                           config_directory,
                                           targets,
                                           components,
                                           condition,
                                           host_tools,
                                           workspace));
        auto result             = CMakeExternal<Workspace> {};
        result.package          = package.take();
        result.source           = source.take();
        result.adapter          = adapter.take();
        result.cache            = cache.take();
        result.config_directory = config_directory.take();
        result.targets          = targets.take();
        result.components       = components.take();
        result.condition        = condition.take();
        result.host_tools       = host_tools.take();
        result.workspace        = workspace.take().is_some() ? Some(true) : Option<bool> {};
        const auto inherited    = result.workspace.is_some();
        if (result.package.is_some() && inherited)
            return Err(d.unknown_field(package.names().canonical()));
        if (! inherited && result.package.is_none())
            return Err(d.missing_field(package.names().canonical()));
        if (result.source.is_some() && inherited)
            return Err(d.unknown_field(source.names().canonical()));

        if (result.adapter.is_some() && inherited)
            return Err(d.unknown_field(adapter.names().canonical()));

        if (result.cache.is_some() && inherited)
            return Err(d.unknown_field(cache.names().canonical()));

        if (result.config_directory.is_some() && inherited)
            return Err(d.unknown_field(config_directory.names().canonical()));

        if (result.targets.is_some() && Workspace)
            return Err(d.unknown_field(targets.names().canonical()));
        if (result.components.is_some() && inherited)
            return Err(d.unknown_field(components.names().canonical()));

        if (result.condition.is_some() && Workspace)
            return Err(d.unknown_field(condition.names().canonical()));

        if (result.host_tools.is_some() && inherited)
            return Err(d.unknown_field(host_tools.names().canonical()));

        if (result.workspace.is_some() && Workspace)
            return Err(d.unknown_field(workspace.names().canonical()));

        return Ok(rstd::move(result));
    }
};
} // namespace rstd

export namespace lito::manifest::wire
{
struct CargoExternalFields {
    Option<String>      source;
    Option<String>      package;
    Option<String>      manifest_path;
    Option<Vec<String>> features;
    Option<bool>        default_features;
    Option<String>      profile;
    Option<TextList>    usage;
    Option<String>      visibility;
    Option<bool>        pub;
    Option<String>      condition;
    Option<bool>        workspace;
};
template<bool Workspace>
struct CargoExternal : CargoExternalFields {};
} // namespace lito::manifest::wire
export namespace rstd
{
template<bool Workspace>
struct Impl<serde::Deserialize, lito::manifest::wire::CargoExternal<Workspace>> {
    template<typename D>
    static auto deserialize(D& d)
        -> Result<lito::manifest::wire::CargoExternal<Workspace>, typename D::error_type> {
        using namespace lito::manifest::wire;
        auto source           = serde::OptionalField<String>("source"_str);
        auto package          = serde::OptionalField<String>("package"_str);
        auto manifest_path    = serde::OptionalField<String>("manifest-path"_str);
        auto features         = serde::OptionalField<Vec<String>>("features"_str);
        auto default_features = serde::OptionalField<bool>("default-features"_str);
        auto profile          = serde::OptionalField<String>("profile"_str);
        auto usage            = serde::OptionalField<TextList>("usage"_str);
        auto visibility       = serde::OptionalField<String>("visibility"_str);
        auto pub              = serde::OptionalField<bool>("pub"_str);
        auto condition        = serde::OptionalField<String>("condition"_str);
        auto workspace        = serde::OptionalField<WorkspaceReference>("workspace"_str);
        rstd_try(serde::deserialize_record(d,
                                           serde::UnknownFieldPolicy::Reject,
                                           source,
                                           package,
                                           manifest_path,
                                           features,
                                           default_features,
                                           profile,
                                           usage,
                                           visibility,
                                           pub,
                                           condition,
                                           workspace));
        auto result             = CargoExternal<Workspace> {};
        result.source           = source.take();
        result.package          = package.take();
        result.manifest_path    = manifest_path.take();
        result.features         = features.take();
        result.default_features = default_features.take();
        result.profile          = profile.take();
        result.usage            = usage.take();
        result.visibility       = visibility.take();
        result.pub              = pub.take();
        result.condition        = condition.take();
        result.workspace        = workspace.take().is_some() ? Some(true) : Option<bool> {};
        const auto inherited    = result.workspace.is_some();
        if (result.source.is_some() && inherited)
            return Err(d.unknown_field(source.names().canonical()));
        if (! inherited && result.source.is_none())
            return Err(d.missing_field(source.names().canonical()));
        if (result.package.is_some() && inherited)
            return Err(d.unknown_field(package.names().canonical()));
        if (! inherited && result.package.is_none())
            return Err(d.missing_field(package.names().canonical()));
        if (result.manifest_path.is_some() && inherited)
            return Err(d.unknown_field(manifest_path.names().canonical()));

        if (result.features.is_some() && Workspace)
            return Err(d.unknown_field(features.names().canonical()));

        if (result.default_features.is_some() && Workspace)
            return Err(d.unknown_field(default_features.names().canonical()));

        if (result.profile.is_some() && Workspace)
            return Err(d.unknown_field(profile.names().canonical()));

        if (result.usage.is_some() && Workspace)
            return Err(d.unknown_field(usage.names().canonical()));

        if (result.visibility.is_some() && Workspace)
            return Err(d.unknown_field(visibility.names().canonical()));

        if (result.pub.is_some() && Workspace) return Err(d.unknown_field(pub.names().canonical()));

        if (result.condition.is_some() && Workspace)
            return Err(d.unknown_field(condition.names().canonical()));

        if (result.workspace.is_some() && Workspace)
            return Err(d.unknown_field(workspace.names().canonical()));

        return Ok(rstd::move(result));
    }
};
} // namespace rstd
