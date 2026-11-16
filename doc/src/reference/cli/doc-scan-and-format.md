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

Builtin formatting covers declarations, module/import statements, recognized blocks and lists.
It preserves token spelling, literal and comment contents, macro replacement text, protected conditional
compilation regions, and the original newline convention. It keeps include/import order and does
not reflow comments or reorder declarations. It supports column-limit wrapping and consecutive
alignment for recognized syntax. Complex or ambiguous structures retain protected source ranges,
so it is not a replacement for every clang-format rule.

For each source, builtin searches its directory and then parent directories for `.clang-format`
or `_clang-format`; `.clang-format` takes precedence in the same directory. The nearest file
replaces parent configuration unless it specifies `BasedOnStyle: InheritParentConfig`.
Configurations for all selected sources are checked before any source is written.

With no configuration, builtin keeps its four-space default. A configuration overrides those
defaults. `BasedOnStyle: LLVM` applies defaults for the supported options, including an 80-column
limit. It does not enable unimplemented LLVM style rules. Without configuration, builtin keeps
its unlimited column width.

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
| `EmptyLineBeforeAccessModifier` | `Never`, `Leave`, `LogicalBlock`, `Always` |
| `EmptyLineAfterAccessModifier` | `Never`, `Leave`, `Always` |
| `IndentGotoLabels` | Boolean |
| `SpaceBeforeParens` | `Never`, `ControlStatements`, `Always` |
| `SpaceBeforeAssignmentOperators` | Boolean; controls the space before assignment operators |
| `SpacesInParens` | `Never`, `Custom` |
| `SpacesInParensOptions` | Boolean fields `ExceptDoubleParentheses`, `InConditionalStatements`, `InCStyleCasts`, `InEmptyParentheses`, `Other` |
| `AllowShortFunctionsOnASingleLine` | `None`, `Empty`, `Inline`, `InlineOnly`, `All`; legacy `false`, `true` mean `None`, `All` |
| `AllowShortBlocksOnASingleLine` | `Never`, `Empty`, `Always`; legacy `false`, `true` mean `Never`, `Always` |
| `MaxEmptyLinesToKeep` | Unsigned integer |
| `KeepEmptyLines` | Boolean fields `AtStartOfFile`, `AtEndOfFile`, `AtStartOfBlock`, `AtEndOfBlock` |
| `ColumnLimit` | Unsigned integer; `0` means no column limit |
| `ContinuationIndentWidth`, `ConstructorInitializerIndentWidth` | Unsigned integers |
| `AlignAfterOpenBracket` | Boolean; legacy `Align`, `DontAlign`, `AlwaysBreak`, `BlockIndent` |
| `BreakAfterOpenBracketFunction`, `BreakAfterOpenBracketBracedList`, `BreakAfterOpenBracketIf`, `BreakAfterOpenBracketLoop`, `BreakAfterOpenBracketSwitch` | Boolean |
| `BreakBeforeCloseBracketFunction`, `BreakBeforeCloseBracketBracedList`, `BreakBeforeCloseBracketIf`, `BreakBeforeCloseBracketLoop`, `BreakBeforeCloseBracketSwitch` | Boolean |
| `AlignOperands` | `DontAlign`, `Align`, or legacy boolean |
| `AlignConsecutiveMacros`, `AlignConsecutiveAssignments`, `AlignConsecutiveDeclarations` | Boolean; consecutive groups stop at scope, blank-line and protected boundaries |
| `AlignTrailingComments` | `Leave`, `Never`, `Always`, or legacy boolean |
| `AlignEscapedNewlines` | `DontAlign`, `Left`, `Right` |
| `BinPackArguments` | Boolean |
| `AllowAllArgumentsOnNextLine` | Boolean; applies to calls and initializer lists |
| `BinPackParameters` | `BinPack`, `OnePerLine`, `AlwaysOnePerLine` |
| `AllowAllParametersOfDeclarationOnNextLine` | Boolean |
| `PointerAlignment` | `Left`, `Right`, `Middle` |
| `ReferenceAlignment` | `Pointer`, `Left`, `Right`, `Middle` |
| `DerivePointerAlignment` | Boolean |
| `SpaceAroundPointerQualifiers` | `Default`, `Before`, `After`, `Both` |
| `BitFieldColonSpacing` | `Both`, `None`, `Before`, `After` |
| `SpacesInAngles` | `Never`, `Leave`, or legacy `false` |
| `Cpp11BracedListStyle` | `Block`, `FunctionCall`, `AlignFirstComment`; legacy `false`, `true` mean `Block`, `FunctionCall` |
| `SpacesInSquareBrackets`, `SpaceAfterCStyleCast`, `SpaceAfterLogicalNot`, `SpaceAfterTemplateKeyword` | Boolean |
| `SpaceBeforeCpp11BracedList`, `SpaceBeforeCtorInitializerColon`, `SpaceBeforeInheritanceColon`, `SpaceBeforeRangeBasedForLoopColon` | Boolean |
| `SpaceBeforeCaseColon`, `SpaceBeforeSquareBrackets` | Boolean |
| `IndentCaseLabels`, `IndentWrappedFunctionNames` | Boolean |
| `BreakTemplateDeclarations` | `Leave`, `No`, `MultiLine`, `Yes`; legacy `AlwaysBreakTemplateDeclarations` remains accepted |
| `BreakAfterReturnType` | `Automatic`, `All`; legacy `AlwaysBreakAfterReturnType: None` means `Automatic` |
| `BreakAfterAttributes` | `Leave`, `Never`, `Always` |
| `BreakBeforeBinaryOperators` | `None`, `NonAssignment`, `All` |
| `BreakBeforeTernaryOperators` | Boolean |
| `PackConstructorInitializers` | `Never`, `BinPack`, `CurrentLine`, `NextLine`; legacy `ConstructorInitializerAllOnOneLineOrOnePerLine` remains accepted |
| `AllowShortIfStatementsOnASingleLine` | `Never`, `WithoutElse`, `OnlyFirstIf`, `AllIfsAndElse`; legacy `true` means `WithoutElse` |
| `AllowShortLoopsOnASingleLine`, `AllowShortCaseLabelsOnASingleLine` | Boolean |
| `AllowShortLambdasOnASingleLine` | `None`, `Empty`, `Inline`, `All`, or legacy boolean |
| `CompactNamespaces`, `ReflowComments`, `SpacesInContainerLiterals` | Only `false` |
| `SortIncludes`, `SortUsingDeclarations` | Only `Never` |
| `IndentPPDirectives` | Only `None` |
| `Standard` | Only `Latest` |
| `Language` | `Cpp` |
| `BasedOnStyle` | `LLVM`, `InheritParentConfig` |

