# Build the first project

Create an empty directory with this layout:

```text
hello/
├── lito.toml
└── src/
    ├── lib.cppm
    └── main.cppm
```

## Declare the package and targets

Create `lito.toml`:

```toml
[package]
name = "hello"
version = "0.1.0"

[lib]
name = "hello"
module = "hello"
archive = "hello"

[[bin]]
name = "hello"
module = "hello.app"
```

`[lib]` creates `libhello.a` and owns the `hello` module. `[[bin]]` creates the executable. Because
neither target declares `sources`, Lito starts discovery at `src/lib.cppm` for the library and
`src/main.cppm` for the executable.

## Define the library module

Create `src/lib.cppm`:

```cpp
export module hello;

export auto answer() -> int {
    return 42;
}
```

## Define the executable

Create `src/main.cppm`:

```cpp
module;

#include <cstdio>

export module hello.app;
import hello;

auto main() -> int {
    std::puts("Hello from Lito");
    return answer() == 42 ? 0 : 1;
}
```

The global module fragment contains the header include. The named module and imports remain
isolated from header macros.

## Build and run

From `hello/`:

```sh
lito build
./build/debug/bin/hello/hello
```

The default profile is `debug`, and the default output root is `build/debug`. A successful run
prints:

```text
Hello from Lito
```

Build the built-in release profile with:

```sh
lito build --profile release
./build/release/bin/hello/hello
```

Use `--out DIRECTORY` when an integration owns the output root. See
[build command options](../reference/cli/build-and-install.md) and
[target fields](../reference/lito-toml/targets-and-sources.md).
