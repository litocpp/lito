# `test` and `bench`

Both commands resolve, build, and optionally run development targets.

## Shared options

They accept:

- repeated `-p/--package NAME`;
- `--profile PROFILE`;
- repeated `--features FEATURES` and `--no-default-features`;
- repeated `--target NAME`;
- `--out DIRECTORY`;
- `--locked`, `--offline`, `--frozen`, and repeated `--source-bundle DIRECTORY`;
- `--no-run`;
- `--verbose`, `--timing-file FILE`, `--no-timing`, and `-j/--jobs N`.

The global `--use-env-flags` option opts these commands into `CFLAGS`, `CXXFLAGS`, and `LDFLAGS`.

Remaining positional arguments are passed to each selected executable. Use `--` when a forwarded
argument starts with `-` and could otherwise be read as a Lito option.

## `lito test`

```text
lito test [OPTIONS] [ARGS]...
```

Lito includes test targets from the selected primary project and associated `tests/` project. Zero
exit status passes; non-zero exit or a signal fails.

```sh
lito test
lito test --no-run --package renderer-tests
lito test -- --filter=TextureUpload
```

## `lito bench`

```text
lito bench [OPTIONS] [ARGS]...
```

Lito includes benchmark targets and associated `benches/` project. Benchmarks commonly use the
release profile, but it is not selected implicitly.

```sh
lito bench --profile release
lito bench --target allocation -- --iterations=1000
```
