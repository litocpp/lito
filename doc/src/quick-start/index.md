# Quick Start

This path creates one library module, one executable, and one associated test package. It uses
source discovery, so the manifest does not need a hand-maintained source list for the production
targets.

1. [Install Lito](installation.md).
2. [Build the first project](first-project.md).
3. [Add a test](testing.md).

You need Clang 22, LLD, LLVM `ar` and `strip`, and either libc++ or libstdc++. Lito currently drives
the Clang command-line interface; GCC, MSVC, and clang-cl are not supported toolchains.

The project in this Quick Start omits `[package].standard` and therefore uses the C++20 default.
Later C++ standards are selected through `[package].standard`; see
[workspace and package fields](../reference/lito-toml/workspace-and-package.md). C package support
exists for compatibility and is not Lito's primary workflow.
