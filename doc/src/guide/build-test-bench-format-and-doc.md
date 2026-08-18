# Daily commands

## Select a project without changing the shell

`-C` is global and applies before project discovery:

```sh
lito -C path/to/project build
```

Use repeated `--package NAME` and `--target NAME` to narrow a workspace build. `--profile` defaults
to `debug`.

## Build and install

```sh
lito build
lito build --profile release --package viewer
lito install --prefix staging --bin viewer
```

Build events are concise by default. `--verbose` prints individual operations. `--timing-file FILE`
writes the detailed timing report; `--no-timing` hides the stdout timing summary. `-j N` controls
scan and compile workers.

Lito does not consume ambient compiler flags implicitly. Use `--use-env-flags` when an integration
intentionally supplies `CFLAGS`, `CXXFLAGS`, or `LDFLAGS`; Lito appends them to separate C, C++, and
link domains and reports their sources in the build setup.

## Test and benchmark

```sh
lito test
lito test --no-run
lito test --package renderer-tests -- --filter=upload
lito bench --profile release
```

Arguments after command options are forwarded to each selected executable. A non-zero exit or
signal is a failed target. `--no-run` still resolves, scans, compiles, archives, and links.

## Format

```sh
lito format
lito format --check
```

From a workspace root, format operates on selected workspace packages. It discovers source files
from manifests and source conventions; it does not accept a directory operand. Configure the
formatter through `toolchain.format`.

## Scan

`scan` analyzes one source in its package/target context and writes JSON to stdout:

```sh
lito scan src/lib.cppm
lito scan src/lib.cppm --format p1689
```

Pass `--use-env-flags` when the scan must match an invocation that opts into ambient build flags.

The `lito` format contains Lito's frontend facts. `p1689` emits the standardized dependency-rule
shape used by C++ module tooling.

## Generate API documentation

```sh
lito doc
lito doc --output build/doc
lito doc --data-only --data-output build/doc-data
```

`lito doc` selects documentation units from library targets and invokes Litodoc. Use `--frontend`
for a local frontend bundle. The `[doc]` project config can select a local Litodoc package directory.

See the [CLI Reference](../reference/cli/index.md) for the accepted options of each command.
