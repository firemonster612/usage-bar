#!/usr/bin/env bash
set -euo pipefail

data="${XDG_DATA_HOME:-$HOME/.local/share}"
rm -rf "$data/usagebar"
rm -f "$HOME/.local/bin/usagebar" \
  "$data/applications/io.github.usagebar.UsageBar.desktop" \
  "$data/icons/hicolor/256x256/apps/usagebar.png" \
  "${XDG_CONFIG_HOME:-$HOME/.config}/autostart/io.github.usagebar.UsageBar.desktop"
if [[ -d "$data/icons/hicolor" ]]; then
  touch "$data/icons/hicolor"
  if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -f -t "$data/icons/hicolor" >/dev/null 2>&1 || true
  fi
fi
if [[ -d "$data/applications" ]] && command -v update-desktop-database >/dev/null 2>&1; then
  update-desktop-database "$data/applications" >/dev/null 2>&1 || true
fi
printf 'UsageBar removed.\n'
