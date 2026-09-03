# `update`, `lock`, and `config`

## `lito fetch`

```text
lito fetch [--locked] [--offline] [--frozen]
           [--output DIRECTORY] [-j N]
           [--features FEATURES] [--all-features] [--no-default-features]
```

Resolve the complete workspace dependency graph and acquire its active remote inputs without
scanning, compiling, linking, running build scripts, or preparing CMake packages. With no output
directory, verified inputs remain in Lito's global source cache. `--output` writes the same closure
to a new, portable source-bundle directory; the destination must not already exist.

For Cargo external dependencies, an output bundle contains Cargo's versioned vendor projection and
a validated relative source configuration. Offline builds pass that configuration to Cargo without
changing the external project's manifest or lock file.

`--locked`, `--offline`, and `--frozen` have the same lock and network semantics as build. Fetch
reuses locked Git commits and Registry releases; dependency upgrades remain the responsibility of
`lito update`.

```sh
lito fetch --locked
lito fetch --locked --output packaging/source-bundle
lito build --locked --offline --source-bundle packaging/source-bundle
```

## `lito update`

```text
lito update [OPTIONS]
```

Resolve the current manifests and update `lito.lock` without building selected artifacts.

For Registry dependencies, update conditionally refreshes the per-package HTTP Index entries
actually reached while solving the dependency graph. It does not download or clone a complete
Registry Index. Existing ETags are reused only for the same configured Index endpoint.

- `--offline` forbids network acquisition;
- repeated `--source-bundle DIRECTORY` supplies read-only source bundles.

```sh
lito update
lito update --offline --source-bundle packaging/source-bundle
```

## `lito lock export`

```text
lito lock export --format FORMAT [--attach-cargo-lock FILE] --output FILE
```

Both options are required. The current format is `flatpak-sources`. It writes Flatpak source entries
for locked Git/archive inputs into Lito's versioned source-bundle layout. The project lock remains
the source identity index.

```sh
lito lock export \
  --format flatpak-sources \
  --output packaging/lito-sources.json
```

`--attach-cargo-lock FILE` explicitly adds the registry and Git vendor inputs described by one
Cargo lock to the same Flatpak JSON array. Relative paths are resolved from the Lito project root.
Lito reads the Cargo lock only for this export: it does not discover one automatically, modify it,
copy Cargo data into `lito.lock`, or make Cargo participate in Lito lock freshness. Git packages
may require an exact checkout through the normal Lito source cache or network policy.

```sh
lito lock export \
  --format flatpak-sources \
  --attach-cargo-lock Cargo.lock \
  --output packaging/sources.json
```

## `lito config path`

Print the absolute `.lito/config.toml` path for the resolved project.

## `lito config get [KEY]`

With no key, print the complete persisted local configuration. With a TOML key path, print the
stored value. This command reads `.lito/config.toml`, not the fully merged effective configuration.

## `lito config set KEY VALUE`

Parse `VALUE` as TOML when possible, otherwise store it as a string. Validate the full local config
and write atomically.

```sh
lito config set toolchain.stdlib libstdc++
lito config set environment.append-path '["/opt/llvm/bin"]'
```

## `lito config unset KEY`

Remove a stored key, validate the remaining local config, and write atomically. Missing keys are
errors.

```sh
lito config unset environment.append-path
```

See [Configuration precedence](../config/precedence-and-locations.md).
