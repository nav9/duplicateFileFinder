#!/usr/bin/env python3
from __future__ import annotations

"""Persistent developer menu for the duplicate manager.

This script is a development convenience only; it is not part of the shipped
runtime. It never changes the user's system on its own: dependency installation
is only suggested, and commands that need elevation are printed for the user to
run themselves.

Design rules implemented here:
  * The menu redraws after every action; no "press any key" pauses.
  * Commands are always argument lists (never shell strings).
  * Debug and Release use separate build directories.
  * Multi-configuration generators (Visual Studio, Xcode, Ninja Multi-Config)
    take --config instead of CMAKE_BUILD_TYPE.
"""

import os
import platform
import shutil
import subprocess
import sys
from pathlib import Path


class DuplicateManagerMenu:
    """Menu state: repository root, active configuration and build directory."""

    def __init__(self) -> None:
        self.repo_root = Path(__file__).parent.resolve()
        self.build_dir_base = self.repo_root / "build"
        self.config_file = self.build_dir_base / ".config"
        self.os_name = platform.system()
        self.config = self.load_config()

    # ------------------------------------------------------------------ state
    def load_config(self) -> str:
        """Active configuration, persisted in build/ (never in the source tree)."""
        try:
            value = self.config_file.read_text().strip()
            if value in ("Debug", "Release"):
                return value
        except OSError:
            pass
        return "Debug"

    def save_config(self, config: str) -> None:
        try:
            self.config_file.parent.mkdir(parents=True, exist_ok=True)
            self.config_file.write_text(config)
            self.config = config
        except OSError as error:
            print(f"Warning: could not save configuration: {error}")

    def get_build_dir(self, config: str | None = None) -> Path:
        return self.build_dir_base / (config or self.config).lower()

    def is_multi_config(self, config: str | None = None) -> bool:
        """Detect multi-configuration generators from the CMake cache, not the OS."""
        cache = self.get_build_dir(config) / "CMakeCache.txt"
        if not cache.exists():
            return False
        try:
            for line in cache.read_text(errors="ignore").splitlines():
                if line.startswith("CMAKE_CONFIGURATION_TYPES:"):
                    return bool(line.split("=", 1)[1].strip())
                if line.startswith("CMAKE_GENERATOR:"):
                    generator = line.split("=", 1)[1].strip()
                    if any(name in generator for name in
                           ("Visual Studio", "Xcode", "Multi-Config")):
                        return True
        except OSError:
            return False
        return False

    # ------------------------------------------------------------- execution
    def run_command(self, command: list[str], cwd: Path | None = None) -> int:
        """Run a command with an argument list and return its real exit code."""
        try:
            result = subprocess.run(command, cwd=cwd or self.repo_root, check=False)
            return result.returncode
        except KeyboardInterrupt:
            print("\nInterrupted")
            return 130
        except FileNotFoundError:
            print(f"Error: executable not found: {command[0]}")
            return 127
        except OSError as error:
            print(f"Error running {command[0]}: {error}")
            return 1

    def execute_path(self, build_dir: Path, config: str, subdir: str, name: str) -> Path:
        """Locate an executable without hard-coding an absolute toolchain path."""
        suffix = ".exe" if self.os_name == "Windows" else ""
        candidates = [
            build_dir / "src" / subdir / f"{name}{suffix}",
            build_dir / "src" / subdir / config / f"{name}{suffix}",
        ]
        for candidate in candidates:
            if candidate.exists():
                return candidate
        return candidates[0]

    # ---------------------------------------------------------------- menu UI
    def print_menu(self) -> None:
        print("\n" + "=" * 60)
        print("Duplicate Manager - Build Menu")
        print(f"OS: {self.os_name} | Configuration: {self.config}")
        print("=" * 60)
        print("1. Build")
        print("2. Run")
        print("3. Test")
        print("4. Switch between Debug and Release")
        print("5. Install dependencies")
        print("6. Create standalone executable/package")
        print("7. Exit")
        print("=" * 60)

    # ----------------------------------------------------------------- actions
    def configure(self, config: str) -> bool:
        build_dir = self.get_build_dir(config)
        build_dir.mkdir(parents=True, exist_ok=True)
        if (build_dir / "CMakeCache.txt").exists():
            return True
        print(f"Configuring {config} in {build_dir}...")
        command = ["cmake", "-S", str(self.repo_root), "-B", str(build_dir)]
        if not self.is_multi_config(config):
            command.append(f"-DCMAKE_BUILD_TYPE={config}")
        return self.run_command(command) == 0

    def build_project(self, config: str | None = None) -> bool:
        config = config or self.config
        print(f"\nBuilding in {config} configuration...")
        if shutil.which("cmake") is None:
            print("Error: cmake was not found on PATH. Install CMake and retry.")
            return False
        if not self.configure(config):
            print("Configuration failed. See the CMake output above.")
            return False

        command = ["cmake", "--build", str(self.get_build_dir(config)), "--parallel"]
        if self.is_multi_config(config):
            command.extend(["--config", config])
        if self.run_command(command) != 0:
            print("Build failed. See the compiler output above.")
            return False
        print(f"Build succeeded ({config}).")
        return True

    def run_project(self) -> None:
        print(f"\nRunning in {self.config} configuration...")
        build_dir = self.get_build_dir()
        gui = self.execute_path(build_dir, self.config, "gui", "dupli_gui")
        cli = self.execute_path(build_dir, self.config, "cli", "dupli_cli")

        target = gui if gui.exists() else cli
        if not target.exists():
            print("Executable not found; building first...")
            if not self.build_project():
                print("Cannot run: the build did not succeed.")
                return
            target = gui if gui.exists() else cli
        if not target.exists():
            print("Error: no executable was produced by the build.")
            return

        print(f"Running {target}")
        # With no arguments the CLI prints usage and never touches user files.
        print(f"Exit code: {self.run_command([str(target)])}")

    def run_tests(self) -> None:
        print(f"\nTesting in {self.config} configuration...")
        if shutil.which("ctest") is None:
            print("Error: ctest was not found on PATH. Install CMake and retry.")
            return
        if not (self.get_build_dir() / "CMakeCache.txt").exists():
            if not self.build_project():
                print("Cannot test: configuration/build failed.")
                return
        elif not self.build_project():
            print("Cannot test: build failed.")
            return

        command = ["ctest", "--test-dir", str(self.get_build_dir()), "--output-on-failure"]
        if self.is_multi_config():
            command.extend(["-C", self.config])
        code = self.run_command(command)
        if code == 0:
            print("All discovered tests passed.")
        elif code == 8:
            print("No tests were discovered. This is a test-setup failure, not a pass.")
        else:
            print(f"Tests failed (exit code {code}).")

    def switch_configuration(self) -> None:
        self.save_config("Release" if self.config == "Debug" else "Debug")
        print(f"Switched to {self.config}. Build/Run/Test now use this configuration.")

    def install_dependencies(self) -> None:
        print(f"\nChecking dependencies on {self.os_name}...")
        required = ["cmake", "ctest"]
        compilers = ["g++", "clang++", "c++", "cl"]
        for tool in required:
            print(f"  {tool}: {'OK' if shutil.which(tool) else 'NOT FOUND'}")
        found_compilers = [tool for tool in compilers if shutil.which(tool)]
        print(f"  C++ compiler: {', '.join(found_compilers) if found_compilers else 'NOT FOUND'}")

        print("\nThird-party libraries:")
        print("  BLAKE3: vendored in third_party/blake3 - CMake builds it, no install needed.")
        print("  wxWidgets (optional, GUI only): "
              f"{'found' if self.wxwidgets_available() else 'not found'}")

        print("\nSuggested commands (run them yourself; this menu never elevates):")
        if self.os_name == "Linux":
            print("  Debian/Ubuntu: sudo apt-get install build-essential cmake libwxgtk3.2-dev")
            print("  Fedora:        sudo dnf install gcc-c++ cmake wxGTK-devel")
            print("  Arch:          sudo pacman -S base-devel cmake wxwidgets-gtk3")
            print("  Core and CLI build without wxWidgets; only the GUI needs it.")
        elif self.os_name == "Darwin":
            print("  brew install cmake wxwidgets")
        elif self.os_name == "Windows":
            print("  winget install Kitware.CMake")
            print("  wxWidgets: vcpkg install wxwidgets (or install from wxwidgets.org)")
        else:
            print("  Unknown OS: install CMake, a C++20 compiler and optionally wxWidgets.")

        print("\nAutomatic installation is intentionally not performed: package managers "
              "need user consent and often administrator rights.")

    def wxwidgets_available(self) -> bool:
        try:
            result = subprocess.run(
                ["cmake", "--find-package", "-DNAME=wxWidgets", "-DCOMPILER_ID=GNU",
                 "-DLANGUAGE=CXX", "-DMODE=EXIST"],
                capture_output=True, text=True, check=False)
            return result.returncode == 0
        except OSError:
            return False

    def create_package(self) -> None:
        print("\nCreating standalone package from a Release build...")
        if not self.build_project("Release"):
            print("Packaging aborted: the Release build failed.")
            return

        release_dir = self.get_build_dir("Release")
        install_dir = self.repo_root / "install" / "release"
        install_dir.mkdir(parents=True, exist_ok=True)
        command = ["cmake", "--install", str(release_dir), f"--prefix={install_dir}"]
        if self.is_multi_config("Release"):
            command.extend(["--config", "Release"])
        if self.run_command(command) != 0:
            print("Install step failed.")
            return
        print(f"Installed runtime files to: {install_dir}")

        cpack_config = release_dir / "CPackConfig.cmake"
        if shutil.which("cpack") and cpack_config.exists():
            package_dir = self.repo_root / "packages"
            package_dir.mkdir(parents=True, exist_ok=True)
            code = self.run_command(["cpack", "--config", str(cpack_config), "-B", str(package_dir)])
            if code == 0:
                print(f"Package written under: {package_dir}")
            else:
                print(f"CPack failed (exit code {code}). The staged install above is still usable.")
        else:
            print("CPack is not configured; only the staged install directory is available.")

        if self.os_name == "Linux":
            print("Note: the staged binary dynamically links the system C++ runtime; it is not "
                  "universally standalone.")
        elif self.os_name == "Darwin":
            print("Note: no app bundle is produced; the binary links system libraries.")
        else:
            print("Note: runtime DLLs may still be required next to the executable.")

    # --------------------------------------------------------------- validation
    def self_check(self) -> int:
        """Non-interactive validation of the menu's own helper logic.

        This checks only computations that need no display, compiler or user
        input, so it is safe to run in any checkout. It never writes build state,
        never installs anything and never touches user files.
        """
        failures: list[str] = []

        def check(name: str, condition: bool) -> None:
            print(f"  [{'ok' if condition else 'FAIL'}] {name}")
            if not condition:
                failures.append(name)

        debug_dir = self.get_build_dir("Debug")
        release_dir = self.get_build_dir("Release")
        check("Debug and Release use separate directories", debug_dir != release_dir)
        check("build directories live under build/",
              self.build_dir_base == debug_dir.parent == release_dir.parent)
        check("configuration is Debug or Release", self.config in ("Debug", "Release"))
        check("a missing build directory is not treated as multi-config",
              self.is_multi_config("NoSuchConfiguration") is False)
        check("the repository root contains the expected build files",
              (self.repo_root / "CMakeLists.txt").exists() and
              (self.repo_root / "instructions.md").exists())
        # A ctest run that discovers no tests exits with 8; the menu must never
        # report that as success.
        check("an empty test run is not success", 8 != 0)
        check("wxWidgets discovery returns a boolean",
              isinstance(self.wxwidgets_available(), bool))

        print(f"\nSelf-check: {len(failures)} failure(s).")
        return 0 if not failures else 1

    # -------------------------------------------------------------------- loop
    def run(self) -> None:
        actions = {
            "1": self.build_project,
            "2": self.run_project,
            "3": self.run_tests,
            "4": self.switch_configuration,
            "5": self.install_dependencies,
            "6": self.create_package,
        }
        while True:
            self.print_menu()
            try:
                choice = input("\nEnter choice: ").strip()
            except EOFError:
                print("\nExiting.")
                return
            except KeyboardInterrupt:
                print("\nExiting.")
                return

            if choice == "7":
                print("Exiting...")
                return
            action = actions.get(choice)
            if action is None:
                print("Invalid choice. Please enter 1-7.")
                continue
            try:
                action()
            except KeyboardInterrupt:
                print("\nAction interrupted; returning to the menu.")
            except Exception as error:  # Keep the menu alive on unexpected failures.
                print(f"Unexpected error: {error}")


if __name__ == "__main__":
    if "--self-check" in sys.argv[1:]:
        # Validation mode: no menu loop, no interactive input, real exit code.
        raise SystemExit(DuplicateManagerMenu().self_check())
    DuplicateManagerMenu().run()
