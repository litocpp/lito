#!/bin/sh

set -eu

readonly VERSION="v0.7.1"
readonly REPOSITORY="https://github.com/litocpp/lito"
readonly INSTALL_DIRECTORY="${HOME}/.local/bin"

fail() {
  printf 'lito installer: %s\n' "$*" >&2
  exit 1
}

for command in chmod curl install mktemp mv rm rmdir sha256sum tar uname; do
  command -v "$command" >/dev/null 2>&1 || fail "required command '$command' was not found"
done

[ "$(uname -s)" = "Linux" ] || fail "only Linux release archives are available"

case "$(uname -m)" in
  x86_64 | amd64) architecture="x86_64" ;;
  aarch64 | arm64) architecture="aarch64" ;;
  *) fail "unsupported architecture '$(uname -m)'" ;;
esac

archive="lito-${VERSION}-linux-${architecture}.tar.gz"
checksum="${archive}.sha256"
release_url="${REPOSITORY}/releases/download/${VERSION}"
temporary_directory="$(mktemp -d)"
archive_path="${temporary_directory}/${archive}"
checksum_path="${temporary_directory}/${checksum}"
staged_binary=""

cleanup() {
  if [ -n "$staged_binary" ] && [ -f "$staged_binary" ]; then
    rm -f -- "$staged_binary"
  fi
  rm -f -- "$archive_path" "$checksum_path"
  rmdir -- "$temporary_directory"
}
trap cleanup EXIT

printf 'Downloading lito %s for linux-%s...\n' "$VERSION" "$architecture"
curl --fail --location --proto '=https' --silent --show-error \
  --output "$archive_path" "${release_url}/${archive}"
curl --fail --location --proto '=https' --silent --show-error \
  --output "$checksum_path" "${release_url}/${checksum}"

(
  cd "$temporary_directory"
  sha256sum --check "$checksum"
)

install -d "$INSTALL_DIRECTORY"
staged_binary="$(mktemp "${INSTALL_DIRECTORY}/.lito.install.XXXXXX")"
tar -xOzf "$archive_path" "lito-${VERSION}-linux-${architecture}/bin/lito" >"$staged_binary"
[ -s "$staged_binary" ] || fail "release archive does not contain bin/lito"
chmod 0755 "$staged_binary"
mv -f -- "$staged_binary" "${INSTALL_DIRECTORY}/lito"
staged_binary=""

printf 'Installed lito %s to %s\n' "$VERSION" "${INSTALL_DIRECTORY}/lito"
case ":${PATH:-}:" in
  *":${INSTALL_DIRECTORY}:"*) ;;
  *) printf 'Add %s to PATH to run lito.\n' "$INSTALL_DIRECTORY" ;;
esac
