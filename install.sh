#!/usr/bin/env bash

set -euo pipefail

readonly VERSION="v0.2.1"
readonly REPOSITORY="https://github.com/litocpp/lito"
readonly INSTALL_DIRECTORY="${HOME}/.local/bin"

fail() {
  printf 'lito installer: %s\n' "$*" >&2
  exit 1
}

for command in curl install mktemp mv rm sha256sum tar uname; do
  command -v "$command" >/dev/null 2>&1 || fail "required command '$command' was not found"
done

[[ "$(uname -s)" == "Linux" ]] || fail "only Linux release archives are available"

case "$(uname -m)" in
  x86_64 | amd64) architecture="x86_64" ;;
  aarch64 | arm64) architecture="aarch64" ;;
  *) fail "unsupported architecture '$(uname -m)'" ;;
esac

archive="lito-linux-${architecture}.tar.gz"
checksum="${archive}.sha256"
release_url="${REPOSITORY}/releases/download/${VERSION}"
temporary_directory="$(mktemp -d)"
staged_binary=""

cleanup() {
  if [[ -n "$staged_binary" && -f "$staged_binary" ]]; then
    rm -f -- "$staged_binary"
  fi
  if [[ -n "$temporary_directory" && -d "$temporary_directory" ]]; then
    rm -rf -- "$temporary_directory"
  fi
}
trap cleanup EXIT

printf 'Downloading lito %s for linux-%s...\n' "$VERSION" "$architecture"
curl --fail --location --proto '=https' --silent --show-error \
  --output "${temporary_directory}/${archive}" "${release_url}/${archive}"
curl --fail --location --proto '=https' --silent --show-error \
  --output "${temporary_directory}/${checksum}" "${release_url}/${checksum}"

pushd "$temporary_directory" >/dev/null
sha256sum --check "$checksum"
popd >/dev/null

tar -xzf "${temporary_directory}/${archive}" -C "$temporary_directory"
source_binary="${temporary_directory}/lito-linux-${architecture}/bin/lito"
[[ -f "$source_binary" ]] || fail "release archive does not contain bin/lito"

install -d "$INSTALL_DIRECTORY"
staged_binary="$(mktemp "${INSTALL_DIRECTORY}/.lito.install.XXXXXX")"
install -m 0755 "$source_binary" "$staged_binary"
mv -f -- "$staged_binary" "${INSTALL_DIRECTORY}/lito"
staged_binary=""

printf 'Installed lito %s to %s\n' "$VERSION" "${INSTALL_DIRECTORY}/lito"
case ":${PATH:-}:" in
  *":${INSTALL_DIRECTORY}:"*) ;;
  *) printf 'Add %s to PATH to run lito.\n' "$INSTALL_DIRECTORY" ;;
esac
