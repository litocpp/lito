# `build`, `run`, `install`, and `pack`

## Shared package/profile selection

Both commands accept:

- repeated `-p/--package NAME`;
- `--profile PROFILE`;
- repeated `--features FEATURES`, with comma/quoted whitespace-separated names or `package/feature`
  selectors (see [feature selection](../lito-toml/profiles-features-and-conditions.md#featuresname));
- `--no-default-features`.

Both accept source acquisition policy:

- `--locked` requires an unchanged lock;
- `--offline` forbids network acquisition;
- `--frozen` combines locked and offline;
- repeated `--source-bundle DIRECTORY` adds read-only seeded sources.

Both accept execution/reporting options:

- `--verbose` prints build events;
- `--timing-file FILE` writes detailed timings;
- `--no-timing` hides the stdout timing summary;
- `-j/--jobs N` bounds scan, compile-cache check, and compile workers.

Without `--verbose`, interactive terminals show append-only `=` bars for source scanning and
compile-cache checks. Scan progress counts packages, not files or targets: a package advances
only when its selected targets and any possible further import discovery have finished in the
current batch. Shared host-tool and remaining source scans have separate batches. Cache checks
validate existing object and BMI contents before scheduling compilation.

The bar width is chosen at the start of each batch. It never rewrites earlier output or moves the
cursor backwards. Redirected output reports only batch start and finish. `--verbose` prints
per-file scan/scan-reuse events without progress bars; cache checks use plain start/finish lines.
`--no-timing` does not hide this progress.

Preparation work prints plain summary lines in both normal and verbose output:
`[scan-finalize]` reports compilation units and unique headers, `[compile-plan]` reports
compilation units and direct dependency edges, and `[execution-graph]` reports actions and
artifacts in the current execution graph. Each line includes the elapsed preparation time;
counts describe the current graph or plan, including retained host-tool work, not only new
items. These summaries do not use progress bars and remain visible with `--no-timing`.
The `[scan-finalize]` label appears as soon as finalization starts; counts and elapsed time
are appended when it completes. If another message interrupts the line, completion prints
a new labeled line without rewriting earlier output.

Timing stages report exclusive wall time, except for the inclusive `build.total` row. Cache checks
have their own `build.compile-cache-check` stage. Worker totals and nested action timings can
overlap and must not be added to the stage totals. Shared host-tool and target checks are combined
in the cache-check statistics.

The global `--use-env-flags` option appends `CFLAGS`, `CXXFLAGS`, and `LDFLAGS` to their respective
build domains. Lito ignores those variables unless this option is present.

## `lito build`

Synopsis:

```text
lito build [OPTIONS]
```

Additional options:

- repeated `--target NAME` selects package targets. Use a kind prefix, such as `bin:viewer`,
  `lib:viewer`, or `cdylib:viewer-android`, to disambiguate names;
- repeated `--example NAME` selects example targets instead of production targets;
- `--examples` builds all examples in the selected packages;
- `--build-dir DIRECTORY` overrides the profile output root.

Examples:

```sh
lito build
lito build --profile release --package viewer
lito build --target codegen --target viewer -j 8
```

Without explicit package/target selection, Lito uses package defaults or workspace
`default-members`. `build` uses the `debug` profile when `--profile` is omitted.
Binary and cdylib targets may declare their own platform predicates. Default selection skips an
unsupported target. Selecting such a target explicitly reports an error.

Builds automatically write `compile_commands.json` in the effective build directory, such as
`build/debug/` or `build/release/`. The database contains the selected dependency closure, including
cached units, with the actual compiler arguments, source, working directory, and staging output.
Shared host tools and generated sources appear in the same database; isolated host environments
have their own database. It is published after the full scan and plugin preparation, before target
compilation. Unchanged contents are not rewritten. `--outputs-json` is a separate artifact report.

`--example`, `--examples`, and `--target` are mutually exclusive selection modes. An example name
shared by multiple selected packages requires `-p` to disambiguate. Example executables are stored
under `examples/PACKAGE/`, separately from ordinary binaries and tests.

## `lito run --example`

```sh
lito run -p ui-examples --example gallery -- --theme dark
```

Build and run exactly one example. The current run command requires `--example NAME`; it does not
select ordinary binaries or a default executable. Build package/profile, source acquisition,
`--build-dir`, job, feature, and reporting options are supported. Arguments after `--` are passed
to the program, which runs in its package directory with inherited standard streams and the
resolved runtime environment. A normal child exit code is returned unchanged; termination by a
signal returns `128 + signal`. Cross-target execution without a configured runner is rejected.

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

## `lito pack --recipe`

```sh
lito pack -p viewer --recipe apk --output dist/viewer.apk
```

Select one local package with `-p` and one recipe from its package-root `pack.lua` with `--recipe`.
The command uses the existing build target configuration and builds the targets referenced by the
recipe. It defaults to the `release` profile. `--profile` and `--build-dir` select a different build.
Use `--no-build` with the matching profile, target configuration, and build directory to package an
existing completed build. The original `lito pack` syntax for Registry source archives is available
without `--recipe`.

The default output is `pack/PACKAGE/RECIPE/FILENAME` inside the effective build directory.
`--output FILE` overrides it. Relative output paths are resolved from the command's working
directory. The tool writes a temporary file and Lito publishes it at the destination after a
successful exit.

### `pack.lua`

The script may declare multiple named recipes. A recipe specifies a host executable, its arguments,
and an output filename. `tool` accepts an executable name on the host `PATH`, an absolute path, or
a package-relative path with a directory component. The tool runs from the package directory with
the resolved host environment.

```lua
lito.pack {
    name = "apk",
    tool = "python3",
    output = "viewer.apk",
    args = {
        "android/package.py",
        "--library", { artifact = "cdylib:viewer-android" },
        "--abi", { android_abi = true },
        "--min-api", { android_min_api = true },
        "--output", { output = true },
    },
}
```

Each argument is a string or one of the following references.

- `{ artifact = "bin:NAME" }` or `{ artifact = "cdylib:NAME" }` passes the actual artifact path and
  selects that package-owned target for the build. Each recipe needs at least one artifact input.
- `{ android_abi = true }` and `{ android_min_api = true }` pass the ABI and native minimum API from
  the Android build configuration. These references require an Android build.
- `{ output = true }` passes the temporary output path and must appear exactly once. The tool must
  create a regular file at that path.

`lito.env(NAME)` returns a UTF-8 environment value or `nil` when the variable is unset.
The tool and its arguments are passed directly as a process invocation.

### Lua recipes and script packages

A Lua recipe declares its input targets and a callback. Lito runs the callback for the selected
recipe after building or validating those inputs. `tool` and `args` belong to executable recipes.

```lua
local lito = require("@lito")
lito.pack {
    name = "copy",
    output = "viewer.bin",
    inputs = { "bin:viewer" },
    run = function(ctx)
        lito.copy_file(ctx.artifacts["bin:viewer"], ctx.output)
    end,
}
```

The callback receives `package_root`, `work_dir`, `output`, the effective target triple in
`target`, and an `artifacts` table mapping each input selector to its validated runtime file.
Android builds additionally provide `android_abi` and the integer `android_min_api`.
Lito removes `work_dir` after the recipe and publishes `output` only if the callback succeeds.

Packages declaring `[script] supports = ["pack"]` can be normal version or path dependencies.
Load modules with `require()` during recipe declaration and capture their exports in the callback.
Use the package's script name, such as `require("@lito.apk")` for `lito-apk`.
`--no-build` uses the completed target configuration and resolves script dependencies from local
sources and the existing lock without configuring a compiler.

During the callback, `lito.run { tool = ..., args = {...}, cwd = ... }` runs a host executable
directly and reports any nonzero exit. `cwd` defaults to the package root. File helpers provide
`file_kind(path)`, `list_dir(path)`, `mkdir(path)`, `copy_file(source, destination)`, and
`link_file(source, destination)`. `list_dir` returns sorted names, and `link_file` returns `false`
when the destination already exists. `temp_dir(parent)` allocates a temporary directory on the
parent's filesystem and removes it after the callback.
