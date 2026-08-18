# Lito

Lito is a module-first build tool for C++. A fixed `lito.toml` describes packages, workspaces,
targets, dependencies, and build policy. Lito discovers C++ module sources from their logical
names, resolves a package and BMI graph, and then drives Clang, LLD, and LLVM tools.

Choose a path:

- [Quick Start](quick-start/index.md) builds and tests a small C++20 module project.
- [Guide](guide/index.md) explains how Lito's package, module, dependency, and build models fit
  together.
- [`lito.toml` Reference](reference/lito-toml/index.md) defines the manifest contract.
- [Configuration Reference](reference/config/index.md) defines shared, local, and command-line
  configuration.
- [Command-line Reference](reference/cli/index.md) documents every public command.

The documentation follows the current `main` branch. Lito rejects unknown manifest and
configuration keys, so examples intentionally use only the current public contract.
