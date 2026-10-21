#!/usr/bin/env python3
"""
DedupVault dependency installer.

Detects the build prerequisites for DedupVault (CMake >= 3.16, a C++20
compiler, Qt 6 development files, the OpenGL development headers, and
make/ninja) and prints the exact installation commands for this machine.

By default nothing is installed: the script only reports what is missing and
shows the commands. You stay in control:

    python3 install_dependencies.py            # check + print commands
    python3 install_dependencies.py --run      # run the commands (asks first)
    python3 install_dependencies.py --run -y   # run without confirmation
    python3 install_dependencies.py --json     # machine-readable check (build.py)

Besides checking for CMake, a compiler, Qt 6 and make/ninja, the script
configures a throwaway CMake project that asks for the same Qt 6 modules as
DedupVault. That catches transitive requirements (notably the OpenGL
development headers, which Qt 6 Gui needs and which are packaged separately
from Qt on Linux), so a "Qt 6 detected" result really means "Qt 6 usable".

The script never elevates privileges by itself; on Linux it prints the
`sudo ...` command (or runs it when you pass --run, in which case your
terminal prompts for the password as usual).
"""

from __future__ import annotations

import argparse
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field
from typing import List, Optional, Sequence


# ---------------------------------------------------------------------------
# Terminal helpers
# ---------------------------------------------------------------------------

class C:
    RESET = "\033[0m"
    BOLD = "\033[1m"
    DIM = "\033[2m"
    RED = "\033[91m"
    GREEN = "\033[92m"
    YELLOW = "\033[93m"
    BLUE = "\033[94m"
    CYAN = "\033[96m"

    @staticmethod
    def enabled() -> bool:
        return sys.stdout.isatty() and os.environ.get("NO_COLOR") is None


def style(text: str, color: str) -> str:
    if not C.enabled():
        return text
    return f"{color}{text}{C.RESET}"


# Suppresses the per-item status lines when the caller wants machine output.
QUIET = False


def info(text: str) -> None:
    if QUIET:
        return
    print(text)


def ok(text: str) -> None:
    if QUIET:
        return
    print(style("  OK   ", C.GREEN) + text)


def warn(text: str) -> None:
    if QUIET:
        return
    print(style("  MISS ", C.YELLOW) + text)


def fail(text: str) -> None:
    if QUIET:
        return
    print(style("  FAIL ", C.RED) + text)


# ---------------------------------------------------------------------------
# Detection
# ---------------------------------------------------------------------------

def native_os() -> str:
    system = platform.system()
    if system == "Linux":
        return "Linux"
    if system == "Darwin":
        return "macOS"
    if system == "Windows":
        return "Windows"
    return system


def linux_distro() -> str:
    try:
        with open("/etc/os-release", encoding="utf-8") as handle:
            data = dict(
                line.rstrip("\n").split("=", 1)
                for line in handle
                if "=" in line
            )
        return data.get("ID", "unknown").strip('"').lower()
    except OSError:
        return "unknown"


def cmake_version() -> Optional[tuple]:
    exe = shutil.which("cmake")
    if not exe:
        return None
    try:
        out = subprocess.run([exe, "--version"], capture_output=True,
                             text=True, timeout=15).stdout
    except (OSError, subprocess.SubprocessError):
        return None
    match = re.search(r"cmake version (\d+)\.(\d+)\.(\d+)", out)
    if not match:
        return None
    return tuple(int(g) for g in match.groups())


def compiler_path() -> Optional[str]:
    candidates = ("g++", "c++", "clang++", "cl")
    for name in candidates:
        path = shutil.which(name)
        if path:
            return path
    return None


def qt6_info() -> Optional[str]:
    """Return a short Qt6 version string when a dev installation is found."""
    qmake = shutil.which("qmake6") or shutil.which("qmake")
    if qmake:
        try:
            out = subprocess.run([qmake, "-query", "QT_VERSION"],
                                 capture_output=True, text=True, timeout=15).stdout.strip()
            if out.startswith("6."):
                return out
        except (OSError, subprocess.SubprocessError):
            pass

    if shutil.which("pkg-config"):
        try:
            result = subprocess.run(["pkg-config", "--modversion", "Qt6Core"],
                                    capture_output=True, text=True, timeout=15)
            if result.returncode == 0 and result.stdout.strip():
                return result.stdout.strip()
        except (OSError, subprocess.SubprocessError):
            pass

    search_roots = [
        "/usr/lib/x86_64-linux-gnu/cmake/Qt6",
        "/usr/lib/aarch64-linux-gnu/cmake/Qt6",
        "/usr/lib64/cmake/Qt6",
        "/usr/lib/cmake/Qt6",
        "/usr/local/lib/cmake/Qt6",
        "/opt/homebrew/lib/cmake/Qt6",
        "/opt/homebrew/opt/qt/lib/cmake/Qt6",
        "/usr/local/opt/qt/lib/cmake/Qt6",
    ]
    for root in search_roots:
        if os.path.isdir(root):
            return "installed (CMake package found)"
    return None