`KeepEmptyLines.AtEndOfBlock` is a supported addition from newer Clang versions, outside the
22.1 baseline. Lito's four-space fallback and the partial LLVM preset remain explicit differences
from a complete clang-format style.

The current repository configuration is supported as a whole. Support remains a subset of the
reference: fixed policies preserve include/using order, comment contents and separate namespace
lines. Other values of the fixed-policy options produce an error. `SpacesInContainerLiterals`
is a C++ language constraint here; C++ initializer spacing uses `Cpp11BracedListStyle`.

Column limits use legal token boundaries and list strategies. A long token, comment, raw string
or protected region may exceed the limit. Inline-only functions are definitions directly inside
a class, struct or union. Short-code rules also respect the column limit. Consecutive declaration
and assignment alignment applies to statements that fit on one planned line.

Bracket-opening rules apply when a recognized group exceeds the column limit. Closing-bracket
rules require a real break immediately after the matching opening bracket. Braced-list rules
apply to non-`Block` initializer styles. Function rules cover calls, declarations and definitions;
control rules distinguish `if`, loops and `switch`. A zero column limit does not force these
groups to expand.

`AllowAllArgumentsOnNextLine` permits all call or initializer arguments to share a continuation
line when they fit, including with `BinPackArguments: false`. Declaration parameters use
`AllowAllParametersOfDeclarationOnNextLine`; constructor initializers use their own packing rule.
The LLVM projection enables `AllowAllArgumentsOnNextLine` and `BreakBeforeTernaryOperators`.
The fallback style leaves both disabled.

