#!/usr/bin/env bash
set -euo pipefail

destination="${1:-.cache/codexbar-cli}"
case "$(uname -m)" in
  x86_64|amd64)
    arch=x86_64
    checksum=11bf5a4509ab883c5ce352540dfcb642bd331ad1406ba866c49c8dcc63d867f5
    ;;
  aarch64|arm64)
    arch=aarch64
    checksum=1fdda874a9084dfd9d724654045d8ef5d17a439ce1c21e458df19aeeaa92af2e
    ;;
  *) echo "Unsupported architecture: $(uname -m)" >&2; exit 1 ;;
esac

mkdir -p "$destination"
tag=v0.42.1
archive="CodexBarCLI-${tag}-linux-musl-${arch}.tar.gz"
temporary="$(mktemp)"
trap 'rm -f "$temporary"' EXIT
curl -fL "https://github.com/steipete/codexbar/releases/download/${tag}/${archive}" -o "$temporary"
printf '%s  %s\n' "$checksum" "$temporary" | sha256sum -c -
tar -xzf "$temporary" -C "$destination" CodexBarCLI VERSION
chmod 0755 "$destination/CodexBarCLI"
printf 'Fetched CodexBarCLI %s\n' "$tag"
