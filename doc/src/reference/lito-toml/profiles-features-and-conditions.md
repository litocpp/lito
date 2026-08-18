# Profiles, features, and conditions

## `[profile]`

`exceptions` and `rtti` are project-wide booleans and both default to `true`.

Any other valid key defines a named build profile. Profile names contain ASCII letters, digits,
`-`, or `_`, and cannot be `exceptions` or `rtti`.

## `[profile.NAME]`

- `inherits` names a parent. It is required for custom profiles and forbidden for built-in `debug`
  and `release`.
- `opt-level` is integer `0`, `1`, `2`, or `3`, or string `"s"` or `"z"`.
- `debug` is boolean; integer `0`, `1`, or `2`; or `"none"`, `"line-directives-only"`,
  `"line-tables-only"`, `"limited"`, or `"full"`.
- `strip` is boolean or `"none"`, `"debuginfo"`, or `"symbols"`.
- `lto` is boolean or `"off"`, `"thin"`, or `"fat"`.

Boolean `debug = true` means full debug information. Boolean `strip = true` strips symbols. Boolean
`lto = true` means fat LTO.

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
