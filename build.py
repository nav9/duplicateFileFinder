#!/usr/bin/env python3
"""
DedupVault Build Script
Interactive menu for building, running, and managing the DedupVault project.

Features
  - Build / Run / Clean with Debug or Release builds (Debug is the default)
  - Toggle Debug/Release (the menu always shows the active mode)
  - Choose the target OS (Linux by default)
  - Check for required build dependencies and offer to install them
  - Prints the absolute executable path and name after a successful build
"""

import json
import os
import platform
import shutil
import subprocess
import sys

PROJECT_NAME = "DedupVault"
STATE_FILE = ".build_state.json"


# ---------------------------------------------------------------------------
# Terminal helpers
# ---------------------------------------------------------------------------

class Colors:
    HEADER = "\033[95m"
    BLUE = "\033[94m"
    CYAN = "\033[96m"
    GREEN = "\033[92m"
    YELLOW = "\033[93m"
    RED = "\033[91m"
    BOLD = "\033[1m"
    DIM = "\033[2m"
    RESET = "\033[0m"


def supports_color():
    """Check if the terminal supports color output."""
    if os.name == "nt":
        return os.environ.get("ANSICON") is not None or "WT_SESSION" in os.environ
    return hasattr(sys.stdout, "isatty") and sys.stdout.isatty()


USE_COLOR = supports_color()


def colored(text, color):
    if USE_COLOR:
        return f"{color}{text}{Colors.RESET}"
    return text


def print_header():
    print()
    print(colored("=" * 62, Colors.CYAN))
    print(colored("          DedupVault Build System", Colors.BOLD + Colors.CYAN))
    print(colored("=" * 62, Colors.CYAN))
    print()


def print_menu(build_mode, target_os):
    mode_str = colored(build_mode, Colors.GREEN if build_mode == "Debug" else Colors.YELLOW)
    os_str = colored(target_os, Colors.BLUE)

    print(colored("  Build Menu", Colors.BOLD))
    print(colored("  ----------", Colors.DIM))
    print(f"  1. {colored('Build Project', Colors.GREEN)}")
    print(f"  2. {colored('Run Project', Colors.CYAN)}")
    print(f"  3. Toggle Debug/Release (current: {mode_str})")
    print(f"  4. {colored('Clean Build', Colors.YELLOW)}")
    print(f"  5. Set Target OS (current: {os_str})")
    print(f"  6. Check Dependencies (and install if missing)")
    print(f"  7. {colored('Exit', Colors.RED)}")
    print()


# ---------------------------------------------------------------------------
# Project helpers
# ---------------------------------------------------------------------------

def get_project_root():
    """Return the directory containing this script."""
    return os.path.dirname(os.path.abspath(__file__))


def get_build_dir(project_root, build_mode):
    return os.path.join(project_root, f"build_{build_mode.lower()}")


def get_executable_path(build_dir):
    """Return the absolute path and name of the built executable."""
    bin_dir = os.path.join(build_dir, "bin")
    if platform.system() == "Windows":
        exe_name = PROJECT_NAME + ".exe"
    else:
        exe_name = PROJECT_NAME
    return os.path.join(bin_dir, exe_name), exe_name


def get_native_os():
    system = platform.system()
    if system == "Linux":
        return "Linux"
    if system == "Darwin":
        return "macOS"
    if system == "Windows":
        return "Windows"
    return "Linux"


def load_state():
    """Remember the last mode/OS selection between runs (best effort)."""
    path = os.path.join(get_project_root(), STATE_FILE)
    try:
        with open(path, "r", encoding="utf-8") as handle:
            data = json.load(handle)
        mode = data.get("build_mode", "Debug")
        target_os = data.get("target_os", get_native_os())
        if mode not in ("Debug", "Release"):
            mode = "Debug"
        if target_os not in ("Linux", "Windows", "macOS"):
            target_os = get_native_os()
        return mode, target_os
    except (OSError, ValueError):
        return "Debug", get_native_os()


def save_state(build_mode, target_os):
    path = os.path.join(get_project_root(), STATE_FILE)
    try:
        with open(path, "w", encoding="utf-8") as handle:
            json.dump({"build_mode": build_mode, "target_os": target_os}, handle)
    except OSError:
        pass  # remembering state is a convenience, never fatal


# ---------------------------------------------------------------------------
# Dependency checking
# ---------------------------------------------------------------------------

INSTALLER_SCRIPT = "install_dependencies.py"


def dependency_report():
    """Return the dependency status reported by install_dependencies.py.

    The installer script is the single source of truth: it probes Qt with a
    real CMake configure, so build.py never claims "all dependencies present"
    when Qt cannot actually be configured (for example when the OpenGL
    development headers are missing).
    """
    script = os.path.join(get_project_root(), INSTALLER_SCRIPT)
    if not os.path.isfile(script):
        return None

    try:
        result = subprocess.run(
            [sys.executable, script, "--json"],
            capture_output=True, text=True, timeout=300,
        )
    except (OSError, subprocess.SubprocessError):
        return None
    if result.returncode not in (0, 1):
        return None
    try:
        return json.loads(result.stdout)
    except ValueError:
        return None


