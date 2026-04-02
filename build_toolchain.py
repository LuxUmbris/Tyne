"""
Tyne Compiler Build and Install Toolchain

This script automatically builds and installs the Tyne compiler for the current platform.
Supports Windows, Linux, and macOS with cross-compilation capabilities.

Additionally creates platform-native installers:
  Windows : tyne-setup.exe  (via NSIS / Inno Setup, or a portable .zip fallback)
  Linux   : tyne-<ver>.deb  (via dpkg-deb) and tyne-<ver>.tar.gz portable archive
  macOS   : tyne-<ver>.pkg  (via pkgbuild) and tyne-<ver>.tar.gz portable archive
"""

import os
import sys
import platform
import subprocess
import shutil
import argparse
import textwrap
import tempfile
from pathlib import Path
import urllib.request
import zipfile

# ── Version info ──────────────────────────────────────────────────────────────
TYNE_VERSION = "1.0.0"
TYNE_DESCRIPTION = "Tyne Programming Language Compiler"
TYNE_AUTHOR = "Tyne Project"
TYNE_URL = "https://github.com/tyne-lang/tyne"


# ══════════════════════════════════════════════════════════════════════════════
# Builder
# ══════════════════════════════════════════════════════════════════════════════

class TyneBuilder:
    def __init__(self, source_dir=None, build_dir=None, install_dir=None,
                 compiler=None, update_path=True):
        self.system = platform.system().lower()
        self.machine = platform.machine().lower()
        self.source_dir = Path(source_dir or Path(__file__).parent)
        self.build_dir = Path(build_dir or self.source_dir / "build")
        self.install_dir = Path(install_dir or self._get_default_install_dir())
        self.requested_compiler = compiler
        self.found_compiler = None
        self.do_update_path = update_path

        self.dependencies = {
            'cmake':    self._check_cmake,
            'compiler': self._check_compiler,
            'toml++':   self._check_tomlpp,
        }

    # ── Paths ─────────────────────────────────────────────────────────────────

    def _get_default_install_dir(self):
        if self.system == "windows":
            local_appdata = os.environ.get('LOCALAPPDATA')
            return Path(local_appdata) / "Programs" / "tyne" if local_appdata else Path.home() / "tyne"
        return Path.home() / ".local" / "bin"

    # ── Dependency checks ─────────────────────────────────────────────────────

    def _check_cmake(self):
        try:
            result = subprocess.run(['cmake', '--version'], capture_output=True, text=True, check=True)
            version = result.stdout.split()[2]
            print(f"  [OK] CMake {version} found")
            return True
        except (subprocess.CalledProcessError, FileNotFoundError):
            print("  [!!] CMake not found")
            return False

    def _is_compiler_available(self, compiler):
        try:
            if compiler == 'cl':
                result = subprocess.run([compiler], capture_output=True, text=True, check=False)
                return bool(result.returncode == 0 or result.stdout or result.stderr)
            result = subprocess.run([compiler, '--version'], capture_output=True, text=True, check=True)
            return result.returncode == 0
        except (FileNotFoundError, subprocess.CalledProcessError):
            return False

    def _check_compiler(self):
        candidates = []
        if self.requested_compiler:
            candidates.append(self.requested_compiler)
        if self.system == "windows":
            candidates.extend(['cl', 'g++', 'clang++'])
        else:
            candidates.extend(['g++', 'clang++', 'c++'])

        seen = set()
        for compiler in candidates:
            if compiler in seen:
                continue
            seen.add(compiler)
            if self._is_compiler_available(compiler):
                self.found_compiler = compiler
                print(f"  [OK] C++ compiler '{compiler}' found")
                return True

        print("  [!!] No C++ compiler found")
        return False

    def _check_tomlpp(self):
        print("  [  ] toml++ will be handled by CMake (FetchContent)")
        return True

    # ── Environment ───────────────────────────────────────────────────────────

    def _download_tomlpp(self):
        print("  Downloading toml++ library...")
        url = "https://github.com/marzer/tomlplusplus/archive/refs/tags/v3.4.0.zip"
        zip_path = self.build_dir / "tomlpp.zip"
        try:
            urllib.request.urlretrieve(url, zip_path)
            with zipfile.ZipFile(zip_path, 'r') as z:
                z.extractall(self.build_dir / "tomlpp_src")
            for item in (self.build_dir / "tomlpp_src").iterdir():
                if item.is_dir() and "tomlplusplus" in item.name:
                    tomlpp_install = self.build_dir / "tomlpp"
                    shutil.copytree(item, tomlpp_install, dirs_exist_ok=True)
                    print(f"  [OK] toml++ installed to {tomlpp_install}")
                    return tomlpp_install
        except Exception as e:
            print(f"  [!!] Failed to download toml++: {e}")
        return None

    def _setup_build_environment(self):
        print("Checking build environment...")
        self.build_dir.mkdir(parents=True, exist_ok=True)

        deps_ok = all(checker() for checker in self.dependencies.values())
        if not deps_ok:
            print("\n[FAIL] Missing dependencies. Please install:")
            if self.system == "windows":
                print("  - Visual Studio Build Tools or MinGW-w64")
                print("  - CMake: https://cmake.org/download/")
            elif self.system == "linux":
                print("  sudo apt install build-essential cmake")
            elif self.system == "darwin":
                print("  brew install cmake llvm")
            return False

        self._download_tomlpp()
        return True

    # ── CMake helpers ─────────────────────────────────────────────────────────

    def _find_program(self, names):
        for name in names:
            try:
                cmd = ['where', name] if self.system == 'windows' else ['which', name]
                result = subprocess.run(cmd, capture_output=True, text=True, check=True)
                first = result.stdout.strip().splitlines()[0]
                if first:
                    return first
            except (subprocess.CalledProcessError, FileNotFoundError):
                pass
            if self.system == 'windows':
                for d in filter(None, [
                    os.environ.get('MINGW_HOME'),
                    os.environ.get('MSYS2_HOME'),
                    r'C:\MinGW\bin', r'C:\msys64\mingw64\bin',
                    r'C:\msys64\usr\bin', r'C:\msys32\mingw32\bin',
                ]):
                    for candidate in [Path(d) / (name + '.exe'), Path(d) / name]:
                        if candidate.exists():
                            return str(candidate)
        return None

    def _configure_cmake(self):
        print("Configuring CMake...")
        for stale in ['CMakeCache.txt']:
            p = self.build_dir / stale
            if p.exists():
                p.unlink()
        for stale_dir in ['CMakeFiles', '_deps']:
            p = self.build_dir / stale_dir
            if p.exists():
                try:
                    shutil.rmtree(p)
                except Exception as e:
                    print(f"  [WW] Could not remove {p}: {e}")

        cmake_args = [
            'cmake', '-S', str(self.source_dir), '-B', str(self.build_dir),
            '-DCMAKE_BUILD_TYPE=Release',
            f'-DCMAKE_INSTALL_PREFIX={self.install_dir}',
        ]

        if self.found_compiler:
            cmake_args.append(f'-DCMAKE_CXX_COMPILER={self.found_compiler}')
            if self.found_compiler == 'g++':
                cmake_args.append('-DCMAKE_C_COMPILER=gcc')
            elif self.found_compiler == 'clang++':
                cmake_args.append('-DCMAKE_C_COMPILER=clang')
            elif self.found_compiler == 'cl':
                cmake_args.append('-DCMAKE_C_COMPILER=cl')

        generator = None
        if self.system == "windows":
            if self.found_compiler in ('g++', 'clang++'):
                ninja = self._find_program(['ninja'])
                mingw_make = self._find_program(['mingw32-make'])
                make = self._find_program(['make'])
                if ninja:
                    generator = 'Ninja'
                elif mingw_make:
                    generator = 'MinGW Makefiles'
                    cmake_args.append(f'-DCMAKE_MAKE_PROGRAM={mingw_make}')
                elif make:
                    generator = 'MSYS Makefiles'
                    cmake_args.append(f'-DCMAKE_MAKE_PROGRAM={make}')
                else:
                    print("  [!!] No build program found for g++/clang++")
                    return False
            else:
                ninja = self._find_program(['ninja'])
                if ninja:
                    generator = 'Ninja'
        else:
            generator = 'Unix Makefiles'

        if generator:
            cmake_args.extend(['-G', generator])

        try:
            subprocess.run(cmake_args, cwd=self.build_dir, check=True)
            print("  [OK] CMake configuration successful")
            return True
        except subprocess.CalledProcessError as e:
            print(f"  [!!] CMake configuration failed: {e}")
            return False

    def _build_project(self):
        print("Building Tyne compiler...")
        cmd = ['cmake', '--build', str(self.build_dir), '--config', 'Release']
        if self.system != "windows":
            cmd.extend(['--', '-j', str(os.cpu_count() or 4)])
        try:
            subprocess.run(cmd, check=True)
            print("  [OK] Build successful")
            return True
        except subprocess.CalledProcessError as e:
            print(f"  [!!] Build failed: {e}")
            return False

    def _install_binary(self):
        print(f"Installing to {self.install_dir}...")
        self.install_dir.mkdir(parents=True, exist_ok=True)
        binary_name = "tyne.exe" if self.system == "windows" else "tyne"
        src = self.build_dir / binary_name
        if not src.exists():
            src = self.build_dir / "Release" / binary_name
        if not src.exists():
            print(f"  [!!] Binary not found at {src}")
            return False
        dest = self.install_dir / binary_name
        shutil.copy2(src, dest)
        if self.system != "windows":
            dest.chmod(0o755)
        print(f"  [OK] Installed {binary_name} to {self.install_dir}")
        return True

    def _update_path(self):
        print("Updating PATH...")
        install_bin = str(self.install_dir)
        current_path = os.environ.get('PATH', '')
        if install_bin not in current_path:
            if self.system == "windows":
                try:
                    subprocess.run(['setx', 'PATH', f'{current_path};{install_bin}'], check=True)
                    print("  [OK] PATH updated permanently (restart shell to take effect)")
                except subprocess.CalledProcessError:
                    print(f"  [WW] Add '{install_bin}' to your PATH manually")
            else:
                for rc in [Path.home() / ".bashrc", Path.home() / ".zshrc", Path.home() / ".profile"]:
                    if rc.exists():
                        with open(rc, 'a') as f:
                            f.write(f'\nexport PATH="$PATH:{install_bin}"\n')
                        print(f"  [OK] Added to {rc}")
                        break
                else:
                    print(f"  [WW] Add '{install_bin}' to your PATH manually")
        os.environ['PATH'] = f"{current_path}{os.pathsep}{install_bin}"
        print("  [OK] PATH updated for current session")

    def _test_installation(self):
        print("Testing installation...")
        try:
            subprocess.run(['tyne', '--help'], capture_output=True, text=True, check=True)
            print("  [OK] Tyne compiler installed and working")
            return True
        except (subprocess.CalledProcessError, FileNotFoundError):
            print("  [WW] 'tyne' not found in PATH yet (restart shell)")
            return False

    # ── Main flow ─────────────────────────────────────────────────────────────

    def build_and_install(self):
        print("Tyne Compiler Build Toolchain")
        print("=" * 50)
        print(f"  Platform : {self.system} ({self.machine})")
        print(f"  Source   : {self.source_dir}")
        print(f"  Build    : {self.build_dir}")
        print(f"  Install  : {self.install_dir}")
        print()

        if not self._setup_build_environment(): return False
        if not self._configure_cmake():         return False
        if not self._build_project():           return False
        if not self._install_binary():          return False
        if self.do_update_path:
            self._update_path()
        self._test_installation()

        print("\n[DONE] Tyne compiler installation complete!")
        print(f"       Binary: {self.install_dir}")
        return True

    def clean(self):
        print("Cleaning build directory...")
        if self.build_dir.exists():
            try:
                shutil.rmtree(self.build_dir)
                print("  [OK] Build directory cleaned")
            except Exception as e:
                print(f"  [WW] Could not clean: {e}")
        else:
            print("  [  ] Build directory already clean")


