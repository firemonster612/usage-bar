#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
prefix="${XDG_DATA_HOME:-$HOME/.local/share}/usagebar"
bin_dir="${HOME}/.local/bin"
data="${XDG_DATA_HOME:-$HOME/.local/share}"

write_desktop() {
  local source="$1" destination="$2" executable="$3" line escaped
  escaped="${executable//\\/\\\\}"
  escaped="${escaped//\"/\\\"}"
  while IFS= read -r line; do
    if [[ "$line" == Exec=* ]]; then
      printf 'Exec="%s" --show\n' "$escaped"
    else
      printf '%s\n' "$line"
    fi
  done < "$source" > "$destination"
}

if [[ -x "$root/AppRun" ]]; then
  rm -rf "$prefix"
  install -d "$prefix" "$bin_dir" "$data/applications" "$data/icons/hicolor/256x256/apps"
  cp -a "$root/." "$prefix/"
  ln -sfn "$prefix/AppRun" "$bin_dir/usagebar"
  install -m 0644 "$root/usr/share/icons/hicolor/256x256/apps/usagebar.png" \
    "$data/icons/hicolor/256x256/apps/usagebar.png"
  write_desktop "$root/usr/share/applications/io.github.usagebar.UsageBar.desktop" \
    "$data/applications/io.github.usagebar.UsageBar.desktop" "$prefix/AppRun"
  printf 'Installed. Open UsageBar from your application menu.\n'
  exit 0
fi

if [[ -x "$root/usr/bin/usagebar" ]]; then
  app_binary="$root/usr/bin/usagebar"
  cli_binary="$root/usr/lib/usagebar/CodexBarCLI"
elif [[ ! -x "$root/build/usagebar" ]]; then
  "$root/scripts/fetch-codexbar-cli.sh" "$root/.cache/codexbar-cli"
  cmake -S "$root" -B "$root/build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
  cmake --build "$root/build"
fi

app_binary="${app_binary:-$root/build/usagebar}"
cli_binary="${cli_binary:-$root/.cache/codexbar-cli/CodexBarCLI}"

install -d "$prefix/bin" "$prefix/lib/usagebar" "$bin_dir" \
  "$data/applications" "$data/icons/hicolor/256x256/apps"
install -m 0755 "$app_binary" "$prefix/bin/usagebar"
if [[ ! -x "$cli_binary" ]]; then
  "$root/scripts/fetch-codexbar-cli.sh" "$root/.cache/codexbar-cli"
  cli_binary="$root/.cache/codexbar-cli/CodexBarCLI"
fi
install -m 0755 "$cli_binary" "$prefix/lib/usagebar/CodexBarCLI"
install -m 0755 "$root/uninstall.sh" "$prefix/uninstall.sh"
ln -sfn "$prefix/bin/usagebar" "$bin_dir/usagebar"
icon="${root}/assets/usagebar-256.png"
desktop="${root}/packaging/io.github.usagebar.UsageBar.desktop"
[[ -f "$root/usr/share/icons/hicolor/256x256/apps/usagebar.png" ]] && icon="$root/usr/share/icons/hicolor/256x256/apps/usagebar.png"
[[ -f "$root/usr/share/applications/io.github.usagebar.UsageBar.desktop" ]] && desktop="$root/usr/share/applications/io.github.usagebar.UsageBar.desktop"
install -m 0644 "$icon" \
  "$data/icons/hicolor/256x256/apps/usagebar.png"
write_desktop "$desktop" "$data/applications/io.github.usagebar.UsageBar.desktop" "$prefix/bin/usagebar"
printf 'Installed. Open UsageBar from your application menu.\n'
