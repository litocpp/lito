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

Builtin uses the [Clang 22.1 configuration reference](https://releases.llvm.org/22.1.0/tools/clang/docs/ClangFormatStyleOptions.html)
as its semantic baseline. Defaults, compatibility spellings, and option interactions are checked
against [LLVM 22.1.8](https://github.com/llvm/llvm-project/tree/llvmorg-22.1.8/clang).
The table describes the implemented subset; tests use hand-written expected results.

| Option | Values |
| --- | --- |
| `IndentWidth`, `TabWidth` | Unsigned integers; `TabWidth` must be nonzero |
| `UseTab` | `Never`, `ForIndentation` |
| `NamespaceIndentation` | `None`, `Inner`, `All` |
| `BreakBeforeBraces` | `Attach`, `Allman`, `Custom` |
| `BraceWrapping` | Supported fields listed below; effective with `Custom` |
| `AccessModifierOffset` | Signed integer; default `-2` |
| `IndentAccessModifiers` | Boolean |
| `SpaceBeforeParens` | `Never`, `ControlStatements`, `Always` |
| `SpaceBeforeAssignmentOperators` | Boolean; controls the space before assignment operators |
| `SpacesInParens` | `Never`, `Custom` |
| `SpacesInParensOptions` | Boolean fields `ExceptDoubleParentheses`, `InConditionalStatements`, `InCStyleCasts`, `InEmptyParentheses`, `Other` |
| `AllowShortFunctionsOnASingleLine` | `None`, `Empty`, `All`; legacy `false`, `true` mean `None`, `All` |
| `AllowShortBlocksOnASingleLine` | `Never`, `Empty`, `Always`; legacy `false`, `true` mean `Never`, `Always` |
| `MaxEmptyLinesToKeep` | Unsigned integer |
| `KeepEmptyLines` | Boolean fields `AtStartOfFile`, `AtEndOfFile`, `AtStartOfBlock`, `AtEndOfBlock` |
| `ColumnLimit` | `0`, meaning no column limit |
| `Language` | `Cpp` |
| `BasedOnStyle` | `LLVM`, `InheritParentConfig` |

`KeepEmptyLines.AtEndOfBlock` is a supported addition from newer Clang versions, outside the
22.1 baseline. Lito's four-space fallback and the partial LLVM preset remain explicit differences
from a complete clang-format style.

Supported `BraceWrapping` fields are:

- booleans `AfterClass`, `AfterStruct`, `AfterUnion`, `AfterEnum`, `AfterNamespace`, `AfterFunction`;
- `AfterControlStatement`: `Never`, `MultiLine`, `Always`, or legacy `false`/`true`;
- booleans `BeforeElse`, `BeforeCatch`, `IndentBraces`;
- booleans `SplitEmptyFunction`, `SplitEmptyRecord`, `SplitEmptyNamespace`.

`MultiLine` considers line breaks within the recognized control header. `IndentBraces` follows
the control-block rule: wrapped control braces and their body gain an extra level. Empty-block
split options affect braces whose opening brace has already been wrapped. A short-function rule
that successfully merges the whole function takes precedence. Wrapping fields remain available
for inheritance when the current brace mode does not use them.

Access labels are recognized in the direct scope of a class, struct, or union.
`IndentAccessModifiers: true` gives members two indentation levels and labels one, ignoring
`AccessModifierOffset`. Otherwise, the offset is relative to member indentation; an offset that
would make the column negative leaves the member indentation unchanged.

Legacy `SpacesInParentheses`, `SpaceInEmptyParentheses`, `SpacesInConditionalStatement`, and
`SpacesInCStyleCastParentheses` remain accepted. They follow the 22.1 conversion rules: a resolved
`SpacesInParens: Custom` uses the modern options; otherwise, any true legacy flag activates their
conversion to `Custom`. This includes the mode inherited from a parent. YAML key order does not
change this priority. Cast spacing applies to recognized simple type conversions; ambiguous
forms preserve their original spacing.

`KeepEmptyLinesAtEOF` and `KeepEmptyLinesAtTheStartOfBlocks` remain accepted compatibility names.
If both a legacy name and its modern `KeepEmptyLines` field occur in the same document, the modern
field wins. Each child configuration applies to its fully resolved parent style.

Builtin accepts a single YAML document with scalar values, comments, and the supported nested
mapping. Quoted scalar values and an optional document start/end marker are accepted. Unsupported
options, enum values, YAML constructs, and repeated occurrences of the same key produce an error with configuration
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
