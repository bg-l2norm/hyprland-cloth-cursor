# Hyprland Cloth Cursor

A native C++ plugin for Hyprland that gives the visible cursor inertial drag, rotation, bend, stretch, and click compression.

https://github.com/user-attachments/assets/21f9e71b-1e15-45f2-aabc-06c6c4872f2d

The compiled plugin is `libclothcursor.so`. On end4 systems, the installer adds a small Lua startup entry that loads and enables the plugin after login.

> [!IMPORTANT]
> This release is live-tested against these exact Hyprland builds:
>
> ```text
> 0.55.4  a0136d8c04687bb36eb8a28eb9d1ff92aea99704_aq_0.12_hu_0.13_hg_0.5_hc_0.1_hlg_0.6
> 0.56.1  5c9377c15f85c50648f35ca5a213754f95b93ca0_aq_0.14_hu_0.14_hg_0.5_hc_0.1_hlg_0.6
> ```
>
> Hyprland plugins use private compositor APIs. Other versions are **untested**, but the installer can build against their installed headers after an explicit warning and confirmation. The plugin still refuses a build/runtime ABI mismatch, and it restores or retains the stock cursor if its guarded renderer-hook checks fail. The current renderer path is OpenGL.

## What it does

- Moves the visible cursor through a bounded two-dimensional spring.
- Uses spring lag and velocity to calculate rotation, skew, and stretch.
- Compresses the cursor to `79%` while a mouse button is held, then springs back.
- Transforms the cursor texture currently supplied by Hyprland, including theme and application cursor shapes.
- Keeps application input at Hyprland's real pointer position.
- Updates only the cursor's previous and current transformed regions.
- Restores Hyprland's stock cursor when disabled.
- Uses Hyprland's stock renderer for a frame when custom rendering cannot proceed safely.

During fast motion, the visible cursor trails the pointer. Clicks still use the real pointer position.

## How it works

<img width="1448" height="1086" alt="b81d1ca1-71b3-457d-9041-46d140093651" src="https://github.com/user-attachments/assets/97a37453-1910-4459-9811-d8ca209598cd" />


```text
Hyprland pointer position
          ↓
Bounded spring simulation
          ↓
Visual cursor position and velocity
          ↓
Rotation + bend + stretch
          ↓
Custom Hyprland cursor render pass
```

### 1. Pointer events set the target

Mouse movement updates the spring target. The event handler damages the affected cursor region and schedules a compositor frame.

### 2. A spring moves the visible cursor

The spring stores:

```text
visual position
visual velocity
```

Its acceleration is calculated from displacement and damping:

```text
acceleration =
    stiffness × (target - visual position)
    - damping × visual velocity
```

The simulation uses small `1/240 s` integration steps, a bounded frame interval, maximum lag, and maximum velocity. It snaps to the target after reaching its settling thresholds.

### 3. Motion creates the cloth effect

The plugin derives the visual transform from spring state:

- horizontal lag and velocity produce rotation;
- the same motion signal produces a smaller skew;
- lag distance and speed produce stretch;
- cross-axis compression keeps the shape balanced.

All values have fixed limits.

### 4. A second spring handles clicks

Mouse-button state drives a scalar spring from `0` to `1`. Its value scales both cursor axes down during a press and restores them on release.

### 5. Hyprland draws the transformed cursor

The plugin hooks:

```text
CPointerManager::renderSoftwareCursorsFor(...)
```

It reads Hyprland's active cursor texture, size, scale, and hotspot, then submits a custom render-pass element. The transform is applied around the hotspot in this order:

```text
rotation → skew → scale
```

The plugin forces software cursor rendering while enabled because the transformed texture is drawn inside the compositor render pass.

### 6. Damage follows the transformed shape

The plugin calculates a conservative bounding box from all four transformed cursor corners. It damages the union of the old and new bounds, including a small padding area. This clears the previous cursor image and avoids full-screen redraws.

## Project structure

| Path | Role |
|---|---|
| `src/plugin.cpp` | Plugin entrypoint and runtime ABI verification |
| `src/runtime.cpp` | Hyprland hooks, input listeners, frame scheduling, controls, fallback handling |
| `src/hyprland_compat.hpp` | Compatibility bridge for Hyprland 0.55 and 0.56 pointer/monitor APIs |
| `src/physics.cpp` | Spring simulation, visual transform, hotspot anchoring, transformed bounds |
| `src/cursor_pass.cpp` | Hyprland render-pass element and texture transform |
| `tests/physics_test.cpp` | Physics and geometry tests |
| `tests/nested_multi_output_test.sh` | Isolated bidirectional two-output render/scheduling gate |
| `scripts/clothcursorctl` | Load, enable, disable, status, and unload commands |
| `install.sh` | Local build, tests, installation, startup entry, rollback |
| `cmake/VerifyHyprland.cmake` | Build-time Hyprland ABI check |

