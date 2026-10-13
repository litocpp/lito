#!/usr/bin/env bash
# Run with a freshly built Lito executable and Clang on PATH.
set -euo pipefail

lito_cli=${1:?usage: outputs-json.sh /path/to/lito}
case "$lito_cli" in
  /*) ;;
  *) lito_cli="$PWD/$lito_cli" ;;
esac
test_root=$(mktemp -d "${TMPDIR:-/tmp}/lito-outputs-json.XXXXXX")
trap 'rm -rf -- "$test_root"' EXIT
mkdir -p "$test_root/project dir" "$test_root/caller"
printf '%s\n' '[package]' 'name = "assets"' 'version = "0.1.0"' \
  > "$test_root/project dir/lito.toml"
printf '%s\n' 'lito.configure_file({input="input.txt", output="site/index.html", values={}})' \
  > "$test_root/project dir/build.lua"
printf '%s\n' 'asset' > "$test_root/project dir/input.txt"

cd "$test_root/caller"
"$lito_cli" -C "$test_root/project dir" --no-config -c toolchain.cxx=clang++ \
  build --offline --profile release --build-dir "$test_root/build" \
  --outputs-json reports/outputs.json --no-timing
test -f "$test_root/project dir/reports/outputs.json"
test ! -e "$test_root/caller/reports/outputs.json"

"$lito_cli" -C "$test_root/project dir" --no-config -c toolchain.cxx=clang++ \
  build --offline --profile release --build-dir "$test_root/build" \
  --outputs-json "$test_root/absolute.json" --no-timing
test -f "$test_root/absolute.json"
cmp "$test_root/project dir/reports/outputs.json" "$test_root/absolute.json"
printf '%s\n' 'outputs-json CLI checks passed'
