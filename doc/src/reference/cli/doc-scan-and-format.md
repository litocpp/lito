# `doc`, `scan`, and `format`

## `lito doc`

```text
lito doc [OPTIONS]
```

Selection and build options:

- repeated `-p/--package NAME` and `--target NAME`;
- `--profile PROFILE`, repeated `--features`, and `--no-default-features`;
- `--locked`, `--offline`, `--frozen`, and repeated `--fetch-seed DIRECTORY`;
- `--verbose`, `--timing-file FILE`, `--no-timing`, and `-j/--jobs N`.

Documentation options:

- `--output DIRECTORY` writes the generated site;
- `--data-output DIRECTORY` writes the intermediate JSON dataset;
- `--frontend DIRECTORY` uses a local frontend bundle;
- `--data-only` generates data without a site.

The global `--use-env-flags` option is accepted because documentation performs frontend analysis
in the selected package context.

```sh
lito doc --package geometry --output build/doc
lito doc --data-only --data-output build/doc-data
```

## `lito scan`

```text
lito scan [OPTIONS] SOURCE
```

`SOURCE` is required. Selection options are repeated `--package`, `--target`, `--features`,
`--no-default-features`, and `--profile`. Source acquisition accepts `--locked`, `--offline`,
`--frozen`, and repeated `--fetch-seed`.

`--format FORMAT` selects JSON output:

- `lito`, the default Lito scan report;
- `p1689`, a P1689 dependency rule.

The JSON is written to stdout.

`scan` also accepts the global `--use-env-flags` option. `format` does not consume build options and
does not accept it.

```sh
lito scan src/lib.cppm
lito scan src/lib.cppm --format p1689
```

## `lito format`

```text
lito format [OPTIONS]
```

- repeated `-p/--package NAME` narrows a workspace;
- `--check` reports files that require formatting without modifying them.

The command has no directory argument and no workspace flag. It discovers the current project via
`-C`/working directory and formats the selected package sources.
