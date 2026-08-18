# Workspace and package fields

## `[workspace]`

`name` is required and must be a non-empty string. It names the workspace in diagnostics and
generated documentation.

`members` is required and is a non-empty array of unique relative package/workspace directories.

`default-members` is an optional non-empty array of member paths selected when no package is named.
Every default member must belong to `members`.

`package` is an optional table of values inherited by member packages:

- `version` is a non-empty string;
- `license` is a non-empty string.

`dependencies`, `external-dependencies`, and `external-sources` contain declarations members may
reference with `workspace = true`. Their nested fields are documented in
[Dependencies](dependencies.md) and
[Build tools and external inputs](build-tools-and-external-inputs.md).

Example:

```toml
[workspace]
name = "graphics"
members = ["core", "viewer"]
default-members = ["viewer"]

[workspace.package]
version = "1.0.0"
license = "MIT OR Apache-2.0"
```

## `[package]`

`name` is required. It contains only ASCII letters, digits, `-`, or `_`. It is the package identity;
dependency table keys must match it.

`version` is a non-empty string or `{ workspace = true }`. A production package containing a
library, binary, or benchmark must resolve a version. A test-only or compile-test-only package may
omit it. A package discovered through `install.lua` must also resolve a version, including when it
has no compile target.

`license` is a non-empty string or `{ workspace = true }`.

`source-root` is an optional relative base for package sources and usage paths. The resolved root
must contain the package directory. Without it, paths are based at the package manifest directory.

`standard` selects both the package language and its minimum language standard:

- `c99`, `c11`, `c17`, or `c23` selects C;
- `c++20`, `c++23`, or `c++26` selects C++.

A package with compile targets defaults to `c++20` when `standard` is omitted. An install-only
package without a compile target must omit `standard`. C++17 and earlier are not supported.

There are no separate `language` or `minimum-standard` fields; both legacy keys are rejected as
unknown manifest fields.

Selected dependency closures resolve an effective minimum per language. A C++ package may consume
a C package; a C package cannot depend on a C++ package.

`target` is an optional host/target predicate. It accepts `family`, `os`, `not-family`, and `not-os`.
Each field is either one string or an array. Families are `unix`, `windows`, and `unknown`. Operating
systems are `linux`, `windows`, `macos`, `android`, `freebsd`, `netbsd`, `openbsd`, and `unknown`.

Example:

```toml
[package]
name = "geometry"
version.workspace = true
license.workspace = true
standard = "c++23"
target = { family = "unix", not-os = "android" }
```

## `[profile]`

Package and workspace manifests may both declare project profile policy. See
[Profiles, features, and conditions](profiles-features-and-conditions.md).
