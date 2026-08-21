# Install and runtime resources

`lito install` resolves installable packages, builds only the artifacts requested by their install
recipes, and publishes one transaction to either a managed store or an untracked prefix. The
command defaults to the `release` profile.

## Default binary installation

A package without `install.lua` gets a default recipe for its binary targets. Each selected binary
is installed below `bin/` using its artifact name:

```sh
lito install --prefix staging --package viewer --bin viewer
```

Repeated `--bin` filters only directly selected packages using the default recipe. Runtime
dependencies are installed dependency-first and keep their own recipes.

## Managed root

`--root DIRECTORY` selects a Lito-managed store. If it is omitted, Lito checks
`LITO_INSTALL_ROOT`, `[install].root`, `LITO_HOME`, then `$HOME/.lito`.

The managed store records package/source identity, version, profile, target, runtime dependencies,
logical and physical destinations, artifact production, and transforms. Packages containing only
`bin/` entries use the direct layout. A recipe containing other destinations receives an isolated
package prefix, with public binary links under the store's `bin/` directory. Publication is locked
and transactional; an incomplete update is recovered before the next install.

## Prefix tree

`--prefix DIRECTORY` writes the recipe directly into an untracked prefix tree suitable for staging
or packaging. It conflicts with `--root`. `--force` allows replacement where ownership and the
selected destination mode permit it, but it does not make two packages claiming the same path
unambiguous.

## Custom install recipes

An `install.lua` regular file replaces the default recipe for its package. The script calls
`lito.install` exactly once and declares destinations rather than copying files itself:

```lua
lito.install({
  artifacts = {{
    target = { kind = "bin", name = "viewer" },
    destination = "bin/viewer",
  }},
  files = {{
    source = "LICENSE",
    destination = "share/viewer/LICENSE",
  }},
})
```

Lito validates the complete recipe, builds its exact binary targets, materializes package files,
templates, CMake external assets, and inventories into a staging transaction, then publishes the
result. Sources remain unchanged; strip transforms are applied only to staged copies.

A direct package with `install.lua` cannot be filtered with `--bin`, because the script owns one
complete package recipe.

## Install-specific runtime search

An artifact recipe can associate a binary with external asset sets. On Linux, Lito computes an
origin-relative ELF `RUNPATH` from the declared destinations and links an install-specific binary
variant:

```lua
lito.install({
  artifacts = {{
    target = { kind = "bin", name = "viewer" },
    destination = "bin/viewer",
    runtime_search = {{
      external_asset = { dependency = "cef", set = "runtime" },
    }},
  }},
  external_assets = {{
    dependency = "cef",
    set = "runtime",
    destination = "lib/cef",
  }},
})
```

The install link variant reuses compiled objects and has its own link receipt and catalog identity.
The normal build artifact keeps its original runtime-search policy.

## Binary runtime resources

A `[[bin]].resources` entry publishes a generated directory as a typed build result for
documentation or other integrations. It is not an input to the current install recipe and is not
copied by `lito install`.

Standard-library selection is also not an install resource declaration. Setting
`toolchain.stdlib = "auto"` resolves to a concrete compile/link library before project resolution,
but neither automatic nor explicit selection asks `lito install` to copy libc++, libc++abi, or
platform runtime DLLs from a managed SDK. Projects that require distributable runtime files need an
explicit artifact or external-asset contract.

See the exact [install script and resource contract](../reference/lito-toml/install-and-resources.md)
and the [`install` command](../reference/cli/build-and-install.md).
