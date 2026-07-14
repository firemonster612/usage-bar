#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
arch="$(uname -m)"
stage="$root/dist/UsageBar-linux-$arch"
rm -rf "$stage"
test -x "$root/dist/AppDir/AppRun"
mkdir -p "$stage"
cp -a "$root/dist/AppDir/." "$stage/"
cp "$root/install.sh" "$root/uninstall.sh" "$root/LICENSE" "$root/THIRD_PARTY_LICENSES.md" "$root/README.md" "$stage/"
tar -C "$root/dist" -czf "$root/dist/UsageBar-linux-$arch.tar.gz" "$(basename "$stage")"
