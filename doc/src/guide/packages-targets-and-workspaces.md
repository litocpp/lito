# Packages, targets, and workspaces

## Project discovery

Lito searches from the effective working directory for the nearest fixed `lito.toml`. Use the
global `-C DIRECTORY` option to select another working directory without changing the shell.

A manifest is either a package manifest or a workspace manifest. A directory does not declare both
in the same file.

## Packages own build policy

A package owns its sources, usage requirements, features, dependencies, and targets. Its name is
the identity used by dependency keys and lock entries. Package names use ASCII letters, digits,
`-`, and `_`; a dot is not allowed.

Production packages declare a version. A package containing only test or compile-test targets may
omit it. A workspace member may inherit version, license, and authors from `[workspace.package]`:

```toml
[package]
name = "geometry"
version.workspace = true
license.workspace = true
authors.workspace = true
```

The package `standard` field owns both its language and minimum standard. C standard values select
C; C++ standard values select C++. A compile package without the field defaults to C++20.

## Target kinds

Lito has these package target kinds:

- `[lib]` builds one static archive and can own a C++ module namespace.
- `[[bin]]` builds an executable and may publish generated runtime resources.
- `[[test]]` builds an executable run by `lito test`.
- `[[bench]]` builds an executable run by `lito bench`.
- `[compile-test]` owns compile-success and compile-failure cases.

Target names identify selection and output layout. A library separately declares `archive`, the
safe basename used for the `.a` file. `module` is a C++ logical module name, not a filesystem path.

If a package has multiple targets, use repeated `--target NAME`. Use repeated `--package NAME` in a
workspace. When neither is supplied, Lito selects the project defaults.

## Workspaces own a package catalog

A workspace lists member directories explicitly:

```toml
[workspace]
name = "graphics"
members = ["core", "renderer", "viewer"]
default-members = ["viewer"]

[workspace.package]
version = "0.4.0"
license = "MIT OR Apache-2.0"
authors = ["Example Authors <authors@example.com>"]
```

`default-members` must name workspace members. It controls the default root selection, not which
packages exist in the dependency graph.

The workspace can also own reusable dependency, external dependency, and external source
declarations. Members opt into those declarations with `workspace = true`; the member still owns
visibility and feature requests where those are target-local decisions.

## Associated development projects

For a package or workspace root, Lito also looks for independent projects below `tests/` and
`benches/`. Each associated project has its own `lito.toml`, package names, dependencies, and source
ownership. They are selected for test or benchmark operations and do not enter a normal production
build.

See the exact [workspace and package fields](../reference/lito-toml/workspace-and-package.md) and
[target fields](../reference/lito-toml/targets-and-sources.md).