def print_dependency_help(report):
    """Show what is missing and the exact commands that install it."""
    missing = report.get("missing") or []
    commands = report.get("commands") or []
    notes = report.get("notes") or []

    if missing:
        print(colored("  Missing dependencies:", Colors.BOLD + Colors.RED))
        for dependency in missing:
            print(colored(f"    - {dependency}", Colors.RED))
        print()
        print(colored("  Install them with:", Colors.BOLD))
        print(colored(f"    python3 {INSTALLER_SCRIPT} --run", Colors.CYAN))

    if commands:
        print()
        print(colored("  Or run these commands yourself:", Colors.BOLD))
        for command in commands:
            print(colored(f"    {command}", Colors.CYAN))

    for note in notes:
        print()
        for line in str(note).splitlines():
            print(colored(f"    {line}", Colors.DIM))


def run_installer_script():
    """Run install_dependencies.py --run so the user performs the install."""
    script = os.path.join(get_project_root(), INSTALLER_SCRIPT)
    print()
    answer = input(colored(
        "  Run install_dependencies.py --run now? (uses your package manager, "
        "may ask for sudo) [y/N]: ", Colors.CYAN)).strip().lower()
    if answer not in ("y", "yes"):
        print(colored("  Installation skipped. Run the commands above "
                      "yourself, then build again.", Colors.YELLOW))
        return False

    try:
        result = subprocess.run([sys.executable, script, "--run", "-y"])
    except (OSError, subprocess.SubprocessError) as exc:
        print(colored(f"  Could not run the installer: {exc}", Colors.RED))
        return False
    return result.returncode == 0


def check_dependencies(interactive=True):
    """Check dependencies; optionally hand the installation to the user."""
    report = dependency_report()
    if report is None:
        print(colored(
            f"  Could not run {INSTALLER_SCRIPT} to check dependencies.",
            Colors.RED))
        print(colored(f"  Run it yourself: python3 {INSTALLER_SCRIPT}",
                      Colors.YELLOW))
        return False

    if not report.get("missing"):
        print(colored("  ✓ All required dependencies are present.", Colors.GREEN))
        for line in report.get("details", []):
            print(colored(f"    {line}", Colors.DIM))
        return True

    print_dependency_help(report)

    if not interactive:
        return False

    print()
    if run_installer_script():
        missing_after = (dependency_report() or {}).get("missing") or []
        if not missing_after:
            print(colored("\n  ✓ All required dependencies are now present.",
                          Colors.GREEN))
            return True
        print(colored("\n  Still missing after installation:", Colors.RED))
        for dependency in missing_after:
            print(colored(f"    - {dependency}", Colors.RED))
    return False


def diagnose_configure_failure():
    """Explain a failed CMake configure using the dependency report."""
    report = dependency_report()
    if report and report.get("missing"):
        print()
        print(colored("  The dependency check reports:", Colors.YELLOW))
        print_dependency_help(report)
        print()
        print(colored(f"  Install them with: python3 {INSTALLER_SCRIPT} --run",
                      Colors.CYAN))
        print(colored("  then build again.", Colors.DIM))
    else:
        print(colored("  See the CMake output above for the exact error.",
                      Colors.YELLOW))
        print(colored(f"  Re-check the environment with: "
                      f"python3 {INSTALLER_SCRIPT}", Colors.DIM))

# ---------------------------------------------------------------------------
# Actions
# ---------------------------------------------------------------------------

def build_project(project_root, build_mode, target_os):
    """Configure and build the project."""
    print(colored(f"\n  Building in {build_mode} mode for {target_os}...\n",
                  Colors.CYAN))

    if not check_dependencies(interactive=True):
        print(colored("  Please install missing dependencies and try again.",
                      Colors.RED))
        return

    if target_os != get_native_os():
        print(colored(f"  Note: native toolchain detected ({get_native_os()}).",
                      Colors.YELLOW))
        print(colored(f"  Cross-compiling to {target_os} requires a matching "
                      f"toolchain and Qt build; the native toolchain is used "
                      f"for this build.", Colors.YELLOW))
        print()

    build_dir = get_build_dir(project_root, build_mode)
    os.makedirs(build_dir, exist_ok=True)

    cmake_args = [
        "cmake",
        f"-DCMAKE_BUILD_TYPE={build_mode}",
        "-S", project_root,
        "-B", build_dir,
    ]

    print(colored("  [1/2] Configuring with CMake...", Colors.BLUE))
    result = subprocess.run(cmake_args, cwd=build_dir)
    if result.returncode != 0:
        print(colored("\n  CMake configuration failed!", Colors.RED))
        diagnose_configure_failure()
        return

    build_args = ["cmake", "--build", build_dir, "--config", build_mode,
                  "--parallel"]
    print(colored("\n  [2/2] Building...", Colors.BLUE))
    result = subprocess.run(build_args, cwd=build_dir)
    if result.returncode != 0:
        print(colored("\n  Build failed!", Colors.RED))
        return

    exe_path, exe_name = get_executable_path(build_dir)
    print()
    print(colored("  Build successful!", Colors.GREEN + Colors.BOLD))
    print(colored("  " + "-" * 40, Colors.DIM))
    print(f"  Mode:            {colored(build_mode, Colors.GREEN)}")
    print(f"  Executable name: {colored(exe_name, Colors.CYAN)}")
    print(f"  Executable path: {colored(os.path.abspath(exe_path), Colors.CYAN)}")
    if not os.path.isfile(exe_path):
        print(colored("  Warning: the build succeeded but the executable was "
                      "not found at the expected path.", Colors.YELLOW))
    print()


