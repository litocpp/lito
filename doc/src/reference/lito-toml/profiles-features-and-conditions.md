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

`enables` is an optional array of `dependency/feature` strings. When this feature is enabled,
each entry requests the named feature through an active direct dependency, development dependency,
or runtime dependency.
For example:

```toml
[features.waywallen]
enables = ["shared-cli/waywallen"]

[dependencies]
shared-cli = { version = "0.1" }
```

The dependency remains available with or without the package feature. Its `condition` still controls
whether the edge is active; forwarding never activates a disabled edge, even if another consumer
uses the same provider. Development requests apply only when development targets are selected.
Fixed dependency `features` and forwarded requests are unified across consumers. Forwarding can
continue through the provider's own feature declarations.

Runtime requests apply only when the consumer is being installed. Forwarding to a runtime package
does not make it a compile/link dependency or activate it during an ordinary build.

Normal/development C++/plugin/pmacro dependencies and runtime dependencies support these requests.
Entries must reference a declared direct dependency; script and external dependencies, local feature
names, `dep:`, and `?/feature` are not supported. Duplicate entries are rejected. A requested provider
feature must exist.

Lito derives the package-local macro as `LITO_FEAT_` plus the uppercased feature name with `-`
changed to `_`. Two features that normalize to the same macro are rejected.

Feature macro names are not configurable. The manifest parser and JSON Schema accept `default`
and `enables` in a feature declaration and derive the macro from the feature name.

Command-line `--features` accepts repeated arguments, comma-separated names, and quoted ASCII
whitespace-separated names. Empty separator items are ignored and repeated selectors are deduplicated.
A bare feature name matches every selected root declaring it; at least one must match.
`package/feature` selects a selected root's feature or a direct C++/plugin/pmacro or runtime
dependency's feature.
Dependency requests are applied only through active edges, including development edges only when
their targets are selected and runtime edges only when their consumer is installed. A qualifier does
not add roots, search transitive dependencies, or bypass dependency conditions. Unknown selectors
and malformed or repeated slashes are errors.
`dep:` and weak dependency `?/feature` syntax are not supported.

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

## Dependency condition environment

Package and external dependency conditions share the same context:

- `feature.NAME`: a declared package-local feature's final boolean value;
- `target.os`, `target.vendor`, `target.family`, `target.environment`, `target.arch`,
  `target.triple`: the effective target of the consuming package instance;
- `host.os`, `host.arch`: the build host;
- `build.cross`: whether that instance is cross-compiled;
- `profile.name`: the selected profile;
- `toolchain.compiler`, `toolchain.stdlib`, `toolchain.stdlib-runtime`: the resolved toolchain policy.

Host and target instances can select different edges while sharing the existing package feature
union. A dependency providing a host tool is still selected using its consumer's environment.
Unknown keys and incompatible value types are errors when evaluated; boolean operators retain
short-circuit evaluation. Conditions cannot inspect another package's features or probe results.

Completed build products preserve these environment values for installation without rebuilding.
Products from older schema versions must be rebuilt before they can be reused.
