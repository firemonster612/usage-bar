#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
appdir="$root/dist/AppDir"
rm -rf "$appdir"
DESTDIR="$appdir" cmake --install "$root/build" --prefix /usr
install -D -m 0755 "$root/.cache/codexbar-cli/CodexBarCLI" "$appdir/usr/lib/usagebar/CodexBarCLI"
cp "$appdir/usr/share/applications/io.github.usagebar.UsageBar.desktop" "$appdir/"
cp "$appdir/usr/share/icons/hicolor/256x256/apps/usagebar.png" "$appdir/usagebar.png"
: "${LINUXDEPLOY:?Set LINUXDEPLOY to the linuxdeploy AppImage path}"
: "${APPIMAGETOOL:?Set APPIMAGETOOL to the appimagetool AppImage path}"
: "${APPIMAGE_RUNTIME_FILE:?Set APPIMAGE_RUNTIME_FILE to the pinned AppImage runtime path}"
qmake="${QMAKE:-$(command -v qmake6 || command -v qmake-qt6 || true)}"
test -x "$qmake"
cd "$root/dist"
ARCH="$(uname -m)" QMAKE="$qmake" NO_STRIP=1 APPIMAGE_EXTRACT_AND_RUN=1 "$LINUXDEPLOY" \
  --appdir "$appdir" \
  --executable "$appdir/usr/bin/usagebar" \
  --desktop-file "$appdir/io.github.usagebar.UsageBar.desktop" \
  --icon-file "$appdir/usagebar.png" \
  --plugin qt
APPIMAGE_EXTRACT_AND_RUN=1 "$APPIMAGETOOL" --runtime-file "$APPIMAGE_RUNTIME_FILE" \
  "$appdir" "$root/dist/UsageBar-linux-$(uname -m).AppImage"
