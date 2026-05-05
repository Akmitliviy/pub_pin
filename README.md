# pub_pin

Keeps the version pins in your `pubspec.yaml` in sync with what
`flutter pub upgrade` actually resolves — automatically, without touching
packages that aren't already pinned.

---

## What it does

1. Runs `flutter pub get`
2. Runs `flutter pub upgrade`
3. Runs `flutter pub deps --style list` to collect every resolved version
4. Reads your `pubspec.yaml` line-by-line (comments and blank lines are
   preserved verbatim)
5. For every dependency that **already has an explicit version constraint**
   in `pubspec.yaml`, rewrites the constraint to `^<resolved_version>`
6. Saves a backup as `pubspec.yaml.bak`, then writes the patched file

Transitive / implicit dependencies that are **not** listed in your
`pubspec.yaml` are never touched.

---

## Building

### Requirements

| Platform | Toolchain                                                   |
|----------|-------------------------------------------------------------|
| Windows  | Visual Studio 2019+ **or** MinGW-w64 (GCC 9+) + CMake 3.16+ |
| macOS    | Xcode Command Line Tools + CMake 3.16+                      |
| Linux    | GCC 9+ or Clang 10+ + CMake 3.16+                           |

CMake can be installed via:
- **Windows**: https://cmake.org/download/ or `winget install Kitware.CMake`
- **macOS**: `brew install cmake`
- **Linux**: `sudo apt install cmake` / `sudo dnf install cmake`

### Windows

```bat
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
```

The binary ends up at `build\Release\pub_pin.exe`
(MinGW: `build\pub_pin.exe`).

Copy `pub_pin.exe` to your Flutter project root.

### macOS / Linux

```sh
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

The binary ends up at `build/pub_pin`.

Copy it to your Flutter project root and make it executable:

```sh
chmod +x pub_pin
```

---

## Usage

Place the binary in your Flutter project root (the folder that contains
`pubspec.yaml`) and run it:

```sh
# from the project root
./pub_pin          # macOS / Linux
pub_pin.exe        # Windows (terminal)
# or just double-click it on Windows
```

You can also pass a project directory as the first argument:

```sh
./pub_pin /path/to/my_flutter_app
```

---

## Example output

```
══════════════════════════════════════════
  pub_pin  — dependency version syncer
══════════════════════════════════════════

─── Step 1: flutter pub get ─────────────────────
Resolving dependencies…
…

─── Step 2: flutter pub upgrade ─────────────────
Resolving dependencies…
…

─── Step 3: collecting resolved versions ─────────
        Found 84 resolved packages.

─── Step 4: reading pubspec.yaml ─────────────────
        Read 1842 bytes.

─── Step 5: updating pinned versions ─────────────
  updated: camerawesome  ^2.5.0  →  ^2.6.1
  updated: record        ^5.1.1  →  ^5.2.0
  updated: path_provider ^2.1.2  →  ^2.1.4
        3 version(s) updated.

─── Backup written to pubspec.yaml.bak ───────────
─── Done ──────────────────────────────────────────
    pubspec.yaml updated. Run `flutter pub get` if
    you want to verify the lockfile is consistent.
```

---

## Notes

- The tool rewrites version constraints as `^X.Y.Z` (caret syntax).
  If you use range constraints like `>=1.0.0 <2.0.0`, they will be
  normalised to `^X.Y.Z` after the first run.
- `pubspec.yaml.bak` is overwritten on every run — keep your version
  control history if you need older snapshots.
- The binary has **no runtime dependencies** — no Dart, no Python, no
  Node. Just `flutter` must be on your `PATH`.