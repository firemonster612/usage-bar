#!/usr/bin/env bash
set -euo pipefail

data="${XDG_DATA_HOME:-$HOME/.local/share}"
rm -rf "$data/usagebar"
rm -f "$HOME/.local/bin/usagebar" \
  "$data/applications/io.github.usagebar.UsageBar.desktop" \
  "$data/icons/hicolor/256x256/apps/usagebar.png" \
  "${XDG_CONFIG_HOME:-$HOME/.config}/autostart/io.github.usagebar.UsageBar.desktop"
printf 'UsageBar removed.\n'