Legacy `AlwaysBreak` enables after-opening rules for functions, braced lists and `if`.
`BlockIndent` also enables their before-closing rules. Both set bracket alignment to true;
loop and switch rules remain separate. An explicit `AlignAfterOpenBracket` value initializes
these bracket flags, then explicit modern fields override it regardless of YAML key order.
An inheriting configuration that omits the alignment key keeps the parent's flags.

Binary `None` breaks after operators, `NonAssignment` breaks before non-assignment operators,
and `All` includes assignment operators. Ternary `?` and `:` follow their separate boolean
rule. These choices use recognized expression boundaries; ambiguous syntax remains protected.

Macro alignment only edits recognized header separators and existing horizontal whitespace
before continuations. Macro replacement tokens, stringization, token paste and format-off
regions retain their bytes. `IndentPPDirectives: None` normalizes safe directive prefixes to
column zero. Formatting does not evaluate conditional branches or expand macros.

Supported `BraceWrapping` fields are:

- booleans `AfterClass`, `AfterStruct`, `AfterUnion`, `AfterEnum`, `AfterNamespace`, `AfterFunction`;
- `AfterControlStatement`: `Never`, `MultiLine`, `Always`, or legacy `false`/`true`;
- booleans `BeforeElse`, `BeforeCatch`, `IndentBraces`;
- booleans `SplitEmptyFunction`, `SplitEmptyRecord`, `SplitEmptyNamespace`.

`MultiLine` considers the recognized control header's final line layout. `IndentBraces` follows
the control-block rule: wrapped control braces and their body gain an extra level. Empty-block
split options affect braces whose opening brace has already been wrapped. A short-function rule
that successfully merges the whole function takes precedence. Wrapping fields remain available
for inheritance when the current brace mode does not use them.

`ReferenceAlignment: Pointer` follows the effective pointer alignment; other values select a
separate reference style. `DerivePointerAlignment` counts directional spacing evidence from
recognized declarations in the original file. A tie keeps `PointerAlignment`. Derivation also
resets reference alignment to `Pointer`, following the 22.1.8 behavior. Each source receives
its own effective style; protected text and macro bodies do not contribute evidence.

Pointer-qualifier spacing applies to recognized pointer declarators, without changing ordinary
type qualifiers or expression operators. Square-bracket prefix spacing applies to the first
bracket in an array/subscript chain; it does not affect lambda captures, attributes or structured
bindings. Case and bit-field colons use their separate settings.

Access labels are recognized in the direct scope of a class, struct, or union.
`IndentAccessModifiers: true` gives members two indentation levels and labels one, ignoring
`AccessModifierOffset`. Otherwise, the offset is relative to member indentation; an offset that
would make the column negative leaves the member indentation unchanged.

Access-label blank-line rules use the direct record scope and preserve comment boundaries.
Before rules own the gap between consecutive labels. `LogicalBlock` inserts a blank line after
a completed member block and removes one between consecutive labels. `Always` before skips the
record opening; `Always` after skips the record closing. Existing empty lines are limited before
these local rules supply their requested minimum. The fallback uses `Leave` for both settings;
the LLVM projection uses `LogicalBlock` before and `Never` after.

Ordinary labels are distinct from case and access labels. `IndentGotoLabels: false` places them
at column zero; `true` outdents them by one indentation level from the enclosing code block.
Following statements retain their block indentation, and `goto name;` remains an ordinary jump.

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
protected text, or nested blocks. Macro replacement text and disabled regions retain their original bytes.

Use `// lito-format off` and `// lito-format on` comments to preserve a region byte for byte;
`clang-format off/on` comments are also recognized. Unknown function-like macros from external
headers cannot always be distinguished from calls, so use these markers where spacing is
significant. If lexical or structural analysis fails, the file is not replaced. Builtin formatting
does not run compiler dependency scanning.

```sh
lito format --backend builtin --check
lito format --backend builtin
```