def opengl_dev_info() -> Optional[str]:
    """Return a version/description when OpenGL development files are present.

    Qt 6's Gui module links against OpenGL, and Qt6GuiConfig.cmake calls
    find_dependency(WrapOpenGL). A Qt installation is therefore unusable for
    CMake until the platform OpenGL headers (GL/gl.h) exist, which is a
    separate package from Qt on Linux.
    """
    if native_os() != "Linux":
        return "provided by the platform SDK"

    # CMake's FindOpenGL looks for GL/gl.h, so that header decides the result.
    # `pkg-config opengl` is not enough: libopengl-dev ships opengl.pc without
    # providing gl.h (libgl-dev does), which is exactly how a "Qt detected"
    # check can succeed while the configure step fails.
    include_dirs = ["/usr/include", "/usr/local/include"]

    if shutil.which("pkg-config"):
        for module in ("gl", "opengl"):
            try:
                result = subprocess.run(
                    ["pkg-config", "--variable=includedir", module],
                    capture_output=True, text=True, timeout=10)
                if result.returncode == 0 and result.stdout.strip():
                    include_dirs.append(result.stdout.strip())
            except (OSError, subprocess.SubprocessError):
                pass

    for base in include_dirs:
        header = os.path.join(base, "GL", "gl.h")
        if os.path.isfile(header):
            return f"GL/gl.h found ({header})"
    return None


PROBE_CMAKE = """\
cmake_minimum_required(VERSION 3.16)
project(DedupVaultDependencyProbe LANGUAGES CXX)
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Sql Concurrent)
"""


def qt6_probe() -> Optional[tuple]:
    """Configure a throwaway project that needs the same Qt modules as the app.

    This is the only reliable check: file heuristics said "Qt 6 detected" on
    systems where Qt's own CMake package then failed to configure because a
    transitive dependency (OpenGL, XKB, ...) was missing.

    Returns (succeeded, output) or None when the probe cannot be attempted.
    """
    cmake = shutil.which("cmake")
    if not cmake or not compiler_path():
        return None

    try:
        with tempfile.TemporaryDirectory(prefix="dedupvault-deps-") as tmp:
            src_dir = os.path.join(tmp, "src")
            build_dir = os.path.join(tmp, "build")
            os.makedirs(src_dir, exist_ok=True)
            with open(os.path.join(src_dir, "CMakeLists.txt"), "w",
                      encoding="utf-8") as handle:
                handle.write(PROBE_CMAKE)
            result = subprocess.run([cmake, "-S", src_dir, "-B", build_dir],
                                    capture_output=True, text=True, timeout=180)
            output = (result.stdout or "") + (result.stderr or "")
            return result.returncode == 0, output
    except (OSError, subprocess.SubprocessError):
        return None


def summarize_probe(output: str, limit: int = 6) -> List[str]:
    """Pick the lines of a failed CMake configure that explain the problem."""
    could_not_find = []
    errors = []
    for line in output.splitlines():
        stripped = line.strip()
        if not stripped:
            continue
        if "Could NOT find" in stripped:
            could_not_find.append(stripped)
        elif "CMake Error" in stripped or "Failed to find Qt component" in stripped:
            if len(errors) < 2:
                errors.append(stripped)
    return (could_not_find + errors)[:limit]


def diagnose_probe_failure(output: str) -> Optional[str]:
    """Map a failed Qt CMake configure to the missing prerequisite."""
    if "WrapOpenGL" in output or "OPENGL_INCLUDE_DIR" in output:
        return ("OpenGL development headers (GL/gl.h) — required by Qt 6 Gui; "
                "on Debian/Ubuntu install libgl-dev")
    if "XKB" in output:
        return "XKB development files (libxkbcommon-dev) — required by Qt 6 Gui"
    if "WrapFontconfig" in output or "FONTCONFIG" in output:
        return "Fontconfig development files (libfontconfig1-dev) — required by Qt 6 Gui"
    if "WrapPNG" in output:
        return "libpng development files (libpng-dev) — required by Qt 6 Gui"
    return None


def has_make_tool() -> Optional[str]:
    for name in ("ninja", "make", "mingw32-make", "nmake"):
        path = shutil.which(name)
        if path:
            return name
    return None


@dataclass
class Report:
    missing: List[str] = field(default_factory=list)
    commands: List[str] = field(default_factory=list)
    notes: List[str] = field(default_factory=list)
    details: List[str] = field(default_factory=list)
    can_auto_install: bool = False


