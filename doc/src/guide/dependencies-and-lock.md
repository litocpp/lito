# Dependencies and the lock file

The manifest states requirements. `lito.lock` records the complete resolved package graph and exact
source identities used to satisfy them. The lock file does not replace `lito.toml`.

## Package dependencies

The dependency key must equal the provider package name:

```toml
[dependencies.geometry]
path = "../geometry"
visibility = "private"
```

Supported package sources are:

- `path`, resolved relative to the declaring manifest;
- `git`, optionally with exactly one of `branch`, `tag`, `rev`, or a full 40-digit `commit`;
- `workspace = true`, which reuses the workspace declaration with the same name.

A package dependency declares `visibility`:

- `public` propagates the provider's public compile and link requirements through a library;
- `private` makes the dependency available to the consumer package without exporting it;
- `link` contributes the link closure without exposing compile usage.

Development dependencies omit visibility and are active for tests, benchmarks, and compile tests.
Runtime dependencies describe packages needed by installation/runtime handling and do not accept
compile feature requests.

## Features on dependencies

`features = ["name"]` requests provider features. `default-features = false` disables that
dependency edge's request for provider defaults. Features are resolved per package; a dependency's
`LITO_FEAT_*` macro does not leak into the consumer package.

## Lock lifecycle

Normal build-like commands create or update `lito.lock` when the resolved graph changes. Commit it
for reproducible applications and workspaces.

- `lito update` resolves the manifest and updates the lock without building targets.
- `--locked` requires the existing lock to match and refuses an update.
- `--offline` forbids network source acquisition but may update the lock from available inputs.
- `--frozen` combines `--locked` and `--offline`.
- repeated `--fetch-seed DIRECTORY` adds read-only pre-populated sources for offline acquisition.

Offline resolution evaluates source availability before resolving network tools. A verified
archive file-cache entry can be extracted without `curl`, and a valid archive materialization
needs neither a downloader nor an extractor. Locked Git checkouts carry a Lito-owned receipt, so a
reusable checkout does not require Git merely to reinterpret Git's internal repository format.

`lito lock export --format flatpak-sources --output FILE` exports locked network inputs as Flatpak
source entries and an inline fetch-seed catalog.

## Local Git patches

Keep a Git identity in `lito.toml` and redirect it locally through config:

```toml
[patch."https://github.com/example/geometry.git"]
path = "../geometry"
```

This belongs in `.lito/config.toml` when it is a developer checkout override. The lock retains the
package/source model instead of requiring every manifest consumer to share the local path.

See the [dependency reference](../reference/lito-toml/dependencies.md) and
[configuration keys](../reference/config/keys.md).
