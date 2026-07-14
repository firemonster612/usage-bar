# UsageBar

UsageBar is a Qt 6 tray application that shows how much of your AI coding
assistant quota is left. It tracks six providers in one window and reads them
all through `CodexBarCLI`, the command-line tool from the macOS
[CodexBar](https://github.com/steipete/codexbar) project. UsageBar is an
independent Linux port of that idea and is not affiliated with any of the
providers it reports on.

The interface is plain Qt Widgets with no custom theme, so fonts, spacing,
colours, dark mode, and accessibility follow your desktop settings. Nothing
opens a window on its own — threshold notifications are the only unprompted UI,
and they can be turned off.

## Providers

| Provider | CLI id     | Source      |
| -------- | ---------- | ----------- |
| Codex    | `codex`    | OAuth       |
| Claude   | `claude`   | OAuth       |
| Cursor   | `cursor`   | Web session |
| Droid    | `factory`  | API         |
| Gemini   | `gemini`   | API         |
| Copilot  | `copilot`  | API         |

Each provider is queried with its source pinned explicitly, so the CLI always
takes a non-interactive path that reads credentials already on the machine.

## How usage is fetched

A refresh runs the CLI once per provider, in sequence, with a 30-second timeout
each:

```
CodexBarCLI usage --provider codex --source oauth --status --format json
```

Then it makes one cost query with a 40-second timeout:

```
CodexBarCLI cost --provider both --days 90 --format json
```

`NO_COLOR=1` and `TERM=dumb` are set on the child so the output stays
machine-readable. Because the cost query asks for `both`, cost figures only
appear for the providers that command reports — in practice Codex and Claude.
The other four show usage windows but no cost section.

UsageBar looks for the CLI in this order, taking the first hit that is an
executable file:

1. `$USAGEBAR_CODEXBAR_CLI`
2. `CodexBarCLI` next to the `usagebar` binary
3. `../lib/usagebar/CodexBarCLI` relative to that binary
4. `CodexBarCLI` on `$PATH`
5. `codexbar` on `$PATH`

If nothing is found the window says so. The path actually in use is shown in
Settings ▸ About, which is the first thing to check when the numbers look wrong.

## Credentials

UsageBar never asks for or stores credentials. CodexBarCLI handles authentication
using credentials already configured on the machine: OAuth sessions for Codex
and Claude, Cursor's web session, and provider API credentials for Droid,
Gemini, and Copilot. A provider you are not signed in to reports an error and is
shown as unavailable; the others keep working. Refreshes that fail keep the last
good numbers on screen rather than blanking the page.

## Interface

Where a system tray is available the window is a frameless popover anchored to
the tray icon. Without one it is an ordinary resizable window, which is how it
behaves on stock GNOME.

A six-segment switcher across the top selects the provider, and the choice is
remembered between openings. Each provider page shows the plan and last update
time, every usage window with its reset and pace text, Codex reset credits,
today and last-30-day cost with expandable comparisons and a daily history
chart, and rows that open the provider's account, usage dashboard, and status
page in your browser.

| Shortcut           | Action                          |
| ------------------ | ------------------------------- |
| `Ctrl+1` … `Ctrl+6`| Select a provider               |
| `←` / `→`          | Move through the switcher       |
| `Ctrl+R`           | Refresh now                     |
| `Ctrl+,`           | Open Settings                   |
| `Esc`              | Hide the popover                |
| `Ctrl+Q`           | Quit                            |

## Settings

Settings is a single non-modal window reachable from the main window and the
tray menu. Changes apply immediately and are stored with `QSettings`; there is
nothing to save.

- **General** — start at login, refresh interval (Manual, 1, 2, 5, or 15
  minutes), and whether opening the window refreshes.
- **Display** — show remaining instead of used, show the cost section, show the
  7 and 90-day cost comparisons, and show reset times once a quota is exhausted.
- **Notifications** — enable threshold notifications and set the three
  thresholds, which are kept strictly ascending.
- **About** — version, the CodexBarCLI path in use, and attribution.

## Behaviour

- Refreshes every five minutes by default. `Manual` stops the timer entirely.
- Notifications default to 75%, 90%, and 100% used. They fire only on an upward
  crossing, once per threshold per reset cycle, and never for the first snapshot
  after launch. Crossings are tracked even while notifications are off, so
  re-enabling them does not replay everything you missed.
- Notifications go through the freedesktop D-Bus service and fall back to the
  tray's own message if that is unavailable, so they do not depend on the tray.
- The tray tooltip lists each provider's first usage window.
- Starts in the background. The desktop launcher passes `--show`; the autostart
  entry it writes to `~/.config/autostart` passes `--background`. Launching a
  second copy with `--show` raises the running one instead.

Stock GNOME does not show legacy tray icons. Use the application-menu launcher,
or install an AppIndicator extension. On Wayland a client cannot place its own
window, so the compositor centres the popover instead of anchoring it to the
tray icon; X11 and XWayland anchor normally.

## Build and test

Requires CMake 3.21+, Ninja, a C++20 compiler, Qt 6.2+ (Core, DBus, Network,
Svg, Widgets, and Test), and `curl`. The Qt packages are `qt6-base-dev
qt6-svg-dev` on Debian and Ubuntu, `qt6-qtbase-devel qt6-qtsvg-devel` on Fedora,
and `qt6-base qt6-svg` on Arch.

```sh
./scripts/fetch-codexbar-cli.sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

`fetch-codexbar-cli.sh` downloads the pinned statically linked CodexBarCLI
0.42.1 musl build for x86_64 or aarch64 and checks it against a per-architecture
SHA-256 digest. The tests cover JSON parsing, notification thresholds, and
backend failure handling; they do not need the CLI or a display.

## Install

```sh
./install.sh
```

This installs into `~/.local` without sudo, building first if there is no build
yet, and works the same from a checkout or from an extracted release tarball.
Open **UsageBar** from your application menu. Run
`~/.local/share/usagebar/uninstall.sh` to remove it.

## Packaging

`scripts/package-appimage.sh` builds an AppDir and an AppImage; it needs
`LINUXDEPLOY`, `APPIMAGETOOL`, and `APPIMAGE_RUNTIME_FILE` pointing at pinned
tools. `scripts/package-tar.sh` turns that AppDir into a self-contained tarball
with the install scripts alongside it. On a `v*` tag, CI runs both for x86_64
and aarch64 and publishes `UsageBar-linux-<arch>.AppImage` and
`UsageBar-linux-<arch>.tar.gz` to a GitHub release. No release has been tagged
yet, so building from source is currently the only way to run it.

## Licence

UsageBar is MIT licensed. Usage fetching relies on CodexBarCLI, and the provider
artwork is derived from CodexBar — both remain under Peter Steinberger's MIT
licence. See [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).
