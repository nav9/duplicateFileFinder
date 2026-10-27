#!/usr/bin/env python3
import os
import sys
import platform
import subprocess
from pathlib import Path

class DuplicateManagerMenu:
    def __init__(self):
        self.repo_root = Path(__file__).parent.resolve()
        self.config_file = self.repo_root / "build" / ".config"
        self.build_dir_base = self.repo_root / "build"
        self.config = self.load_config()
        self.os_name = platform.system()
        
    def load_config(self):
        if self.config_file.exists():
            try:
                with open(self.config_file) as f:
                    return f.read().strip()
            except:
                pass
        return "Debug"
    
    def save_config(self, config):
        self.config_file.parent.mkdir(parents=True, exist_ok=True)
        try:
            with open(self.config_file, 'w') as f:
                f.write(config)
            self.config = config
        except Exception as e:
            print(f"Warning: Could not save config: {e}")
    
    def get_build_dir(self):
        return self.build_dir_base / self.config.lower()
    
    def print_menu(self):
        print("\n" + "="*50)
        print(f"Duplicate Manager - Build Menu")
        print(f"OS: {self.os_name} | Configuration: {self.config}")
        print("="*50)
        print("1. Build")
        print("2. Run")
        print("3. Test")
        print("4. Switch between Debug and Release")
        print("5. Install dependencies")
        print("6. Create standalone executable/package")
        print("7. Exit")
        print("="*50)
    
    def run_command(self, cmd, cwd=None):
        if cwd is None:
            cwd = self.repo_root
        try:
            result = subprocess.run(cmd, cwd=cwd, check=False)
            return result.returncode
        except KeyboardInterrupt:
            print("\nInterrupted")
            return -1
        except Exception as e:
            print(f"Error: {e}")
            return -1
    
    def build_project(self):
        print(f"\nBuilding in {self.config} configuration...")
        build_dir = self.get_build_dir()
        build_dir.mkdir(parents=True, exist_ok=True)
        
        # Check for cmake
        if self.run_command(["cmake", "--version"]) != 0:
            print("Error: cmake not found")
            return
        
        # Configure
        if not (build_dir / "CMakeCache.txt").exists():
            print("Configuring...")
            cmd = ["cmake", "-S", str(self.repo_root), "-B", str(build_dir)]
            if self.os_name != "Windows":
                cmd.append(f"-DCMAKE_BUILD_TYPE={self.config}")
            ret = self.run_command(cmd)
            if ret != 0:
                print("Configuration failed")
                return
        
        # Build
        print("Building...")
        cmd = ["cmake", "--build", str(build_dir)]
        if self.os_name == "Windows":
            cmd.extend(["--config", self.config])
        cmd.append("--parallel")
        ret = self.run_command(cmd)
        if ret == 0:
            print("Build successful")
        else:
            print("Build failed")
    
    def run_project(self):
        print(f"\nRunning in {self.config} configuration...")
        build_dir = self.get_build_dir()
        
        # Try GUI first, then CLI
        gui_exe_name = "dupli_gui"
        cli_exe_name = "dupli_cli"
        
        if self.os_name == "Windows":
            gui_paths = [
                build_dir / "src" / "gui" / self.config / f"{gui_exe_name}.exe",
                build_dir / "src" / "gui" / f"{gui_exe_name}.exe",
            ]
            cli_paths = [
                build_dir / "src" / "cli" / self.config / f"{cli_exe_name}.exe",
                build_dir / "src" / "cli" / f"{cli_exe_name}.exe",
            ]
        else:
            gui_paths = [
                build_dir / "src" / "gui" / gui_exe_name,
            ]
            cli_paths = [
                build_dir / "src" / "cli" / cli_exe_name,
            ]
        
        exe_path = None
        exe_type = None
        
        # Prefer GUI
        for p in gui_paths:
            if p.exists():
                exe_path = p
                exe_type = "GUI"
                break
        
        if exe_path is None:
            for p in cli_paths:
                if p.exists():
                    exe_path = p
                    exe_type = "CLI"
                    break
        
        if exe_path is None:
            print("Executable not found. Building first...")
            self.build_project()
            # Try again
            for p in gui_paths:
                if p.exists():
                    exe_path = p
                    exe_type = "GUI"
                    break
            if exe_path is None:
                for p in cli_paths:
                    if p.exists():
                        exe_path = p
                        exe_type = "CLI"
                        break
        
        if exe_path is None:
            print("Error: Could not find executable")
            return
        
        print(f"Running {exe_type}: {exe_path}")
        ret = self.run_command([str(exe_path)])
        print(f"Exit code: {ret}")
    
    def run_tests(self):
        print(f"\nTesting in {self.config} configuration...")
        build_dir = self.get_build_dir()
        
        if not build_dir.exists():
            print("Build directory not found. Building first...")
            self.build_project()
        
        cmd = ["ctest", "--test-dir", str(build_dir), "--output-on-failure"]
        if self.os_name == "Windows":
            cmd.extend(["-C", self.config])
        ret = self.run_command(cmd)
        print(f"Test exit code: {ret}")
    
    def switch_configuration(self):
        if self.config == "Debug":
            self.save_config("Release")
        else:
            self.save_config("Debug")
        print(f"Switched to {self.config}")
    
    def install_dependencies(self):
        print(f"\nChecking dependencies on {self.os_name}...")
        tools = ["cmake", "g++", "clang++", "make"]
        for tool in tools:
            ret = self.run_command([tool, "--version"])
            if ret == 0:
                print(f"  {tool}: OK")
            else:
                print(f"  {tool}: NOT FOUND")
        
        print("\nChecking wxWidgets...")
        if self.os_name == "Linux":
            ret = self.run_command(["pkg-config", "--exists", "wxgtk3"])
            if ret == 0:
                print("  wxWidgets: OK")
            else:
                print("  wxWidgets: NOT FOUND")
        elif self.os_name == "Darwin":
            ret = self.run_command(["brew", "list", "wxwidgets"])
            if ret == 0:
                print("  wxWidgets: OK")
            else:
                print("  wxWidgets: NOT FOUND")
        elif self.os_name == "Windows":
            print("  wxWidgets: Check manually")
        
        print("\n--- Install Commands ---")
        if self.os_name == "Linux":
            print("Ubuntu/Debian:")
            print("  sudo apt-get install build-essential cmake libwxgtk3.0-gtk3-dev")
            print("\nFedora:")
            print("  sudo dnf install gcc-c++ cmake wxGTK3-devel")
            print("\nArch:")
            print("  sudo pacman -S base-devel cmake wxwidgets-gtk3")
        elif self.os_name == "Darwin":
            print("macOS (Homebrew):")
            print("  brew install cmake wxwidgets")
        elif self.os_name == "Windows":
            print("Windows:")
            print("  Use vcpkg: vcpkg install wxwidgets")
            print("  Or install from https://www.wxwidgets.org/")
        else:
            print("Unknown OS - check wxWidgets installation guide")
        
        print("\nNote: BLAKE3 will be fetched by CMake if needed.")
        print("\nTo install, run the appropriate command above with sudo/admin privileges.")
        resp = input("\nDo you want to attempt automatic installation? (y/N): ").strip().lower()
        if resp == 'y':
            self.auto_install_deps()
    
    def auto_install_deps(self):
        print(f"\nAttempting automatic installation on {self.os_name}...")
        if self.os_name == "Linux":
            # Try apt
            ret = self.run_command(["which", "apt-get"])
            if ret == 0:
                print("Using apt-get...")
                cmd = ["sudo", "apt-get", "install", "-y", "build-essential", "cmake", "libwxgtk3.0-gtk3-dev"]
                print("Running:", " ".join(cmd))
                r = self.run_command(cmd)
                if r == 0:
                    print("Installation completed")
                else:
                    print(f"Installation failed with code {r}")
                return
            ret = self.run_command(["which", "dnf"])
            if ret == 0:
                cmd = ["sudo", "dnf", "install", "-y", "gcc-c++", "cmake", "wxGTK3-devel"]
                print("Running:", " ".join(cmd))
                r = self.run_command(cmd)
                if r == 0:
                    print("Installation completed")
                else:
                    print(f"Installation failed with code {r}")
                return
            ret = self.run_command(["which", "pacman"])
            if ret == 0:
                cmd = ["sudo", "pacman", "-S", "--noconfirm", "base-devel", "cmake", "wxwidgets-gtk3"]
                print("Running:", " ".join(cmd))
                r = self.run_command(cmd)
                if r == 0:
                    print("Installation completed")
                else:
                    print(f"Installation failed with code {r}")
                return
        elif self.os_name == "Darwin":
            ret = self.run_command(["which", "brew"])
            if ret == 0:
                cmd = ["brew", "install", "cmake", "wxwidgets"]
                print("Running:", " ".join(cmd))
                r = self.run_command(cmd)
                if r == 0:
                    print("Installation completed")
                else:
                    print(f"Installation failed with code {r}")
                return
        print("Automatic installation not supported for this setup.")
    
    def create_package(self):
        print(f"\nCreating package in {self.config} configuration...")
        build_dir = self.get_build_dir()
        if not build_dir.exists():
            print("Building first...")
            self.build_project()
        
        # Install to local staging
        install_dir = self.repo_root / "install" / self.config.lower()
        install_dir.mkdir(parents=True, exist_ok=True)
        
        cmd = ["cmake", "--install", str(build_dir), f"--prefix={install_dir}"]
        if self.os_name == "Windows":
            cmd.extend(["--config", self.config])
        ret = self.run_command(cmd)
        if ret == 0:
            print(f"Installed to: {install_dir}")
        else:
            print("Install failed")
        
        # Try cpack if available
        cpack_cmd = ["cpack", "--version"]
        if self.run_command(cpack_cmd) == 0:
            print("Attempting CPack...")
            cpack_cmd = ["cpack", "--config", str(build_dir / "CPackConfig.cmake")]
            if self.os_name == "Windows":
                pass  # cpack handles config differently
            self.run_command(cpack_cmd, cwd=build_dir)
        else:
            print("CPack not configured or not available")
    
    def run(self):
        while True:
            self.print_menu()
            try:
                choice = input("\nEnter choice: ").strip()
                if choice == "1":
                    self.build_project()
                elif choice == "2":
                    self.run_project()
                elif choice == "3":
                    self.run_tests()
                elif choice == "4":
                    self.switch_configuration()
                elif choice == "5":
                    self.install_dependencies()
                elif choice == "6":
                    self.create_package()
                elif choice == "7":
                    print("Exiting...")
                    break
                else:
                    print("Invalid choice. Please try again.")
                # No pause - menu redisplays automatically
            except EOFError:
                break
            except KeyboardInterrupt:
                print("\nExiting...")
                break

if __name__ == "__main__":
    menu = DuplicateManagerMenu()
    menu.run()