# ══════════════════════════════════════════════════════════════════════════════
# Installer creation
# ══════════════════════════════════════════════════════════════════════════════

class TyneInstaller:
    """
    Creates a platform-native installer after a successful build.

    Windows → tyne-setup.exe   (NSIS preferred, Inno Setup fallback, zip fallback)
    Linux   → tyne-<ver>.deb   (dpkg-deb) + tyne-<ver>.tar.gz  (portable)
    macOS   → tyne-<ver>.pkg   (pkgbuild) + tyne-<ver>.tar.gz  (portable)
    """

    def __init__(self, builder: TyneBuilder, output_dir: Path = None):
        self.builder = builder
        self.system = builder.system
        self.build_dir = builder.build_dir
        self.install_dir = builder.install_dir
        self.source_dir = builder.source_dir
        self.output_dir = Path(output_dir or builder.source_dir / "dist")
        self.version = TYNE_VERSION

    # ── Helpers ───────────────────────────────────────────────────────────────

    def _binary_path(self):
        name = "tyne.exe" if self.system == "windows" else "tyne"
        for candidate in [
            self.build_dir / name,
            self.build_dir / "Release" / name,
            self.install_dir / name,
        ]:
            if candidate.exists():
                return candidate
        return None

    def _lib_files(self):
        """Return all stdlib .tyne files from lib/."""
        lib_dir = self.source_dir / "lib"
        if lib_dir.exists():
            return list(lib_dir.glob("*.tyne"))
        return []

    def _readme(self):
        for candidate in [self.source_dir / "README.md", self.source_dir / "README.txt"]:
            if candidate.exists():
                return candidate
        return None

    # ── Windows installer ─────────────────────────────────────────────────────

    def _find_nsis(self):
        for path in [
            r"C:\Program Files (x86)\NSIS\makensis.exe",
            r"C:\Program Files\NSIS\makensis.exe",
        ]:
            if Path(path).exists():
                return path
        try:
            result = subprocess.run(['where', 'makensis'], capture_output=True, text=True, check=True)
            p = result.stdout.strip().splitlines()[0]
            if p:
                return p
        except Exception:
            pass
        return None

    def _find_inno(self):
        for path in [
            r"C:\Program Files (x86)\Inno Setup 6\ISCC.exe",
            r"C:\Program Files\Inno Setup 6\ISCC.exe",
            r"C:\Program Files (x86)\Inno Setup 5\ISCC.exe",
        ]:
            if Path(path).exists():
                return path
        return None

    def _create_nsis_script(self, binary: Path, tmp_dir: Path) -> Path:
        lib_install = "\n".join(
            f'  File "{f}"' for f in self._lib_files()
        )
        readme = self._readme()
        readme_line = f'  File "{readme}"' if readme else ""

        nsi = tmp_dir / "tyne_setup.nsi"
        nsi.write_text(textwrap.dedent(f"""\
            !define APP_NAME    "Tyne"
            !define APP_VERSION "{self.version}"
            !define APP_PUBLISHER "{TYNE_AUTHOR}"
            !define APP_URL     "{TYNE_URL}"
            !define INSTALL_DIR "$PROGRAMFILES64\\Tyne"

            Name "${{APP_NAME}} ${{APP_VERSION}}"
            OutFile "{self.output_dir / f'tyne-setup.exe'}"
            InstallDir "${{INSTALL_DIR}}"
            InstallDirRegKey HKLM "Software\\Tyne" "Install_Dir"
            RequestExecutionLevel admin
            SetCompressor /SOLID lzma

            ; ── Pages ─────────────────────────────────────────────────────────
            !include "MUI2.nsh"
            !insertmacro MUI_PAGE_WELCOME
            !insertmacro MUI_PAGE_LICENSE "{readme or binary}"
            !insertmacro MUI_PAGE_DIRECTORY
            !insertmacro MUI_PAGE_INSTFILES
            !insertmacro MUI_PAGE_FINISH

            !insertmacro MUI_UNPAGE_CONFIRM
            !insertmacro MUI_UNPAGE_INSTFILES

            !insertmacro MUI_LANGUAGE "English"

            ; ── Installer section ─────────────────────────────────────────────
            Section "Tyne Compiler" SecMain
              SectionIn RO
              SetOutPath "$INSTDIR\\bin"
              File "{binary}"

              SetOutPath "$INSTDIR\\lib"
            {lib_install}

            {readme_line}

              ; Registry
              WriteRegStr HKLM "Software\\Tyne" "Install_Dir" "$INSTDIR"
              WriteRegStr HKLM "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Tyne" "DisplayName" "Tyne Compiler"
              WriteRegStr HKLM "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Tyne" "UninstallString" '"$INSTDIR\\uninstall.exe"'
              WriteRegStr HKLM "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Tyne" "DisplayVersion" "${{APP_VERSION}}"
              WriteRegStr HKLM "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Tyne" "Publisher" "${{APP_PUBLISHER}}"
              WriteRegStr HKLM "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Tyne" "URLInfoAbout" "${{APP_URL}}"
              WriteRegDWORD HKLM "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Tyne" "NoModify" 1
              WriteRegDWORD HKLM "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Tyne" "NoRepair" 1
              WriteUninstaller "$INSTDIR\\uninstall.exe"

              ; Add to system PATH
              ReadRegStr $0 HKLM "SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment" "Path"
              WriteRegExpandStr HKLM "SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment" "Path" "$0;$INSTDIR\\bin"
              SendMessage ${{HWND_BROADCAST}} ${{WM_WININICHANGE}} 0 "STR:Environment" /TIMEOUT=5000
            SectionEnd

            ; ── Uninstaller ───────────────────────────────────────────────────
            Section "Uninstall"
              Delete "$INSTDIR\\bin\\tyne.exe"
              Delete "$INSTDIR\\lib\\*.tyne"
              Delete "$INSTDIR\\uninstall.exe"
              RMDir "$INSTDIR\\bin"
              RMDir "$INSTDIR\\lib"
              RMDir "$INSTDIR"
              DeleteRegKey HKLM "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Tyne"
              DeleteRegKey HKLM "Software\\Tyne"
            SectionEnd
        """), encoding="utf-8")
        return nsi

    def _create_inno_script(self, binary: Path, tmp_dir: Path) -> Path:
        lib_install = "\n".join(
            f'Source: "{f}"; DestDir: "{{app}}\\lib"; Flags: ignoreversion'
            for f in self._lib_files()
        )
        iss = tmp_dir / "tyne_setup.iss"
        iss.write_text(textwrap.dedent(f"""\
            [Setup]
            AppName=Tyne
            AppVersion={self.version}
            AppPublisher={TYNE_AUTHOR}
            AppPublisherURL={TYNE_URL}
            DefaultDirName={{autopf}}\\Tyne
            DefaultGroupName=Tyne
            OutputDir={self.output_dir}
            OutputBaseFilename=tyne-setup
            Compression=lzma
            SolidCompression=yes
            PrivilegesRequired=admin
            ChangesEnvironment=yes

            [Languages]
            Name: "english"; MessagesFile: "compiler:Default.isl"

            [Files]
            Source: "{binary}"; DestDir: "{{app}}\\bin"; Flags: ignoreversion
            {lib_install}

            [Icons]
            Name: "{{group}}\\Tyne Compiler"; Filename: "{{app}}\\bin\\tyne.exe"

            [Registry]
            Root: HKLM; Subkey: "SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment"; \\
                ValueType: expandsz; ValueName: "Path"; \\
                ValueData: "{{olddata}};{{app}}\\bin"; \\
                Check: NeedsAddPath('{{app}}\\bin')

            [Code]
            function NeedsAddPath(Param: string): boolean;
            var
              OrigPath: string;
            begin
              if not RegQueryStringValue(HKEY_LOCAL_MACHINE,
                'SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment',
                'Path', OrigPath)
              then begin
                Result := True;
                exit;
              end;
              Result := Pos(';' + Param + ';', ';' + OrigPath + ';') = 0;
            end;
        """), encoding="utf-8")
        return iss

    def _create_windows_installer(self) -> bool:
        binary = self._binary_path()
        if not binary:
            print("  [!!] Compiled binary not found – build first")
            return False

        self.output_dir.mkdir(parents=True, exist_ok=True)
        out_exe = self.output_dir / "tyne-setup.exe"

        with tempfile.TemporaryDirectory() as tmp:
            tmp_dir = Path(tmp)

            # ── Try NSIS ──────────────────────────────────────────────────────
            nsis = self._find_nsis()
            if nsis:
                print(f"  [  ] NSIS found at {nsis}")
                nsi_script = self._create_nsis_script(binary, tmp_dir)
                try:
                    subprocess.run([nsis, str(nsi_script)], check=True)
                    print(f"  [OK] Installer created: {out_exe}")
                    return True
                except subprocess.CalledProcessError as e:
                    print(f"  [WW] NSIS failed ({e}), trying Inno Setup...")

            # ── Try Inno Setup ────────────────────────────────────────────────
            inno = self._find_inno()
            if inno:
                print(f"  [  ] Inno Setup found at {inno}")
                iss_script = self._create_inno_script(binary, tmp_dir)
                try:
                    subprocess.run([inno, str(iss_script)], check=True)
                    # Inno names it tyne-setup.exe already
                    candidate = self.output_dir / "tyne-setup.exe"
                    if candidate.exists():
                        print(f"  [OK] Installer created: {candidate}")
                        return True
                except subprocess.CalledProcessError as e:
                    print(f"  [WW] Inno Setup failed ({e}), falling back to zip...")

            # ── Portable zip fallback ─────────────────────────────────────────
            print("  [  ] Neither NSIS nor Inno Setup found – creating portable .zip instead")
            return self._create_windows_zip(binary)

    def _create_windows_zip(self, binary: Path) -> bool:
        zip_path = self.output_dir / f"tyne-{self.version}-windows-x64.zip"
        self.output_dir.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(zip_path, 'w', zipfile.ZIP_DEFLATED) as zf:
            zf.write(binary, f"tyne/bin/{binary.name}")
            for f in self._lib_files():
                zf.write(f, f"tyne/lib/{f.name}")
            readme = self._readme()
            if readme:
                zf.write(readme, f"tyne/{readme.name}")
            # Write a minimal install.bat
            install_bat = textwrap.dedent("""\
                @echo off
                echo Installing Tyne Compiler...
                set DEST=%LOCALAPPDATA%\\Programs\\tyne
                mkdir "%DEST%\\bin" 2>nul
                mkdir "%DEST%\\lib" 2>nul
                copy /Y bin\\tyne.exe "%DEST%\\bin\\"
                copy /Y lib\\*.tyne "%DEST%\\lib\\"
                setx PATH "%PATH%;%DEST%\\bin"
                echo.
                echo Done! Restart your shell and run: tyne
            """)
            zf.writestr("tyne/install.bat", install_bat)
        print(f"  [OK] Portable archive created: {zip_path}")
        print("       (Install NSIS or Inno Setup for a proper .exe installer)")
        return True

    # ── Linux installer ───────────────────────────────────────────────────────

    def _create_linux_installer(self) -> bool:
        binary = self._binary_path()
        if not binary:
            print("  [!!] Compiled binary not found – build first")
            return False

        self.output_dir.mkdir(parents=True, exist_ok=True)
        ok = False

        # ── .deb package ──────────────────────────────────────────────────────
        if shutil.which("dpkg-deb"):
            ok = self._create_deb(binary) or ok
        else:
            print("  [WW] dpkg-deb not found, skipping .deb package")

        # ── Portable tar.gz ───────────────────────────────────────────────────
        ok = self._create_targz_linux(binary) or ok
        return ok

    def _create_deb(self, binary: Path) -> bool:
        arch_map = {"x86_64": "amd64", "aarch64": "arm64", "armv7l": "armhf"}
        deb_arch = arch_map.get(platform.machine().lower(), "amd64")
        pkg_name = f"tyne_{self.version}_{deb_arch}"

        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / pkg_name
            (root / "usr" / "bin").mkdir(parents=True)
            (root / "usr" / "share" / "tyne" / "lib").mkdir(parents=True)
            (root / "DEBIAN").mkdir(parents=True)

            shutil.copy2(binary, root / "usr" / "bin" / "tyne")
            (root / "usr" / "bin" / "tyne").chmod(0o755)

            for f in self._lib_files():
                shutil.copy2(f, root / "usr" / "share" / "tyne" / "lib" / f.name)

            readme = self._readme()
            if readme:
                doc_dir = root / "usr" / "share" / "doc" / "tyne"
                doc_dir.mkdir(parents=True)
                shutil.copy2(readme, doc_dir / readme.name)

            control = root / "DEBIAN" / "control"
            control.write_text(textwrap.dedent(f"""\
                Package: tyne
                Version: {self.version}
                Architecture: {deb_arch}
                Maintainer: {TYNE_AUTHOR}
                Description: {TYNE_DESCRIPTION}
                 Tyne is a compiled, statically-typed programming language.
                Homepage: {TYNE_URL}
                Section: devel
                Priority: optional
            """))

            out_deb = self.output_dir / f"{pkg_name}.deb"
            try:
                subprocess.run(["dpkg-deb", "--build", str(root), str(out_deb)], check=True)
                print(f"  [OK] .deb package created: {out_deb}")
                return True
            except subprocess.CalledProcessError as e:
                print(f"  [!!] dpkg-deb failed: {e}")
                return False

    def _create_targz_linux(self, binary: Path) -> bool:
        import tarfile as _tar
        arch = platform.machine().lower()
        out = self.output_dir / f"tyne-{self.version}-linux-{arch}.tar.gz"

        with _tar.open(out, "w:gz") as tf:
            tf.add(binary, arcname=f"tyne-{self.version}/bin/tyne")
            for f in self._lib_files():
                tf.add(f, arcname=f"tyne-{self.version}/lib/{f.name}")
            readme = self._readme()
            if readme:
                tf.add(readme, arcname=f"tyne-{self.version}/{readme.name}")

            # install.sh
            install_sh = textwrap.dedent("""\
                #!/bin/sh
                set -e
                PREFIX="${1:-$HOME/.local}"
                install -Dm755 bin/tyne "$PREFIX/bin/tyne"
                install -dm755 "$PREFIX/share/tyne/lib"
                cp -r lib/*.tyne "$PREFIX/share/tyne/lib/"
                echo "Tyne installed to $PREFIX/bin/tyne"
                echo "Make sure $PREFIX/bin is in your PATH."
            """)
            import io
            data = install_sh.encode()
            info = _tar.TarInfo(name=f"tyne-{self.version}/install.sh")
            info.size = len(data)
            info.mode = 0o755
            tf.addfile(info, io.BytesIO(data))

        print(f"  [OK] Portable archive created: {out}")
        return True

    # ── macOS installer ───────────────────────────────────────────────────────

    def _create_macos_installer(self) -> bool:
        binary = self._binary_path()
        if not binary:
            print("  [!!] Compiled binary not found – build first")
            return False

        self.output_dir.mkdir(parents=True, exist_ok=True)
        ok = False

        if shutil.which("pkgbuild"):
            ok = self._create_pkg(binary) or ok
        else:
            print("  [WW] pkgbuild not found (requires macOS Xcode tools), skipping .pkg")

        ok = self._create_targz_macos(binary) or ok
        return ok

    def _create_pkg(self, binary: Path) -> bool:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / "payload"
            (root / "usr" / "local" / "bin").mkdir(parents=True)
            (root / "usr" / "local" / "share" / "tyne" / "lib").mkdir(parents=True)

            shutil.copy2(binary, root / "usr" / "local" / "bin" / "tyne")
            (root / "usr" / "local" / "bin" / "tyne").chmod(0o755)
            for f in self._lib_files():
                shutil.copy2(f, root / "usr" / "local" / "share" / "tyne" / "lib" / f.name)

            out_pkg = self.output_dir / f"tyne-{self.version}.pkg"
            try:
                subprocess.run([
                    "pkgbuild",
                    "--root", str(root),
                    "--identifier", "com.tyne-lang.tyne",
                    "--version", self.version,
                    "--install-location", "/",
                    str(out_pkg),
                ], check=True)
                print(f"  [OK] .pkg installer created: {out_pkg}")
                return True
            except subprocess.CalledProcessError as e:
                print(f"  [!!] pkgbuild failed: {e}")
                return False

    def _create_targz_macos(self, binary: Path) -> bool:
        import tarfile as _tar
        arch = "arm64" if platform.machine().lower() == "arm64" else "x86_64"
        out = self.output_dir / f"tyne-{self.version}-macos-{arch}.tar.gz"

        with _tar.open(out, "w:gz") as tf:
            tf.add(binary, arcname=f"tyne-{self.version}/bin/tyne")
            for f in self._lib_files():
                tf.add(f, arcname=f"tyne-{self.version}/lib/{f.name}")
            readme = self._readme()
            if readme:
                tf.add(readme, arcname=f"tyne-{self.version}/{readme.name}")

            import io
            install_sh = textwrap.dedent("""\
                #!/bin/sh
                set -e
                PREFIX="${1:-/usr/local}"
                install -m755 bin/tyne "$PREFIX/bin/tyne"
                mkdir -p "$PREFIX/share/tyne/lib"
                cp lib/*.tyne "$PREFIX/share/tyne/lib/"
                echo "Tyne installed to $PREFIX/bin/tyne"
            """)
            data = install_sh.encode()
            info = _tar.TarInfo(name=f"tyne-{self.version}/install.sh")
            info.size = len(data)
            info.mode = 0o755
            tf.addfile(info, io.BytesIO(data))

        print(f"  [OK] Portable archive created: {out}")
        return True

    # ── Entry point ───────────────────────────────────────────────────────────

    def create(self) -> bool:
        print("\nCreating installer...")
        print("=" * 50)
        dispatch = {
            "windows": self._create_windows_installer,
            "linux":   self._create_linux_installer,
            "darwin":  self._create_macos_installer,
        }
        handler = dispatch.get(self.system)
        if not handler:
            print(f"  [WW] Installer creation not supported on '{self.system}'")
            return False
        result = handler()
        if result:
            print(f"\n[DONE] Installer(s) written to: {self.output_dir}")
        else:
            print("\n[FAIL] Installer creation failed")
        return result