def run_project(project_root, build_mode):
    """Run the built executable."""
    build_dir = get_build_dir(project_root, build_mode)
    exe_path, exe_name = get_executable_path(build_dir)

    if not os.path.isfile(exe_path):
        print(colored(f"\n  Executable not found: {exe_path}", Colors.RED))
        print(colored("  Please build the project first.", Colors.YELLOW))
        return

    # Make Qt discoverable in a development shell where needed.
    env = os.environ.copy()
    if get_native_os() == "macOS" and "CMAKE_PREFIX_PATH" not in env:
        brew = shutil.which("brew")
        if brew:
            try:
                prefix = subprocess.run([brew, "--prefix", "qt@6"],
                                        capture_output=True, text=True,
                                        timeout=10).stdout.strip()
                if prefix:
                    env["CMAKE_PREFIX_PATH"] = prefix
            except (OSError, subprocess.SubprocessError):
                pass

    print(colored(f"\n  Running {exe_name}...\n", Colors.CYAN))
    try:
        subprocess.run([exe_path], env=env)
    except KeyboardInterrupt:
        print(colored("\n  Application interrupted.", Colors.YELLOW))
    except OSError as exc:
        print(colored(f"\n  Error running application: {exc}", Colors.RED))
    print()


def toggle_build_mode(current_mode):
    new_mode = "Release" if current_mode == "Debug" else "Debug"
    print(colored(f"\n  Build mode changed to: {new_mode}\n", Colors.GREEN))
    return new_mode


def clean_build(project_root, build_mode):
    build_dir = get_build_dir(project_root, build_mode)
    if os.path.isdir(build_dir):
        print(colored(f"\n  Removing {build_dir}...", Colors.YELLOW))
        shutil.rmtree(build_dir)
        print(colored("  Clean complete.\n", Colors.GREEN))
    else:
        print(colored(f"\n  Build directory does not exist: {build_dir}",
                      Colors.YELLOW))
        print(colored("  Nothing to clean.\n", Colors.DIM))


def set_target_os(current_os):
    print(colored("\n  Select target OS:", Colors.BOLD))
    options = ["Linux", "Windows", "macOS"]
    for index, option in enumerate(options, 1):
        marker = " (current)" if option == current_os else ""
        print(f"    {index}. {option}{colored(marker, Colors.DIM)}")
    print()

    while True:
        try:
            choice = input(colored("  Enter choice [1-3]: ", Colors.CYAN)).strip()
            index = int(choice)
            if 1 <= index <= 3:
                new_os = options[index - 1]
                print(colored(f"\n  Target OS set to: {new_os}\n", Colors.GREEN))
                if new_os != get_native_os():
                    print(colored("  Note: building for a different OS requires "
                                  "a cross-compilation toolchain.", Colors.YELLOW))
                    print()
                return new_os
        except (ValueError, EOFError):
            pass
        print(colored("  Invalid choice. Please enter 1, 2, or 3.", Colors.RED))


def main():
    project_root = get_project_root()
    build_mode, target_os = load_state()

    while True:
        print_header()
        print_menu(build_mode, target_os)

        try:
            choice = input(colored("  Enter choice [1-7]: ", Colors.CYAN)).strip()
        except (EOFError, KeyboardInterrupt):
            print(colored("\n\n  Goodbye!\n", Colors.CYAN))
            break

        if choice == "1":
            build_project(project_root, build_mode, target_os)
        elif choice == "2":
            run_project(project_root, build_mode)
        elif choice == "3":
            build_mode = toggle_build_mode(build_mode)
        elif choice == "4":
            clean_build(project_root, build_mode)
        elif choice == "5":
            target_os = set_target_os(target_os)
        elif choice == "6":
            check_dependencies(interactive=True)
        elif choice == "7":
            save_state(build_mode, target_os)
            print(colored("\n  Goodbye!\n", Colors.CYAN))
            break
        else:
            print(colored("\n  Invalid choice. Please enter 1-7.\n", Colors.RED))

        save_state(build_mode, target_os)
        try:
            input(colored("  Press Enter to continue...", Colors.DIM))
        except (EOFError, KeyboardInterrupt):
            print(colored("\n\n  Goodbye!\n", Colors.CYAN))
            break


if __name__ == "__main__":
    main()