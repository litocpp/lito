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
- Registry `version`, optionally selecting a configured `registry`;
- `workspace = true`, which reuses the workspace declaration with the same name.

The dependency key is always the provider package name. Package aliases are not supported.

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
source entries and an inline `.lito/fetch-seed/entries.json` document. Lito also reads the legacy
`.lito/fetch-seed/catalog.json` name for existing exported sources.

## Local Git patches

Keep a Git identity in `lito.toml` and redirect it locally through config:

```toml
[patch."https://github.com/example/geometry.git"]
path = "../geometry"
```

This belongs in `.lito/config.toml` when it is a developer checkout override. `lito.toml` retains
the Git requirement, but the matched package resolves as a Path source. Lito does not contact the
matched Git URL or require the patch directory to be a Git repository. Like an ordinary Path
dependency, the lock records neither the original Git source nor the machine-local path.

The patch config is therefore required whenever that local source should remain active. A normal
command can update the lock when a patch is enabled or removed; `--locked` rejects such a source
change. Local edits and uncommitted files participate directly in builds, just as they do for an
ordinary Path dependency.

See the [dependency reference](../reference/lito-toml/dependencies.md) and
[configuration keys](../reference/config/keys.md).
