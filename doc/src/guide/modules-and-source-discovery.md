# Modules and source discovery

Lito starts from a target entry, scans the active C++ module declarations and imports, and follows
that dependency closure. It does not compile every `.cppm` below `src/`.

## C++ module declarations

A library primary interface can combine a global module fragment, its module declaration, and
imports:

```cpp
module;

#include <cstdint>

export module geometry;

export import :shape;
import geometry.logging;

export struct Point {
    std::int32_t x;
    std::int32_t y;
};
```

These statements have different roles:

- `module;` starts the global module fragment. Textual includes that must precede the named module
  declaration belong here.
- `export module geometry;` declares a primary module interface named `geometry`. For a discovered
  library entry, this name must match `[lib].module`.
- `import geometry.logging;` adds a dependency on another named module for this translation unit.
- `import :shape;` adds a dependency on partition `geometry:shape`; the leading `:` means a
  partition of the current named module.
- `export import :shape;` adds the same build dependency and also re-exports that partition through
  this interface.
- `export` on a declaration controls the C++ interface. It does not create a source dependency;
  discovery follows `import` declarations.

Only imports that remain active after preprocessing enter the graph. An import inside a false
`#if` branch does not cause its source to be compiled. A textual `#include` remains a preprocessor
file dependency, not a named-module provider.

## Target entries

For a library using module discovery, Lito starts at `src/lib.cppm`:

```toml
[lib]
name = "geometry"
module = "geometry"
archive = "geometry"
```

`src/lib.cppm` must provide an interface with `export module geometry;`. A non-exporting
`module geometry;` declaration is an implementation unit and cannot serve as the primary interface.

A binary, test, or benchmark starts at `src/main.cppm`. The entry must declare a named module even
when the target omits its `module` field. When the field is present, the declaration must match it:

```cpp
export module geometry.viewer;

import geometry;

auto main() -> int {
    return 0;
}
```

Use an explicit `.cpp` source when a runnable entry intentionally has no named module.

## Named modules and partitions

A separate named module uses dots as part of its full name:

```cpp
export module geometry.logging;
```

A partition belongs to the primary module on the left of `:`:

```cpp
export module geometry:shape;

import :detail.math;

export auto area(int width, int height) -> int;
```

`geometry:shape` is an interface partition because its module declaration is exported.
`geometry:detail.math` can instead be an internal implementation partition:

```cpp
module geometry:detail.math;
```

Partitions are imported from another unit of the same named module with the leading-colon form,
such as `import :detail.math;`. Lito records the resolved full name, requires a provider for the
primary module `geometry`, and checks that the primary and partition use compatible BMI contexts.

Dots before `:` form a named-module hierarchy. Dots after `:` form a partition hierarchy. A module
name has at most one partition separator.

## Logical names to paths

For a target whose root module is `geometry`, Lito maps imported logical names below `src/`:

- `geometry.logging` maps to `logging`;
- `geometry.render.image` maps to `render/image`;
- `geometry:shape` maps to `shape`;
- `geometry:detail.math` maps to `detail/math`.

For each relative name, Lito checks a direct interface before a directory interface:

```text
geometry.render.image
  -> src/render/image.cppm
  -> src/render/image/mod.cppm

geometry:detail.math
  -> src/detail/math.cppm
  -> src/detail/math/mod.cppm
```

The direct `.cppm` form wins when both paths exist. Intermediate directories do not require their
own `mod.cppm`.

Named module `geometry.shape` and partition `geometry:shape` both map to `src/shape.cppm`; do not
declare both in one target. Their C++ identities remain different even though the path convention
uses the same relative segments.

When a logical name is outside the exact root but shares its leading namespace, Lito also tries the
suffix after that namespace. For example, a target rooted at `geometry.core` can resolve
`geometry.logging` from `src/logging.cppm`. A trailing underscore in a logical segment has a source
alias without the underscore, allowing module `geometry.char_` to use `src/char.cppm`.

## Companion implementation units

When Lito discovers an interface, it checks for a same-name `.cpp`:

```text
src/logging.cppm
src/logging.cpp
```

The interface can declare an exported function:

```cpp
export module geometry.logging;

export auto log_level() -> int;
```

The companion is an implementation unit of the same named module:

```cpp
module geometry.logging;

auto log_level() -> int {
    return 1;
}
```

The companion is compiled only when its `.cppm` enters the import closure. It is not a second
public module interface and should not use `export module`.

## Discovery closure

Discovery repeats these operations:

1. Start with `src/lib.cppm` or `src/main.cppm`.
2. Scan the source's provided module and active imports.
3. Resolve each import to the current target's convention source or a selected dependency's module
   provider.
4. Add the resolved interface and its companion implementation.
5. Continue until no new module imports remain.

Lito reports missing or ambiguous providers, a provided name that differs from the convention,
partitions without a primary interface, incompatible BMI contexts, and module dependency cycles.
An unimported `.cppm` is not compiled merely because it exists below `src/`.

## Explicit and non-module sources

Set `sources` when a target does not follow the convention:

```toml
[[bin]]
name = "asset-tool"
sources = ["tool/main.cpp", "tool/decoder.cpp"]
```

The explicit list owns that target's sources; Lito does not fall back to directory discovery for
missing entries. Use `source-groups` and target `when` entries for named or conditional sets.

C targets are supported as an explicit-source compatibility path. They cannot declare modules and
do not enter the BMI graph.

See [targets and sources](../reference/lito-toml/targets-and-sources.md).
