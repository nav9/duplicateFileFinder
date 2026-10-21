# DedupVault

Cross-platform (Linux, Windows, macOS) desktop tool for finding duplicate
files across several folder backups and consolidating them safely into a
segregation folder. Built with C++20, Qt 6 Widgets, Qt SQL (SQLite), CMake and
SHA-256 content hashing.

The main/reference folder is **never modified**. Every move is journaled,
verified and can be undone from the UI.

## Requirements

- CMake ≥ 3.16, a C++20 compiler (GCC 11+, Clang 14+, MSVC 2019+)
- Qt 6 development packages: Core, Widgets, Sql, Concurrent
  (Ubuntu/Debian: `qt6-base-dev`, `qt6-base-dev-tools`)
- OpenGL development headers (`GL/gl.h`), which Qt 6 Widgets requires to
  configure (Ubuntu/Debian: `libgl-dev libglx-dev libopengl-dev`; Fedora:
  `mesa-libGL-devel`; Arch: `mesa`; openSUSE: `Mesa-libGL-devel`)
- Ninja or Make

A Qt installation on its own is not enough on Linux: Qt's own CMake error in
that case is only "dependency WrapOpenGL could not be found", so both
`install_dependencies.py` and `build.py` configure a throwaway project with
the same Qt components before declaring the environment ready.

Check and install them with the helper script — it prints the exact commands
for your distribution and only runs them if you ask:

```bash
python3 install_dependencies.py          # check + show commands
python3 install_dependencies.py --run    # run them (asks for confirmation)
```

On macOS, after installing Qt with Homebrew, export
`CMAKE_PREFIX_PATH="$(brew --prefix qt@6)"`. On Windows set
`CMAKE_PREFIX_PATH` to the Qt kit's `lib\cmake` directory.

## Build and run

The interactive script remembers Debug/Release and the target OS between
runs (Debug is the default), and prints the executable's absolute path after
each successful build:

```bash
python3 build.py
```

Or build directly:

```bash
cmake -S . -B build_debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build_debug --parallel
./build_debug/bin/DedupVault
```

## How the journey works

1. **Dashboard** – status of the working folder, resumable scan, primary
   actions. Disabled previews reserve space for future tools (duplicate
   images/videos, unique sentences, junk cleaner).
2. **New Scan** – pick a protected main folder, one or more candidate
   folders and a segregation destination. Selections are remembered.
   Folder review defaults to dry-run. The window sizes itself to the available screen,
   the page scrolls, and **Start Scan** sits in a fixed footer so it stays
   reachable on small displays. An option also reports duplicates that exist
   *inside* the main folder; those groups are review-only, because the main
   folder is never modified.
3. **Progress** – live phase/statistics with pause, resume and cancel.
   Closing the app keeps the job resumable; hashes and metadata are
   checkpointed in SQLite.
4. **Duplicate Results** – exact groups (size + SHA-256), keeper chosen
   deterministically (main-folder copy wins), redundant copies pre-selected.
   **Dry Run** validates the selected moves and reports what would happen
   without touching the disk; **Segregate Selected** actually moves them into
   `destination/segregated/<root-name>/<relative path>`.
   Groups that live entirely inside the main folder are shown as
   “review only — main folder is never modified” and carry no moves.
5. **Review & History** – folder/subtree matches that need explicit approval,
   plus the full action journal with one-click undo (files and subtrees).

## Working folder

The working folder holds the authoritative SQLite catalog (`catalog.db`), the
rotating logs (`logs/`) and temporary working memory (`tmp/`). Consolidated
duplicate files go to the destination's segregation area, not the working
folder. Write access is verified at startup; if it is missing you can browse
for or create another folder, and the choice is persisted in Settings.

## Safety model

- Centralized checks in `operations/safefileops.cpp`: no source inside the
  protected root, no destination inside the source, collision-safe naming.
- Journal written *before* the filesystem is touched; verified afterwards.
- Same-filesystem moves use `rename`; cross-filesystem moves copy to a
  temporary name, verify size + SHA-256, then remove the source.
- Startup reconciliation repairs interrupted moves and clears partial copies.
- Directory symlinks are never followed by default (configurable), so they
  cannot bypass protected-root checks.

## Layout

```
src/core/         settings, rotating logger, filesystem utilities, SQLite catalog
src/engine/       traversal, streaming SHA-256, duplicate detection, folder
                  analysis, persistent task queue, scan orchestration
src/operations/   safe moves, segregation planning, undo journal
src/ui/           Qt Widgets pages (dashboard, scan, progress, results,
                  review/history, logs, settings, setup dialog, themes)
build.py                  interactive build menu
install_dependencies.py   dependency checker / installer
gallery/app_icon.svg      application icon
```
