module;
#include <rstd/macro.hpp>

module lito.core:manifest.profile_schema;

import rstd;
import :artifact;
import rstd.serde;
import rstd.toml;
import :manifest.profile;
import :manifest.primitives;
import :manifest.wire;

using namespace rstd::prelude;
using namespace rstd::literals;
using Toml = rstd::toml::Value;
using namespace lito::manifest;

auto build_profile_key(ref<str> key) -> bool {
    return key == "inherits"_str || key == "opt-level"_str || key == "debug"_str ||
           key == "strip"_str || key == "lto"_str || key == "exceptions"_str || key == "rtti"_str;
}

auto parse_profile_bool(const Toml& value, ref<str> context) -> ManifestSchemaResult<bool> {
    auto parsed = value.as_bool();
    if (parsed.is_some()) return Ok(*parsed);
    return manifest_schema_failure<bool>(rstd::format("{} must be a bool", context));
}

auto parse_profile_optimization(const Toml& value, ref<str> context)
    -> ManifestSchemaResult<Optimization> {
    auto integer = value.as_integer();
    if (integer.is_some()) {
        switch (integer->to_primitive()) {
        case 0: return Ok(Optimization::None);
        case 1: return Ok(Optimization::Level1);
        case 2: return Ok(Optimization::Level2);
        case 3: return Ok(Optimization::Level3);
        default: break;
        }
    }
    auto text = value.as_str();
    if (text.is_some() && *text == "s"_str) return Ok(Optimization::Size);
    if (text.is_some() && *text == "z"_str) return Ok(Optimization::SizeMin);
    return manifest_schema_failure<Optimization>(
        rstd::format("{} must be 0, 1, 2, 3, 's', or 'z'", context));
}

auto parse_profile_debug(const Toml& value, ref<str> context) -> ManifestSchemaResult<DebugInfo> {
    auto boolean = value.as_bool();
    if (boolean.is_some()) return Ok(*boolean ? DebugInfo::Full : DebugInfo::None);
    auto integer = value.as_integer();
    if (integer.is_some()) {
        switch (integer->to_primitive()) {
        case 0: return Ok(DebugInfo::None);
        case 1: return Ok(DebugInfo::Limited);
        case 2: return Ok(DebugInfo::Full);
        default: break;
        }
    }
    auto text = value.as_str();
    if (text.is_some() && *text == "none"_str) return Ok(DebugInfo::None);
    if (text.is_some() && *text == "line-directives-only"_str) {
        return Ok(DebugInfo::LineDirectivesOnly);
    }
    if (text.is_some() && *text == "line-tables-only"_str) {
        return Ok(DebugInfo::LineTablesOnly);
    }
    if (text.is_some() && *text == "limited"_str) return Ok(DebugInfo::Limited);
    if (text.is_some() && *text == "full"_str) return Ok(DebugInfo::Full);
    return manifest_schema_failure<DebugInfo>(
        rstd::format("{} must be false, true, 0, 1, 2, 'none', 'line-directives-only', "
                     "'line-tables-only', 'limited', or 'full'",
                     context));
}

auto parse_profile_strip(const Toml& value, ref<str> context)
    -> ManifestSchemaResult<lito::artifact::StripMode> {
    using lito::artifact::StripMode;
    auto boolean = value.as_bool();
    if (boolean.is_some()) return Ok(*boolean ? StripMode::Symbols : StripMode::None);
    auto text = value.as_str();
    if (text.is_some() && *text == "none"_str) return Ok(StripMode::None);
    if (text.is_some() && *text == "debuginfo"_str) return Ok(StripMode::DebugInfo);
    if (text.is_some() && *text == "symbols"_str) return Ok(StripMode::Symbols);
    return manifest_schema_failure<StripMode>(
        rstd::format("{} must be false, true, 'none', 'debuginfo', or 'symbols'", context));
}

auto parse_profile_lto(const Toml& value, ref<str> context) -> ManifestSchemaResult<Lto> {
    auto boolean = value.as_bool();
    if (boolean.is_some()) return Ok(*boolean ? Lto::Fat : Lto::Off);
    auto text = value.as_str();
    if (text.is_some() && *text == "off"_str) return Ok(Lto::Off);
    if (text.is_some() && *text == "thin"_str) return Ok(Lto::Thin);
    if (text.is_some() && *text == "fat"_str) return Ok(Lto::Fat);
    return manifest_schema_failure<Lto>(
        rstd::format("{} must be false, true, 'off', 'thin', or 'fat'", context));
}

