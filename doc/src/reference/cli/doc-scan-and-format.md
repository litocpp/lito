# `doc`, `scan`, and `format`

## `lito doc`

```text
lito doc [OPTIONS]
```

Selection and build options:

- repeated `-p/--package NAME` and `--target NAME`;
- `--profile PROFILE`, repeated `--features`, and `--no-default-features`;
- `--locked`, `--offline`, `--frozen`, and repeated `--source-bundle DIRECTORY`;
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
`--frozen`, and repeated `--source-bundle`.

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
- `--check` reports files that require formatting without modifying them;
- `--backend clang-format|builtin` selects the formatter; the default is `clang-format`.

The command has no directory argument and no workspace flag. It discovers the current project via
`-C`/working directory and formats the selected package sources.

The `clang-format` backend uses `[tools].clang-format` and the tool's `.clang-format` configuration.
The `builtin` backend runs in-process and reads a supported subset of `.clang-format` options.
It does not resolve package dependencies, run build scripts, or require external tools.
Both backends use the same package selection and at most four workers. Check mode never writes;
the builtin backend writes atomically only when the formatted bytes differ.

Builtin formatting covers basic declarations, module/import statements, and recognized blocks.
It preserves token spelling, literal and comment contents, preprocessor lines, conditional
compilation regions, and the original newline convention. It keeps include/import order and does
not reflow comments, align consecutive declarations, or apply column-limit wrapping. Complex or
ambiguous structures are handled conservatively, so it is not a replacement for every
clang-format rule.

For each source, builtin searches its directory and then parent directories for `.clang-format`
or `_clang-format`; `.clang-format` takes precedence in the same directory. The nearest file
replaces parent configuration unless it specifies `BasedOnStyle: InheritParentConfig`.
Configurations for all selected sources are checked before any source is written.

With no configuration, builtin keeps its four-space default. A configuration overrides those
defaults. `BasedOnStyle: LLVM` applies LLVM defaults for the supported options; it does not enable
column wrapping or other unimplemented LLVM style rules.

Supported options and values:

| Option | Values |
| --- | --- |
| `IndentWidth`, `TabWidth` | Unsigned integers; `TabWidth` must be nonzero |
| `UseTab` | `Never`, `ForIndentation` |
| `NamespaceIndentation` | `None`, `Inner`, `All` |
| `BreakBeforeBraces` | `Attach`, `Allman` |
| `SpaceBeforeParens` | `Never`, `ControlStatements`, `Always` |
| `SpaceBeforeAssignmentOperators` | Boolean; controls the space before assignment operators |
| `SpacesInParentheses`, `SpaceInEmptyParentheses` | Boolean |
| `AllowShortFunctionsOnASingleLine` | `None`, `Empty`, `All` |
| `AllowShortBlocksOnASingleLine` | `Never`, `Empty`, `Always` |
| `MaxEmptyLinesToKeep` | Unsigned integer |
| `KeepEmptyLines` | Boolean fields `AtStartOfFile`, `AtEndOfFile`, `AtStartOfBlock`, `AtEndOfBlock` |
| `KeepEmptyLinesAtTheStartOfBlocks` | Alias for `KeepEmptyLines.AtStartOfBlock` |
| `ColumnLimit` | `0`, meaning no column limit |
| `Language` | `Cpp` |
| `BasedOnStyle` | `LLVM`, `InheritParentConfig` |

Builtin accepts a single YAML document with scalar values, comments, and the supported nested
mapping. Quoted scalar values and an optional document start/end marker are accepted. Unsupported
options, enum values, YAML constructs, and duplicate settings produce an error with configuration
location. They are never silently ignored. Full clang-format configurations can continue to use
the default external backend.

For example:

```yaml
IndentWidth: 2
BreakBeforeBraces: Allman
SpaceBeforeParens: ControlStatements
AllowShortFunctionsOnASingleLine: None
MaxEmptyLinesToKeep: 1
KeepEmptyLines:
  AtStartOfBlock: false
```

Brace and spacing options apply to recognized structures. Short functions and control blocks
are merged only when their simple bodies contain at most one statement, without comments,
protected text, or nested blocks. Macro and disabled regions retain their original bytes.

Use `// lito-format off` and `// lito-format on` comments to preserve a region byte for byte;
`clang-format off/on` comments are also recognized. Unknown function-like macros from external
headers cannot always be distinguished from calls, so use these markers where spacing is
significant. If lexical or structural analysis fails, the file is not replaced. Builtin formatting
does not run compiler dependency scanning.

```sh
lito format --backend builtin --check
lito format --backend builtin
```