def build_report() -> Report:
    report = Report()
    system = native_os()

    version = cmake_version()
    if version is None:
        report.missing.append("CMake (3.16 or newer)")
    elif version < (3, 16):
        report.missing.append(
            f"CMake >= 3.16 (found {'.'.join(map(str, version))})")
    else:
        label = f"CMake {'.'.join(map(str, version))}"
        ok(label)
        report.details.append(f"cmake:    {label} ({shutil.which('cmake')})")

    compiler = compiler_path()
    if compiler:
        ok(f"C++ compiler: {compiler}")
        report.details.append(f"compiler: {compiler}")
    else:
        report.missing.append("C++ compiler (g++, clang++ or MSVC)")

    # Qt "detection" alone is not proof: configure a real project that asks
    # for the same components the application uses.
    qt = qt6_info()
    probe = qt6_probe()
    probe_output = ""
    if probe is not None:
        qt_usable, probe_output = probe
    else:
        # Without cmake/compiler we cannot prove it; fall back to heuristics.
        qt_usable = qt is not None

    # OpenGL is a hard requirement of Qt 6 Gui on Linux and ships separately.
    # Passing the probe already proves CMake found it, so only report it
    # missing when Qt itself is not usable.
    opengl = opengl_dev_info()
    if opengl or qt_usable:
        label = opengl or "found via CMake search paths (GL/gl.h)"
        ok(f"OpenGL development files: {label}")
        report.details.append(f"OpenGL:   {label}")
    else:
        report.missing.append(
            "OpenGL development headers (GL/gl.h) — required by Qt 6 Gui")

    if qt_usable:
        label = qt or "usable (CMake configure probe passed)"
        ok(f"Qt 6: {label}")
        report.details.append(f"Qt 6:     {label}")
    elif qt:
        cause = diagnose_probe_failure(probe_output)
        if cause and not any(m.startswith("OpenGL") for m in report.missing):
            report.missing.append(cause)
        elif not cause:
            report.missing.append(
                "Qt 6 is installed but could not be configured by CMake")
        report.notes.append(
            "Qt 6 was found in " + (qt or "the default locations") +
            ", but configuring a project that asks for Qt6 Widgets/Sql/"
            "Concurrent failed. Qt reported:\n" +
            "\n".join("    " + line for line in summarize_probe(probe_output)))
    else:
        report.missing.append("Qt 6 development files (Core, Widgets, Sql, Concurrent)")

    tool = has_make_tool()
    if tool:
        ok(f"Build tool: {tool}")
        report.details.append(f"make:     {tool}")
    else:
        report.missing.append("make or ninja")

    # --- Per-platform installation commands ---------------------------------
    if system == "Linux":
        distro = linux_distro()
        if distro in ("ubuntu", "debian", "linuxmint", "pop", "zorin", "elementary"):
            report.commands = [
                "sudo apt-get update",
                "sudo apt-get install -y build-essential cmake ninja-build "
                "qt6-base-dev qt6-base-dev-tools "
                "libgl-dev libglx-dev libopengl-dev",
            ]
            report.can_auto_install = True
        elif distro in ("fedora", "rhel", "centos", "rocky", "almalinux"):
            report.commands = [
                "sudo dnf install -y gcc-c++ cmake ninja-build qt6-qtbase-devel "
                "mesa-libGL-devel",
            ]
            report.can_auto_install = True
        elif distro in ("arch", "manjaro", "endeavouros"):
            report.commands = [
                "sudo pacman -S --needed base-devel cmake ninja qt6-base mesa",
            ]
            report.can_auto_install = True
        elif distro in ("opensuse", "opensuse-leap", "opensuse-tumbleweed", "sles"):
            report.commands = [
                "sudo zypper install -y gcc-c++ cmake ninja qt6-base-devel "
                "Mesa-libGL-devel",
            ]
            report.can_auto_install = True
        else:
            report.commands = [
                "# Install these with your distribution's package manager:",
                "#   - build-essential / gcc-c++ (a C++20 compiler)",
                "#   - cmake (>= 3.16) and ninja or make",
                "#   - Qt 6 development packages (Core, Widgets, Sql, Concurrent)",
                "#   - OpenGL development headers (GL/gl.h), needed by Qt 6 Gui",
            ]
    elif system == "macOS":
        report.commands = [
            "brew install cmake ninja qt@6",
            'export CMAKE_PREFIX_PATH="$(brew --prefix qt@6)"',
        ]
        report.can_auto_install = shutil.which("brew") is not None
        if not shutil.which("brew"):
            report.notes.append(
                "Homebrew is not installed. Install it from https://brew.sh first.")
    elif system == "Windows":
        report.commands = [
            "winget install Kitware.CMake",
            "winget install Microsoft.VisualStudio.2022.BuildTools",
            "winget install QtProject.Qt",
        ]
        report.can_auto_install = shutil.which("winget") is not None
        report.notes.append(
            "In the Qt installer enable the MSVC 2019/2022 64-bit kit, then set "
            "CMAKE_PREFIX_PATH to its lib\\cmake directory (e.g. "
            'set CMAKE_PREFIX_PATH=C:\\Qt\\6.5.3\\msvc2019_64\\lib\\cmake).')
    else:
        report.commands = [
            "# Unsupported platform; install CMake, a C++20 compiler and Qt 6.",
        ]

    return report


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def print_header() -> None:
    print()
    print(style("=" * 66, C.CYAN))
    print(style("  DedupVault — dependency check", C.BOLD + C.CYAN))
    print(style("=" * 66, C.CYAN))
    print()