auto parse_build_profile(ref<str> name, const Toml& value)
    -> ManifestSchemaResult<BuildProfileDefinition> {
    auto context = rstd::format("manifest.profile.{}", name);
    if (! valid_build_profile_name(name)) {
        return manifest_schema_failure<BuildProfileDefinition>(
            rstd::format("{} is not a valid build profile name", context.as_str()));
    }
    auto table = table_value(value, context.as_str());
    if (table.is_err()) return Err(rstd::move(table).unwrap_err());
    rstd_try(reject_unknown(**table, context.as_str(), build_profile_key));
    auto result = BuildProfileDefinition {
        .name =
            BuildProfileName {
                .value = String::make(name),
            },
    };
    auto inherits = member(value, "inherits"_str);
    if (inherits.is_some()) {
        auto text = (**inherits).as_str();
        if (text.is_some() && *text == "base"_str) {
            return manifest_schema_failure<BuildProfileDefinition>(rstd::format(
                "{}.inherits cannot name the non-selectable base profile; inherit debug, release, "
                "or plain instead",
                context.as_str()));
        }
        if (text.is_none() || ! valid_build_profile_name(*text)) {
            return manifest_schema_failure<BuildProfileDefinition>(
                rstd::format("{}.inherits must name a valid build profile", context.as_str()));
        }
        result.inherits = Some(BuildProfileName {
            .value = String::make(*text),
        });
    }
    auto exceptions = member(value, "exceptions"_str);
    if (exceptions.is_some()) {
        result.exceptions = Some(rstd_try(parse_profile_bool(
            **exceptions, rstd::format("{}.exceptions", context.as_str()).as_str())));
    }
    auto rtti = member(value, "rtti"_str);
    if (rtti.is_some()) {
        result.rtti = Some(rstd_try(
            parse_profile_bool(**rtti, rstd::format("{}.rtti", context.as_str()).as_str())));
    }
    auto optimization = member(value, "opt-level"_str);
    if (optimization.is_some()) {
        result.optimization = Some(rstd_try(parse_profile_optimization(
            **optimization, rstd::format("{}.opt-level", context.as_str()).as_str())));
    }
    auto debug = member(value, "debug"_str);
    if (debug.is_some()) {
        result.debug_info = Some(rstd_try(
            parse_profile_debug(**debug, rstd::format("{}.debug", context.as_str()).as_str())));
    }
    auto strip = member(value, "strip"_str);
    if (strip.is_some()) {
        result.strip = Some(rstd_try(
            parse_profile_strip(**strip, rstd::format("{}.strip", context.as_str()).as_str())));
    }
    auto lto = member(value, "lto"_str);
    if (lto.is_some()) {
        result.lto = Some(
            rstd_try(parse_profile_lto(**lto, rstd::format("{}.lto", context.as_str()).as_str())));
    }
    return Ok(rstd::move(result));
}

auto parse_base_profile(const Toml& value) -> ManifestSchemaResult<BaseProfileDefinition> {
    auto path = rstd::serde::DataPath().with_field("profile"_str).with_field("base"_str);
    auto wire =
        rstd_try(decode_manifest_value<lito::manifest::wire::BaseProfile>(value, rstd::move(path)));
    return Ok(BaseProfileDefinition {
        .exceptions = rstd::move(wire.exceptions),
        .rtti       = rstd::move(wire.rtti),
    });
}

auto parse_project_profile(Option<ref<Toml>> value)
    -> ManifestSchemaResult<Option<ProjectProfile>> {
    if (value.is_none()) return Ok(Option<ProjectProfile> {});
    auto table = table_value(**value, "manifest.profile"_str);
    if (table.is_err()) return Err(rstd::move(table).unwrap_err());

    auto profile      = ProjectProfile {};
    auto base_profile = member(**value, "base"_str);
    if (base_profile.is_some()) {
        profile.base = rstd_try(parse_base_profile(**base_profile));
    }
    auto exceptions = member(**value, "exceptions"_str);
    if (exceptions.is_some()) {
        if (profile.base.exceptions.is_some()) {
            return manifest_schema_failure<Option<ProjectProfile>>(
                "manifest.profile.exceptions conflicts with manifest.profile.base.exceptions"_str);
        }
        profile.base.exceptions =
            Some(rstd_try(parse_profile_bool(**exceptions, "manifest.profile.exceptions"_str)));
    }
    auto rtti = member(**value, "rtti"_str);
    if (rtti.is_some()) {
        if (profile.base.rtti.is_some()) {
            return manifest_schema_failure<Option<ProjectProfile>>(
                "manifest.profile.rtti conflicts with manifest.profile.base.rtti"_str);
        }
        profile.base.rtti = Some(rstd_try(parse_profile_bool(**rtti, "manifest.profile.rtti"_str)));
    }
    auto keys = (**table).keys();
    for (auto key : keys) {
        if ((*key).as_str() == "base"_str || (*key).as_str() == "exceptions"_str ||
            (*key).as_str() == "rtti"_str) {
            continue;
        }
        auto item = (**table).get((*key).as_str());
        profile.build_profiles.push(rstd_try(parse_build_profile((*key).as_str(), **item)));
    }
    rstd_try(validate_build_profiles(profile));
    return Ok(Some<ProjectProfile>(rstd::move(profile)));
}
