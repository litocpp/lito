module;
#include <rstd/macro.hpp>

export module lito.core:manifest.wire.common;
import rstd;
import rstd.serde;
using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::manifest::wire
{
struct WorkspaceReference {};
struct TextList {
    Vec<String> values;
    bool        scalar {};
};
struct ReadmeValue {
    Option<String> path;
    bool           enabled { true };
};
template<typename T>
struct Inherited {
    Option<T> value;
    bool      workspace {};
};
struct IncludeDirectory {
    String         path;
    Option<String> root;
    Option<String> external_source;
};
} // namespace lito::manifest::wire
export namespace rstd
{
template<>
struct Impl<serde::Deserialize, lito::manifest::wire::WorkspaceReference> {
    template<typename D>
    static auto deserialize(D& d)
        -> Result<lito::manifest::wire::WorkspaceReference, typename D::error_type> {
        if (! rstd_try(d.deserialize_bool())) return Err(d.invalid_value("must be true"_str));
        return Ok(lito::manifest::wire::WorkspaceReference {});
    }
};
template<>
struct Impl<serde::Deserialize, lito::manifest::wire::TextList> {
    template<typename D>
    static auto deserialize(D& d)
        -> Result<lito::manifest::wire::TextList, typename D::error_type> {
        auto result   = lito::manifest::wire::TextList {};
        result.scalar = d.kind() == serde::ValueKind::String;
        if (result.scalar)
            result.values.push(rstd_try(d.deserialize_string()));
        else
            result.values = rstd_try(serde::deserialize<Vec<String>>(d));
        return Ok(rstd::move(result));
    }
};
template<>
struct Impl<serde::Deserialize, lito::manifest::wire::ReadmeValue> {
    template<typename D>
    static auto deserialize(D& d)
        -> Result<lito::manifest::wire::ReadmeValue, typename D::error_type> {
        if (d.kind() == serde::ValueKind::String)
            return Ok(lito::manifest::wire::ReadmeValue {
                .path = Some(rstd_try(d.deserialize_string())) });
        return Ok(lito::manifest::wire::ReadmeValue { .enabled = rstd_try(d.deserialize_bool()) });
    }
};
template<typename T>
struct Impl<serde::Deserialize, lito::manifest::wire::Inherited<T>> {
    template<typename D>
    static auto deserialize(D& d)
        -> Result<lito::manifest::wire::Inherited<T>, typename D::error_type> {
        if (d.kind() != serde::ValueKind::Map)
            return Ok(lito::manifest::wire::Inherited<T> {
                .value = Some(rstd_try(serde::deserialize<T>(d))) });
        auto workspace =
            serde::RequiredField<lito::manifest::wire::WorkspaceReference>("workspace"_str);
        rstd_try(serde::deserialize_record(d, serde::UnknownFieldPolicy::Reject, workspace));
        rstd_try(workspace.take(d));
        return Ok(lito::manifest::wire::Inherited<T> { .workspace = true });
    }
};
template<>
struct Impl<serde::Deserialize, lito::manifest::wire::IncludeDirectory> {
    template<typename D>
    static auto deserialize(D& d)
        -> Result<lito::manifest::wire::IncludeDirectory, typename D::error_type> {
        if (d.kind() == serde::ValueKind::String)
            return Ok(lito::manifest::wire::IncludeDirectory {
                .path = rstd_try(d.deserialize_string()) });
        auto path            = serde::RequiredField<String>("path"_str);
        auto root            = serde::OptionalField<String>("root"_str);
        auto external_source = serde::OptionalField<String>("external-source"_str);
        rstd_try(serde::deserialize_record(
            d, serde::UnknownFieldPolicy::Reject, path, root, external_source));
        return Ok(
            lito::manifest::wire::IncludeDirectory { .path            = rstd_try(path.take(d)),
                                                     .root            = root.take(),
                                                     .external_source = external_source.take() });
    }
};
} // namespace rstd

export namespace lito::manifest::wire
{
struct Publish {
    Option<Vec<String>> include;
    Option<Vec<String>> exclude;
};
} // namespace lito::manifest::wire
export namespace rstd
{
template<>
struct Impl<serde::Deserialize, lito::manifest::wire::Publish> {
    template<typename D>
    static auto deserialize(D& d) -> Result<lito::manifest::wire::Publish, typename D::error_type> {
        using namespace lito::manifest::wire;
        auto include = serde::OptionalField<Vec<String>>("include"_str);
        auto exclude = serde::OptionalField<Vec<String>>("exclude"_str);
        rstd_try(serde::deserialize_record(d, serde::UnknownFieldPolicy::Reject, include, exclude));
        return Ok(lito::manifest::wire::Publish {
            .include = include.take(),
            .exclude = exclude.take(),
        });
    }
};
} // namespace rstd

