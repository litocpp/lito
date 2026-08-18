# Add a test

Lito discovers an associated test project at `tests/lito.toml`. The test project is separate from
the production package, so its dependencies and sources do not affect a normal `lito build`.

Add these files:

```text
hello/
└── tests/
    ├── lito.toml
    └── main.cpp
```

Create `tests/lito.toml`:

```toml
[package]
name = "hello-tests"
version = "0.1.0"

[[test]]
name = "hello-tests"
sources = ["main.cpp"]

[dependencies.hello]
path = ".."
visibility = "private"
```

Create `tests/main.cpp`:

```cpp
import hello;

auto main() -> int {
    return answer() == 42 ? 0 : 1;
}
```

Run the test from the production project root:

```sh
lito test
```

Lito builds the selected test target, runs it, and treats exit code zero as success. Build without
running with `lito test --no-run`. Arguments after the options are passed to the test executable:

```sh
lito test -- --example-argument
```

For test-only sources that must be compiled into an existing library context, see
[`test.attach`](../reference/lito-toml/targets-and-sources.md). For compile-pass and compile-fail
cases, see [`compile-test`](../reference/lito-toml/targets-and-sources.md).
