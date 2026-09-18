module;
#include <rstd/macro.hpp>

module lito.core:manifest.profile_schema;

import rstd;
import :artifact;
import rstd.serde;
import :manifest.profile;
import :manifest.primitives;
import :manifest.wire;
import :manifest.wire.profile;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito::manifest;

auto parse_profile_optimization(const wire::ProfileValue& value, ref<str> context)
    -> ManifestSchemaResult<Optimization> {
    auto integer = value.integer;
    if (integer.is_some()) {
        switch (integer->to_primitive()) {
        case 0: return Ok(Optimization::None);
        case 1: return Ok(Optimization::Level1);
        case 2: return Ok(Optimization::Level2);
        case 3: return Ok(Optimization::Level3);
        default: break;
        }
    }
    auto text = value.text.as_ref();
    if (text.is_some() && *text == "s"_str) return Ok(Optimization::Size);
    if (text.is_some() && *text == "z"_str) return Ok(Optimization::SizeMin);
    return Err(
        ManifestSchemaError::Domain(rstd::format("{} must be 0, 1, 2, 3, 's', or 'z'", context)));
}

auto parse_profile_debug(const wire::ProfileValue& value, ref<str> context)
    -> ManifestSchemaResult<DebugInfo> {
    auto boolean = value.boolean;
    if (boolean.is_some()) return Ok(*boolean ? DebugInfo::Full : DebugInfo::None);
    auto integer = value.integer;
    if (integer.is_some()) {
        switch (integer->to_primitive()) {
        case 0: return Ok(DebugInfo::None);
        case 1: return Ok(DebugInfo::Limited);
        case 2: return Ok(DebugInfo::Full);
        default: break;
        }
    }
    auto text = value.text.as_ref();
    if (text.is_some() && *text == "none"_str) return Ok(DebugInfo::None);
    if (text.is_some() && *text == "line-directives-only"_str) {
        return Ok(DebugInfo::LineDirectivesOnly);
    }
    if (text.is_some() && *text == "line-tables-only"_str) {
        return Ok(DebugInfo::LineTablesOnly);
    }
    if (text.is_some() && *text == "limited"_str) return Ok(DebugInfo::Limited);
    if (text.is_some() && *text == "full"_str) return Ok(DebugInfo::Full);
    return Err(ManifestSchemaError::Domain(
        rstd::format("{} must be false, true, 0, 1, 2, 'none', 'line-directives-only', "
                     "'line-tables-only', 'limited', or 'full'",
                     context)));
}

auto parse_profile_strip(const wire::ProfileValue& value, ref<str> context)
    -> ManifestSchemaResult<lito::artifact::StripMode> {
    using lito::artifact::StripMode;
    auto boolean = value.boolean;
    if (boolean.is_some()) return Ok(*boolean ? StripMode::Symbols : StripMode::None);
    auto text = value.text.as_ref();
    if (text.is_some() && *text == "none"_str) return Ok(StripMode::None);
    if (text.is_some() && *text == "debuginfo"_str) return Ok(StripMode::DebugInfo);
    if (text.is_some() && *text == "symbols"_str) return Ok(StripMode::Symbols);
    return Err(ManifestSchemaError::Domain(
        rstd::format("{} must be false, true, 'none', 'debuginfo', or 'symbols'", context)));
}

auto parse_profile_lto(const wire::ProfileValue& value, ref<str> context)
    -> ManifestSchemaResult<Lto> {
    auto boolean = value.boolean;
    if (boolean.is_some()) return Ok(*boolean ? Lto::Fat : Lto::Off);
    auto text = value.text.as_ref();
    if (text.is_some() && *text == "off"_str) return Ok(Lto::Off);
    if (text.is_some() && *text == "thin"_str) return Ok(Lto::Thin);
    if (text.is_some() && *text == "fat"_str) return Ok(Lto::Fat);
    return Err(ManifestSchemaError::Domain(
        rstd::format("{} must be false, true, 'off', 'thin', or 'fat'", context)));
}

auto parse_build_profile(ref<str> name, wire::BuildProfile value)
    -> ManifestSchemaResult<BuildProfileDefinition> {
    auto context = rstd::format("manifest.profile.{}", name);
    if (! valid_build_profile_name(name)) {
        return Err(ManifestSchemaError::Domain(
            rstd::format("{} is not a valid build profile name", context.as_str())));
    }
    auto result = BuildProfileDefinition {
        .name =
            BuildProfileName {
                .value = String::make(name),
            },
    };
    auto inherits = rstd::move(value.inherits);
    if (inherits.is_some()) {
        auto text = inherits->as_str();
        if (text == "base"_str) {
            return Err(ManifestSchemaError::Domain(rstd::format(
                "{}.inherits cannot name the non-selectable base profile; inherit debug, release, "
                "or plain instead",
                context.as_str())));
        }
        if (! valid_build_profile_name(text)) {
            return Err(ManifestSchemaError::Domain(
                rstd::format("{}.inherits must name a valid build profile", context.as_str())));
        }
        result.inherits = Some(BuildProfileName {
            .value = String::make(text),
        });
    }
    result.exceptions = value.exceptions;
    result.rtti       = value.rtti;
    if (value.optimization.is_some())
        result.optimization = Some(rstd_try(parse_profile_optimization(
            *value.optimization, rstd::format("{}.opt-level", context.as_str()).as_str())));
    if (value.debug.is_some())
        result.debug_info = Some(rstd_try(parse_profile_debug(
            *value.debug, rstd::format("{}.debug", context.as_str()).as_str())));
    if (value.strip.is_some())
        result.strip = Some(rstd_try(parse_profile_strip(
            *value.strip, rstd::format("{}.strip", context.as_str()).as_str())));
    if (value.lto.is_some())
        result.lto = Some(rstd_try(
            parse_profile_lto(*value.lto, rstd::format("{}.lto", context.as_str()).as_str())));
    return Ok(rstd::move(result));
}

auto parse_project_profile(Option<wire::Profiles> value)
    -> ManifestSchemaResult<Option<ProjectProfile>> {
    if (value.is_none()) return Ok(Option<ProjectProfile> {});
    auto profile = ProjectProfile {};
    if (value->base.is_some()) {
        profile.base.exceptions = value->base->exceptions;
        profile.base.rtti       = value->base->rtti;
    }
    if (value->exceptions.is_some()) {
        if (profile.base.exceptions.is_some())
            return Err(ManifestSchemaError::Domain(
                "manifest.profile.exceptions conflicts with manifest.profile.base.exceptions"_Str));
        profile.base.exceptions = value->exceptions;
    }
    if (value->rtti.is_some()) {
        if (profile.base.rtti.is_some())
            return Err(ManifestSchemaError::Domain(
                "manifest.profile.rtti conflicts with manifest.profile.base.rtti"_Str));
        profile.base.rtti = value->rtti;
    }
    for (auto key : value->named.keys()) {
        auto item = value->named.get_mut(key->as_str()).unwrap();
        profile.build_profiles.push(
            rstd_try(parse_build_profile(key->as_str(), rstd::move(*item))));
    }
    rstd_try(validate_build_profiles(profile));
    return Ok(Some<ProjectProfile>(rstd::move(profile)));
}