def print_report(report: Report) -> None:
    print()
    if report.missing:
        print(style("Missing prerequisites:", C.BOLD + C.RED))
        for item in report.missing:
            print(style(f"  - {item}", C.RED))

        print()
        print(style("Install them with:", C.BOLD))
        print(style("  python3 install_dependencies.py --run", C.CYAN))

        if report.commands:
            print()
            print(style("Or run these commands yourself:", C.BOLD))
            for command in report.commands:
                print(style(f"  {command}", C.CYAN))
    else:
        print(style("All prerequisites are present — ready to build.", C.GREEN))

    for note in report.notes:
        print()
        print(style(f"  Note: {note}", C.DIM))

    print()
    if report.missing:
        print(style("After installing, re-run this script to verify:", C.DIM))
        print(style("  python3 install_dependencies.py", C.DIM))
        print()
        print(style("Then build and run the app:", C.DIM))
        print(style("  python3 build.py", C.DIM))
        print()


def run_commands(report: Report, assume_yes: bool) -> int:
    if not report.missing:
        print(style("Nothing to install.", C.GREEN))
        return 0
    if not report.commands or not report.can_auto_install:
        print(style(
            "Automatic installation is not available for this platform; "
            "please run the commands above yourself.", C.YELLOW))
        return 2

    actionable = [c for c in report.commands if not c.lstrip().startswith("#")]
    if not actionable:
        print(style("No runnable commands for this platform.", C.YELLOW))
        return 2

    print(style("The following commands will be executed:", C.BOLD))
    for command in actionable:
        print(style(f"  {command}", C.CYAN))
    print()
    if not assume_yes:
        answer = input("Proceed? [y/N]: ").strip().lower()
        if answer not in ("y", "yes"):
            print(style("Cancelled — nothing was changed.", C.YELLOW))
            return 1

    for command in actionable:
        print()
        print(style(f"$ {command}", C.BLUE))
        try:
            # shell=True keeps `sudo` working exactly as the user typed it.
            result = subprocess.run(command, shell=True)
        except OSError as exc:
            fail(f"Could not run command: {exc}")
            return 3
        if result.returncode != 0:
            fail(f"Command exited with code {result.returncode}.")
            return result.returncode

    print()
    print(style("Installation commands finished. Verifying…", C.GREEN))
    return 0


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(
        description="Check and install DedupVault build dependencies.")
    parser.add_argument("--run", action="store_true",
                        help="run the installation commands (default: only print them)")
    parser.add_argument("-y", "--yes", action="store_true",
                        help="do not ask for confirmation before --run")
    parser.add_argument("--json", action="store_true",
                        help="print the result as JSON (used by build.py)")

    global QUIET
    args = parser.parse_args(argv)

    if args.json:
        QUIET = True
        report = build_report()
        print(json.dumps({
            "os": native_os(),
            "missing": report.missing,
            "commands": report.commands,
            "notes": report.notes,
            "details": report.details,
            "can_auto_install": report.can_auto_install,
        }))
        return 0 if not report.missing else 1

    print_header()

    # Fresh detection each run.
    report = build_report()

    if args.run:
        result = run_commands(report, assume_yes=args.yes)
        if result != 0:
            print()
            print_report(report)
            return result
        # Re-check after installing.
        print()
        print_header()
        report = build_report()
        print_report(report)
        if report.missing:
            fail("Some prerequisites are still missing (see above).")
            return 1
        print(style("All prerequisites are now satisfied.", C.GREEN + C.BOLD))
        return 0

    print_report(report)
    if report.missing:
        print(style(
            "Run with --run to execute these commands from here, e.g.:",
            C.DIM))
        print(style("  python3 install_dependencies.py --run", C.DIM))
        print(style("This script never elevates privileges on its own.",
                    C.DIM))
        print()
    return 0 if not report.missing else 1


if __name__ == "__main__":
    sys.exit(main())