export namespace lito::manifest::wire
{
struct Predicate {
    Option<TextList> family;
    Option<TextList> os;
    Option<TextList> not_family;
    Option<TextList> not_os;
};
} // namespace lito::manifest::wire
export namespace rstd
{
template<>
struct Impl<serde::Deserialize, lito::manifest::wire::Predicate> {
    template<typename D>
    static auto deserialize(D& d)
        -> Result<lito::manifest::wire::Predicate, typename D::error_type> {
        using namespace lito::manifest::wire;
        auto family     = serde::OptionalField<TextList>("family"_str);
        auto os         = serde::OptionalField<TextList>("os"_str);
        auto not_family = serde::OptionalField<TextList>("not-family"_str);
        auto not_os     = serde::OptionalField<TextList>("not-os"_str);
        rstd_try(serde::deserialize_record(
            d, serde::UnknownFieldPolicy::Reject, family, os, not_family, not_os));
        return Ok(lito::manifest::wire::Predicate {
            .family     = family.take(),
            .os         = os.take(),
            .not_family = not_family.take(),
            .not_os     = not_os.take(),
        });
    }
};
} // namespace rstd

export namespace lito::manifest::wire
{
struct Usage {
    Option<Vec<IncludeDirectory>> public_includes;
    Option<Vec<IncludeDirectory>> private_includes;
    Option<Vec<String>>           public_definitions;
    Option<Vec<String>>           private_definitions;
    Option<Vec<String>>           options;
    Option<Vec<String>>           linker_options;
    Option<bool>                  threads;
    Option<Vec<String>>           system_libraries;
    Option<Vec<String>>           frameworks;
};
} // namespace lito::manifest::wire
export namespace rstd
{
template<>
struct Impl<serde::Deserialize, lito::manifest::wire::Usage> {
    template<typename D>
    static auto deserialize(D& d) -> Result<lito::manifest::wire::Usage, typename D::error_type> {
        using namespace lito::manifest::wire;
        auto public_includes =
            serde::OptionalField<Vec<IncludeDirectory>>("public-include-directories"_str);
        auto private_includes =
            serde::OptionalField<Vec<IncludeDirectory>>("private-include-directories"_str);
        auto public_definitions  = serde::OptionalField<Vec<String>>("public-definitions"_str);
        auto private_definitions = serde::OptionalField<Vec<String>>("private-definitions"_str);
        auto options             = serde::OptionalField<Vec<String>>("options"_str);
        auto linker_options      = serde::OptionalField<Vec<String>>("linker-options"_str);
        auto threads             = serde::OptionalField<bool>("threads"_str);
        auto system_libraries    = serde::OptionalField<Vec<String>>("system-libraries"_str);
        auto frameworks          = serde::OptionalField<Vec<String>>("frameworks"_str);
        rstd_try(serde::deserialize_record(d,
                                           serde::UnknownFieldPolicy::Reject,
                                           public_includes,
                                           private_includes,
                                           public_definitions,
                                           private_definitions,
                                           options,
                                           linker_options,
                                           threads,
                                           system_libraries,
                                           frameworks));
        return Ok(lito::manifest::wire::Usage {
            .public_includes     = public_includes.take(),
            .private_includes    = private_includes.take(),
            .public_definitions  = public_definitions.take(),
            .private_definitions = private_definitions.take(),
            .options             = options.take(),
            .linker_options      = linker_options.take(),
            .threads             = threads.take(),
            .system_libraries    = system_libraries.take(),
            .frameworks          = frameworks.take(),
        });
    }
};
} // namespace rstd

export namespace lito::manifest::wire
{
struct Condition {
    String condition;
    Usage  usage;
};
} // namespace lito::manifest::wire
export namespace rstd
{
template<>
struct Impl<serde::Deserialize, lito::manifest::wire::Condition> {
    template<typename D>
    static auto deserialize(D& d)
        -> Result<lito::manifest::wire::Condition, typename D::error_type> {
        using namespace lito::manifest::wire;
        auto condition = serde::RequiredField<String>("condition"_str);
        auto usage     = serde::RequiredField<Usage>("usage"_str);
        rstd_try(serde::deserialize_record(d, serde::UnknownFieldPolicy::Reject, condition, usage));
        return Ok(lito::manifest::wire::Condition {
            .condition = rstd_try(condition.take(d)),
            .usage     = rstd_try(usage.take(d)),
        });
    }
};
} // namespace rstd

