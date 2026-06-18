# Battery Statistics

A native GTK 4/libadwaita application that displays live UPower battery information and a real 24-hour charge history.

## Architecture

The application is deliberately modular but remains a single desktop process:

- `main.c` — application lifecycle, actions, and keyboard accelerators
- `battery-window.c` — window layout and presentation of snapshots
- `battery-chart.c` — chart drawing, hover card, pinned selection, keyboard navigation, and accessibility text
- `upower-service.c` — UPower system D-Bus connection, device discovery, refresh scheduling, and history loading
- `battery-model.c` — shared data structures and formatting/domain helpers

A separate microservice would add deployment and D-Bus/process failure modes without providing useful isolation for this small local application. The `BatteryService` GObject provides the same clean boundary in-process and emits a `changed` signal whenever a complete immutable snapshot is ready.

## UX behavior

- Hourly bars use fixed real-hour positions; missing hours remain empty.
- Hovering a bar column shows a custom floating card with percentage, sample time, and state.
- Clicking a bar pins its details; clicking empty chart space clears it.
- Keyboard: focus the chart with `Tab`, then use `Left`, `Right`, `Home`, `End`, and `Escape`.
- Adjacent measured hours are connected, but gaps are never bridged.
- Charging hours include a bolt marker; the current hour includes a dot and the `Now` axis label.
- The main page hides technical history coverage unless data is incomplete.
- Missing battery/UPower data uses an `AdwStatusPage` with a retry action.
- Refresh moved to the application menu and `Ctrl+R` because updates are normally automatic.

## Dependencies

- GTK 4.8 or newer
- libadwaita 1.4 or newer
- UPower
- Meson, Ninja, and a C compiler

Fedora:

```sh
sudo dnf install gcc meson ninja-build gtk4-devel libadwaita-devel upower
```

Ubuntu/Debian:

```sh
sudo apt install build-essential meson ninja-build libgtk-4-dev libadwaita-1-dev upower
```

Arch Linux:

```sh
sudo pacman -S base-devel meson ninja gtk4 libadwaita upower
```

## Build and run

For a fresh build:

```sh
meson setup build
meson compile -C build
./build/src/battery-statistics
```

After replacing an older project version, regenerate the build directory so Meson discovers all new source files:

```sh
rm -rf build
meson setup build
meson compile -C build
./build/src/battery-statistics
```

The setup output should report:

```text
Build targets in project: 1
```

## Shortcuts

- `Ctrl+R` — refresh and reconnect to UPower
- `Tab` — focus the chart
- `Left` / `Right` — previous or next measured hour
- `Home` / `End` — oldest or newest measured hour
- `Escape` — clear a pinned chart value
