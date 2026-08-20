# Profiles, features, and conditions

## `[profile]`

The table contains the non-selectable `base` profile and selectable profile definitions.

The legacy `profile.exceptions` and `profile.rtti` boolean fields both default to `true`. They remain
accepted for compatibility but are deprecated; use `[profile.base]` instead. A legacy field and its
replacement cannot both be declared in one manifest.

## `[profile.base]`

`exceptions` and `rtti` are booleans and both default to `true`. `base` is the common root inherited
by `debug`, `release`, `plain`, and all custom profiles. It cannot declare `inherits`, cannot be used
as an explicit parent, and cannot be selected with `--profile`.

A selectable profile may fix either setting. `plain` otherwise treats the inherited values as
fallbacks that global C++ build inputs may override. The effective setting applies to the complete
package graph and participates in compiler, BMI, and artifact identities.

## `[profile.NAME]`

- `inherits` names a parent. It is required for custom profiles and forbidden for built-in `debug`,
  `release`, and `plain`.
- `exceptions` and `rtti` are booleans inherited from the parent profile.
- `opt-level` is integer `0`, `1`, `2`, or `3`, or string `"s"` or `"z"`.
- `debug` is boolean; integer `0`, `1`, or `2`; or `"none"`, `"line-directives-only"`,
  `"line-tables-only"`, `"limited"`, or `"full"`.
- `strip` is boolean or `"none"`, `"debuginfo"`, or `"symbols"`.
- `lto` is boolean or `"off"`, `"thin"`, or `"fat"`.

Boolean `debug = true` means full debug information. Boolean `strip = true` strips symbols. Boolean
`lto = true` means fat LTO.

The built-in `plain` profile delegates exceptions, RTTI, optimization, debug information, strip,
LTO, and `NDEBUG`. Custom profiles may use `inherits = "plain"` and fix only selected fields. For
exceptions and RTTI, the values inherited from `base` remain the fallback when global C++ build
configuration or explicitly enabled `CXXFLAGS` do not provide an option. Other unspecified
delegated fields remain at the compiler default. This does not delegate other Lito-owned C/C++
contracts, and package usage cannot set these fields.

The built-in defaults are described in
[Profiles, features, and conditions](../../guide/profiles-features-and-conditions.md).

## `[features.NAME]`

Feature names start with an ASCII letter or `_`, followed by letters, digits, `-`, or `_`.

`default` is a boolean and defaults to `false`.

Lito derives the package-local macro as `LITO_FEAT_` plus the uppercased feature name with `-`
changed to `_`. Two features that normalize to the same macro are rejected.

Feature macro names are not configurable. The manifest parser and JSON Schema both accept only
`default` in a feature declaration and derive the macro from the feature name.

## `[[when]]`

Each conditional configuration requires:

- `condition`: a non-empty expression;
- `[when.usage]`: the same fields accepted by `[usage]`.

Conditions can use `true`, `false`, `feature.NAME`, `target.family`, and `target.os`, boolean `!`,
`&&`, `||`, parentheses, and equality/inequality comparisons where applicable. Target family/OS
values use the same vocabulary as `[package].target`.

Active conditional usage is merged with unconditional usage. Conflicting definitions or scalar
settings are errors; declaration order does not silently choose a winner.

Target-local conditional source groups use the target `when` field documented under
[Targets and sources](targets-and-sources.md).