export namespace lito::manifest::wire
{
struct Package {
    String                         name;
    bool                           autoexamples { true };
    Option<Inherited<String>>      version;
    Option<String>                 source_root;
    Option<Predicate>              target;
    Option<Inherited<String>>      license;
    Option<Inherited<Vec<String>>> authors;
    Option<Inherited<String>>      description;
    Option<Inherited<ReadmeValue>> readme;
    Option<Inherited<String>>      repository;
    Option<Inherited<String>>      documentation;
    Option<String>                 standard;
    Option<Publish>                publish;
};
template<bool Embedded>
struct PackageInput {
    Package value;
};
} // namespace lito::manifest::wire
export namespace rstd
{
template<bool Embedded>
struct Impl<serde::Deserialize, lito::manifest::wire::PackageInput<Embedded>> {
    template<typename D>
    static auto deserialize(D& d)
        -> Result<lito::manifest::wire::PackageInput<Embedded>, typename D::error_type> {
        using namespace lito::manifest::wire;
        auto name             = serde::RequiredField<String>("name"_str);
        auto autoexamples     = serde::DefaultedField<bool>("autoexamples"_str, true);
        auto version          = serde::OptionalField<Inherited<String>>("version"_str);
        using SourceRootInput = mtp::cond<Embedded, serde::Ignored, String>;
        auto source_root      = serde::OptionalField<SourceRootInput>("source-root"_str);
        auto target           = serde::OptionalField<Predicate>("target"_str);
        auto license          = serde::OptionalField<Inherited<String>>("license"_str);
        auto authors          = serde::OptionalField<Inherited<Vec<String>>>("authors"_str);
        auto description      = serde::OptionalField<Inherited<String>>("description"_str);
        auto readme           = serde::OptionalField<Inherited<ReadmeValue>>("readme"_str);
        auto repository       = serde::OptionalField<Inherited<String>>("repository"_str);
        auto documentation    = serde::OptionalField<Inherited<String>>("documentation"_str);
        auto standard         = serde::OptionalField<String>("standard"_str);
        auto publish          = serde::OptionalField<Publish>("publish"_str);
        rstd_try(serde::deserialize_record(d,
                                           serde::UnknownFieldPolicy::Reject,
                                           name,
                                           autoexamples,
                                           version,
                                           source_root,
                                           target,
                                           license,
                                           authors,
                                           description,
                                           readme,
                                           repository,
                                           documentation,
                                           standard,
                                           publish));
        auto value = Package {
            .name          = rstd_try(name.take(d)),
            .autoexamples  = autoexamples.take(),
            .version       = version.take(),
            .target        = target.take(),
            .license       = license.take(),
            .authors       = authors.take(),
            .description   = description.take(),
            .readme        = readme.take(),
            .repository    = repository.take(),
            .documentation = documentation.take(),
            .standard      = standard.take(),
            .publish       = publish.take(),
        };
        auto declared_root = source_root.take();
        if constexpr (! Embedded) value.source_root = rstd::move(declared_root);
        return Ok(PackageInput<Embedded> { .value = rstd::move(value) });
    }
};
} // namespace rstd

export namespace lito::manifest::wire
{
struct WorkspacePackage {
    Option<String>      version;
    Option<String>      license;
    Option<Vec<String>> authors;
    Option<String>      description;
    Option<ReadmeValue> readme;
    Option<String>      repository;
    Option<String>      documentation;
};
} // namespace lito::manifest::wire
export namespace rstd
{
template<>
struct Impl<serde::Deserialize, lito::manifest::wire::WorkspacePackage> {
    template<typename D>
    static auto deserialize(D& d)
        -> Result<lito::manifest::wire::WorkspacePackage, typename D::error_type> {
        using namespace lito::manifest::wire;
        auto version       = serde::OptionalField<String>("version"_str);
        auto license       = serde::OptionalField<String>("license"_str);
        auto authors       = serde::OptionalField<Vec<String>>("authors"_str);
        auto description   = serde::OptionalField<String>("description"_str);
        auto readme        = serde::OptionalField<ReadmeValue>("readme"_str);
        auto repository    = serde::OptionalField<String>("repository"_str);
        auto documentation = serde::OptionalField<String>("documentation"_str);
        rstd_try(serde::deserialize_record(d,
                                           serde::UnknownFieldPolicy::Reject,
                                           version,
                                           license,
                                           authors,
                                           description,
                                           readme,
                                           repository,
                                           documentation));
        return Ok(lito::manifest::wire::WorkspacePackage {
            .version       = version.take(),
            .license       = license.take(),
            .authors       = authors.take(),
            .description   = description.take(),
            .readme        = readme.take(),
            .repository    = repository.take(),
            .documentation = documentation.take(),
        });
    }
};
} // namespace rstd
