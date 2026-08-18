# `build` and `install`

## Shared package/profile selection

Both commands accept:

- repeated `-p/--package NAME`;
- `--profile PROFILE`;
- repeated `--features FEATURES`, with comma-separated names;
- `--no-default-features`.

Both accept source acquisition policy:

- `--locked` requires an unchanged lock;
- `--offline` forbids network acquisition;
- `--frozen` combines locked and offline;
- repeated `--fetch-seed DIRECTORY` adds read-only seeded sources.

Both accept execution/reporting options:

- `--verbose` prints build events;
- `--timing-file FILE` writes detailed timings;
- `--no-timing` hides the stdout timing summary;
- `-j/--jobs N` sets scan and compile workers.

The global `--use-env-flags` option appends `CFLAGS`, `CXXFLAGS`, and `LDFLAGS` to their respective
build domains. Lito ignores those variables unless this option is present.

## `lito build`

Synopsis:

```text
lito build [OPTIONS]
```

Additional options:

- repeated `--target NAME` selects package targets;
- `--out DIRECTORY` overrides the profile output root.

Examples:

```sh
lito build
lito build --profile release --package viewer
lito build --target codegen --target viewer -j 8
```

Without explicit package/target selection, Lito uses package defaults or workspace
`default-members`. `build` uses the `debug` profile when `--profile` is omitted.

## `lito install`

Synopsis:

```text
lito install [OPTIONS]
```

Additional options:

- repeated `--bin NAME` installs only selected binaries;
- `--root DIRECTORY` selects the Lito-managed install root;
- `--prefix DIRECTORY` writes an untracked prefix tree;
- `--force` replaces conflicting files where supported.

`--root` and `--prefix` conflict. Project config `[install].root` can provide the managed default.
`install` uses the `release` profile when `--profile` is omitted.

Without `install.lua`, Lito installs selected binary artifacts under `bin/`. A package install
script owns the complete recipe instead; `--bin` cannot partially filter a directly selected
package with a custom recipe.

Examples:

```sh
lito install --root .local-install
lito install --prefix package-root --bin viewer
lito install --profile release --prefix package-root --force
```

See [Install and runtime resources](../../guide/install-and-runtime-resources.md).