## Installation

The installer builds the plugin locally, runs its tests, installs it under `~/.local/`, and creates a managed startup entry.

```bash
git clone https://github.com/bg-l2norm/hyprland-cloth-cursor.git
cd hyprland-cloth-cursor
./install.sh
```

Required tools:

- CMake 3.27 or newer
- C++23 compiler
- Hyprland development headers
- `pkg-config`
- `python3`
- `hyprctl`
- `flock`
- `readelf`
- `ldd`

On Arch Linux, these come from packages such as `base-devel`, `cmake`, `hyprland`, `util-linux`, `binutils`, and `glibc`.

The installer:

1. checks the installed Hyprland ABI and warns before continuing on an untested build;
2. builds `libclothcursor.so`;
3. runs the physics tests;
4. checks the shared library and its dependencies;
5. installs the plugin and controller under `~/.local/`;
6. adds one managed startup block to `custom/execs.lua` or `custom/execs.conf`;
7. loads and enables the plugin in the current session.

Repeated installation updates the same files and startup block. Edited configuration files receive timestamped backups.

## Runtime controls

```bash
clothcursorctl status
clothcursorctl enable
clothcursorctl disable
clothcursorctl toggle
clothcursorctl unload
```

- `disable` restores the stock cursor and keeps the shared library loaded.
- `unload` disables the effect and removes the plugin from the current Hyprland session.
- `status` reports installation, load, and runtime state.

The plugin also registers:

```bash
hyprctl clothcursor status
hyprctl clothcursor enable
hyprctl clothcursor disable
hyprctl clothcursor toggle
```

## Startup on end4

For a Lua-based end4 configuration, the installer adds this managed behavior to `custom/execs.lua`:

```lua
hl.on("hyprland.start", function()
    hl.exec_cmd("~/.local/bin/clothcursorctl session-start")
end)
```

The startup entry launches the controller. The cursor simulation and rendering run inside the compiled C++ plugin.

Legacy Hyprland `.conf` configuration uses an equivalent `exec-once` entry.

## Cursor themes

Hyprland supplies the active cursor texture. Cloth Cursor transforms that texture during rendering.

This keeps the effect compatible with cursor theme and size changes. A cursor-change event damages the old and new regions so the updated shape appears immediately.

## Disable and uninstall

Restore the stock cursor while keeping the plugin loaded:

```bash
clothcursorctl disable
```

Unload it from the current session:

```bash
clothcursorctl unload
```

Remove the installed files and managed startup entry:

```bash
./install.sh uninstall
```

The uninstaller disables and unloads the plugin before deleting its installed library and controller.

## Safety and limitations

- The plugin starts disabled after direct loading. `clothcursorctl enable` activates it.
- Hyprland 0.55.4 and 0.56.1 at the exact commits above are tested baselines. Other builds can be attempted after confirmation, but compatibility is not guaranteed.
- The renderer path requires Hyprland's OpenGL renderer.
- HDR/ICC and other renderer paths are outside this release.
- Software cursor composition uses more power than a hardware cursor plane.
- Fast movement places the visual cursor behind the real input position.
- A mouse disconnect during a held button can leave press compression active. Reset it with:

  ```bash
  clothcursorctl disable
  clothcursorctl enable
  ```

- Controller-driven disable and unload remove queued custom cursor passes and release the software-cursor lock.
- A plugin fault runs inside the compositor process and can end the Hyprland session.

## Development build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j2
ctest --test-dir build --output-on-failure

hyprctl plugin load "$PWD/build/libclothcursor.so"
hyprctl clothcursor enable
```

Build only the physics library and tests:

```bash
cmake -S . -B build-physics \
    -DCLOTH_CURSOR_BUILD_PLUGIN=OFF \
    -DCMAKE_BUILD_TYPE=Debug

cmake --build build-physics
ctest --test-dir build-physics --output-on-failure
```

Enable AddressSanitizer and UndefinedBehaviorSanitizer:

```bash
cmake -S . -B build-sanitize \
    -DCLOTH_CURSOR_SANITIZE=ON \
    -DCMAKE_BUILD_TYPE=Debug

cmake --build build-sanitize
ctest --test-dir build-sanitize --output-on-failure
```

With a parent Hyprland session running, execute the isolated two-output compositor gate:

```bash
./tests/nested_multi_output_test.sh
```

It starts a temporary nested Hyprland, creates an adjacent headless output, verifies A→B and B→A spring settlement and owner-output scheduling, requires zero render rejects/fallbacks, then unloads the plugin and stops the nested compositor.

## License

MIT — see [`LICENSE`](LICENSE).
