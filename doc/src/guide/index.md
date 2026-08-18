# Guide

The Guide explains decisions that span more than one manifest field or command. Use the
[Reference](../reference/index.md) when you already know the concept and need the exact contract.

- [Packages, targets, and workspaces](packages-targets-and-workspaces.md) defines project
  ownership.
- [Modules and source discovery](modules-and-source-discovery.md) explains Lito's module-first
  source convention.
- [Dependencies and the lock file](dependencies-and-lock.md) separates declarations from resolved
  source identities.
- [Profiles, features, and conditions](profiles-features-and-conditions.md) controls build variants.
- [Daily commands](build-test-bench-format-and-doc.md) covers the normal edit-build-test loop.
- [Generated sources and build tools](generated-sources-and-build-tools.md) covers `build.lua` and
  generated inputs.
- [External dependencies](external-dependencies.md) covers CMake, pkg-config, SDK libraries, and
  external source roots.
- [Install and runtime resources](install-and-runtime-resources.md) covers artifact publication.
- [Troubleshooting](troubleshooting.md) starts from Lito's diagnostic chain.