# ══════════════════════════════════════════════════════════════════════════════
# CLI
# ══════════════════════════════════════════════════════════════════════════════

def main():
    # Force UTF-8 to avoid Windows cp1252 / charmap errors
    try:
        if hasattr(sys.stdout, 'reconfigure'):
            sys.stdout.reconfigure(encoding='utf-8', errors='replace')
        if hasattr(sys.stderr, 'reconfigure'):
            sys.stderr.reconfigure(encoding='utf-8', errors='replace')
    except Exception:
        pass

    parser = argparse.ArgumentParser(
        description="Build, install and package the Tyne compiler",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=textwrap.dedent("""\
            examples:
              # Build + install + create installer
              python build_toolchain.py

              # Build + create installer, put packages in ./dist
              python build_toolchain.py --installer --dist ./dist

              # Only create installer from an existing build (skip recompile)
              python build_toolchain.py --installer-only --dist ./dist

              # Clean previous build
              python build_toolchain.py --clean
        """),
    )
    parser.add_argument('--source',    '-s', help='Source directory (default: script directory)')
    parser.add_argument('--build',     '-b', help='Build directory   (default: <source>/build)')
    parser.add_argument('--install',   '-i', help='Install directory  (default: platform default)')
    parser.add_argument('--dist',      '-d', help='Installer output directory (default: <source>/dist)')
    parser.add_argument('--compiler',  '-c', help='Preferred C++ compiler: g++, clang++, cl')
    parser.add_argument('--clean',     action='store_true', help='Clean build directory and exit')
    parser.add_argument('--no-path-update', action='store_true', help='Skip PATH update')
    parser.add_argument('--no-installer',   action='store_true', help='Skip installer creation (build+install only)')
    parser.add_argument('--installer-only', action='store_true', help='Only create installer (skip build+install)')

    args = parser.parse_args()

    builder = TyneBuilder(
        source_dir=args.source,
        build_dir=args.build,
        install_dir=args.install,
        compiler=args.compiler,
        update_path=not args.no_path_update,
    )

    if args.clean:
        builder.clean()
        return

    # Build + install phase
    if not args.installer_only:
        if not builder.build_and_install():
            print("\n[FAIL] Build failed!")
            sys.exit(1)
        print("\n[OK] Build completed successfully!")

    # Installer phase
    if not args.no_installer:
        installer = TyneInstaller(
            builder,
            output_dir=Path(args.dist) if args.dist else None,
        )
        if not installer.create():
            print("\n[FAIL] Installer creation failed!")
            sys.exit(1)

    print("\n[ALL DONE]")


if __name__ == "__main__":
    main()