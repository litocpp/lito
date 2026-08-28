# `lito.toml` Reference

Every Lito project uses the fixed file name `lito.toml`. Unknown top-level tables, fields, and nested
fields are errors. A manifest root is exactly one of:

- a package manifest with required `[package]`;
- a workspace manifest with required `[workspace]`.

The machine-readable structural contract is the
[Lito JSON Schema](https://github.com/litocpp/lito/blob/main/schema/lito.schema.json). The executable
remains authoritative for filesystem checks, workspace/package graph rules, source discovery,
conditions, and toolchain-dependent validation.

## Package top-level fields

A package manifest accepts:

- `[package]`;
- `[lib]`;
- `[pmacro]`;
- `[[bin]]`;
- `[[test]]`;
- `[[bench]]`;
- `[compile-test]` and `[[compile-test.cases]]`;
- `[usage]` and `[[when]]`;
- `[features.NAME]`;
- `[dependencies.NAME]`, `[dev-dependencies.NAME]`, and `[runtime-dependencies.NAME]`;
- `[external-dependencies.pkg-config.NAME]`, `[external-dependencies.cmake.NAME]`, and
  `[external-dependencies.cargo.NAME]`;
- `[external-sources.NAME]` and `[source-groups.NAME]`;
- `[build-tools.NAME]`;
- `[profile]` and `[profile.NAME]`.

## Workspace top-level fields

A workspace manifest accepts `[workspace]` and `[profile]`. Reusable package, dependency, external
dependency, and external source declarations are nested under `[workspace]`.

## Paths

Manifest paths are non-empty and relative to the manifest that declares them unless a field says
otherwise. Lito resolves, canonicalizes, and validates paths in the owner that consumes them. Do not
assume a path is relative to the process working directory.

Continue with:

- [Workspace and package](workspace-and-package.md)
- [Targets and sources](targets-and-sources.md)
- [Dependencies](dependencies.md)
- [Profiles, features, and conditions](profiles-features-and-conditions.md)
- [Usage and linking](usage-and-linking.md)
- [Build tools and external inputs](build-tools-and-external-inputs.md)
- [Install and resources](install-and-resources.md)
