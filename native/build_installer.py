#!/usr/bin/env python3
"""
Takoyo v2.2.1 "Octo" - Build & Installer Script

This script:
1. Builds the native Qt app (cmake)
2. Packs Qt + engine + assets into single Takoyo.exe
3. Creates a Windows installer (PowerShell-based self-extractor)
4. Installs to %LOCALAPPDATA%\Takoyo

Usage:
  python build_installer.py              # Full build + installer
  python build_installer.py --build-only # Only build the exe
  python build_installer.py --install    # Install from existing build
"""

from __future__ import annotations

import argparse
import os
import shutil
import struct
import subprocess
import sys
import time
import zlib
from pathlib import Path

# ── Configuration ────────────────────────────────────────────────────────────
ROOT = Path(__file__).resolve().parent.parent
NATIVE = ROOT / 'native'
DIST = NATIVE / 'dist'
ENGINE = NATIVE / 'engine-dist' / 'TakoyoEngine.exe'
SINGLE = NATIVE / 'single'
STAGE = SINGLE / 'stage'
WORK = SINGLE / 'work'
OUTPUT_EXE = SINGLE / 'Takoyo.exe'
LAUNCHER = NATIVE / 'launcher' / 'launcher.cpp'
ICON = ROOT / 'assets' / 'takoyo.ico'

# Version info
VERSION = '2.2.1'
VERSION_NAME = 'Octo'
VERSION_FULL = f'{VERSION} ({VERSION_NAME})'
BUILD_ID = int(time.time())

# Paths
MINGW = Path(os.environ.get('MINGW_BIN', r'C:\Qt\Tools\mingw1310_64\bin'))
QT_BIN = Path(os.environ.get('QT_BIN', r'C:\Qt\6.8.3\mingw_64\bin'))
CMAKE = shutil.which('cmake') or r'C:\Program Files\CMake\bin\cmake.exe'

# Install target
INSTALL_DIR = Path(os.environ.get('LOCALAPPDATA', Path.home() / 'AppData' / 'Local')) / 'Takoyo'
INSTALL_EXE = INSTALL_DIR / 'Takoyo.exe'

# Pack format
MAGIC = b'TKYOPK02'


# ── Utility ──────────────────────────────────────────────────────────────────
def run(cmd: list[str], cwd: Path | None = None, env: dict | None = None) -> subprocess.CompletedProcess:
    print('+', ' '.join(str(c) for c in cmd))
    merged = os.environ.copy()
    if env:
        merged.update(env)
    return subprocess.run(cmd, check=True, cwd=cwd, env=merged)


def copy_tree(src: Path, dst: Path) -> None:
    for item in src.iterdir():
        target = dst / item.name
        if item.is_dir():
            shutil.copytree(item, target, dirs_exist_ok=True)
        else:
            shutil.copy2(item, target)


# ── Build Steps ──────────────────────────────────────────────────────────────
def build_native() -> None:
    """Build TakoyoNative.exe via cmake."""
    print('=== Building native Qt app ===')
    build_dir = NATIVE / 'build'
    
    # Clean if generator mismatch
    cache = build_dir / 'CMakeCache.txt'
    if cache.exists():
        content = cache.read_text()
        if 'GENERATOR:INTERNAL=Ninja' in content:
            print('  Cleaning build dir (generator mismatch)...')
            shutil.rmtree(build_dir, ignore_errors=True)
    
    build_dir.mkdir(exist_ok=True)
    
    # Configure
    env = {
        'PATH': f'{MINGW};{QT_BIN};{os.environ.get("PATH", "")}',
    }
    run([CMAKE, '-S', str(NATIVE), '-B', str(build_dir), '-G', 'MinGW Makefiles', '-DCMAKE_BUILD_TYPE=Release'], env=env)
    
    # Build
    run([CMAKE, '--build', str(build_dir), '--config', 'Release'], env=env)
    
    # Deploy Qt
    native_exe = build_dir / 'bin' / 'TakoyoNative.exe'
    if not native_exe.exists():
        raise FileNotFoundError(f'Build failed: {native_exe} not found')
    
    # windeployqt
    qt_env = {**env, 'PATH': f'{QT_BIN};{MINGW};{os.environ.get("PATH", "")}'}
    windeployqt = QT_BIN / 'windeployqt.exe'
    run([str(windeployqt), '--release', '--no-translations', str(native_exe)], env=qt_env)
    
    # Copy to dist
    shutil.rmtree(DIST, ignore_errors=True)
    DIST.mkdir(parents=True)
    copy_tree(build_dir / 'bin', DIST)
    print(f'  -> {native_exe} deployed to {DIST}')


def build_engine() -> None:
    """Build the frozen Python engine."""
    print('=== Building Python engine ===')
    engine_dist = NATIVE / 'engine-dist'
    engine_dist.mkdir(exist_ok=True)
    
    spec = NATIVE / 'TakoyoEngine.spec'
    run([sys.executable, '-m', 'PyInstaller', '--clean', '--noconfirm', str(spec)], cwd=NATIVE)
    
    built = NATIVE / 'dist' / 'TakoyoEngine.exe'
    if not built.exists():
        raise FileNotFoundError('PyInstaller did not produce TakoyoEngine.exe')
    shutil.copy2(built, ENGINE)
    print(f'  -> Engine copied to {ENGINE}')


def build_payload() -> Path:
    """Create compressed payload.bin with all runtime files."""
    print('=== Building payload ===')
    
    # Stage files
    shutil.rmtree(STAGE, ignore_errors=True)
    shutil.rmtree(WORK, ignore_errors=True)
    STAGE.mkdir(parents=True)
    WORK.mkdir(parents=True)
    
    copy_tree(DIST, STAGE)
    shutil.copy2(ENGINE, STAGE / 'TakoyoEngine.exe')
    copy_tree(ROOT / 'assets', STAGE / 'assets')
    
    files = sorted(p for p in STAGE.rglob('*') if p.is_file())
    print(f'  Staging {len(files)} files...')
    
    payload = WORK / 'payload.bin'
    with payload.open('wb') as f:
        f.write(MAGIC)
        f.write(struct.pack('<II', BUILD_ID, len(files)))
        for path in files:
            rel = path.relative_to(STAGE).as_posix().encode('utf-8')
            data = path.read_bytes()
            packed = zlib.compress(data, 9)
            f.write(struct.pack('<H', len(rel)))
            f.write(rel)
            f.write(struct.pack('<QQ', len(data), len(packed)))
            f.write(packed)
    
    print(f'  -> {payload} ({payload.stat().st_size / 1024 / 1024:.1f} MB)')
    return payload


def write_resource(payload: Path) -> Path:
    """Generate .rc resource file with payload + version info."""
    resource = WORK / 'payload.rc'
    icon_path = str(ICON).replace('\\', '/')
    payload_path = str(payload).replace('\\', '/')
    ver_parts = VERSION.split('.') + ['0']
    ver_csv = ','.join(ver_parts[:4])
    
    resource.write_text(
        f'''#include <windows.h>

101 RCDATA "{payload_path}"
1 ICON "{icon_path}"

VS_VERSION_INFO VERSIONINFO
 FILEVERSION {ver_csv}
 PRODUCTVERSION {ver_csv}
 FILEFLAGSMASK 0x3fL
 FILEFLAGS 0x0L
 FILEOS 0x40004L
 FILETYPE 0x1L
 FILESUBTYPE 0x0L
BEGIN
    BLOCK "StringFileInfo"
    BEGIN
        BLOCK "040904b0"
        BEGIN
            VALUE "CompanyName", "Takoyo"
            VALUE "FileDescription", "Takoyo AI Agent"
            VALUE "FileVersion", "{VERSION}"
            VALUE "InternalName", "Takoyo"
            VALUE "OriginalFilename", "Takoyo.exe"
            VALUE "ProductName", "Takoyo"
            VALUE "ProductVersion", "{VERSION}"
            VALUE "Comments", "Build {BUILD_ID} - {VERSION_NAME}"
        END
    END
    BLOCK "VarFileInfo"
    BEGIN
        VALUE "Translation", 0x409, 1200
    END
END
''',
        encoding='utf-8'
    )
    return resource


def compile_launcher(resource: Path) -> Path:
    """Compile the C++ launcher with embedded resource."""
    print('=== Compiling launcher ===')
    obj = WORK / 'payload_res.o'
    
    run([
        str(MINGW / 'windres.exe'),
        str(resource), '-O', 'coff', '-o', str(obj)
    ], cwd=WORK)
    
    run([
        str(MINGW / 'g++.exe'),
        '-O2', '-s', '-municode', '-mwindows', '-static',
        str(LAUNCHER), str(obj),
        '-o', str(OUTPUT_EXE),
        '-lz', '-luser32',
    ], cwd=WORK)
    
    print(f'  -> {OUTPUT_EXE} ({OUTPUT_EXE.stat().st_size / 1024 / 1024:.1f} MB)')
    return OUTPUT_EXE


def create_installer() -> Path:
    """Create a PowerShell-based self-extracting installer."""
    print('=== Creating installer ===')
    
    installer_dir = SINGLE / 'installer'
    shutil.rmtree(installer_dir, ignore_errors=True)
    installer_dir.mkdir(parents=True)
    
    # Copy the built exe
    installer_exe = installer_dir / 'Takoyo.exe'
    shutil.copy2(OUTPUT_EXE, installer_exe)
    
    # Create install.ps1 (embedded in the installer wrapper)
    install_script = installer_dir / 'install.ps1'
    install_script.write_text(f'''# Takoyo v{VERSION} "{VERSION_NAME}" Installer
# Self-extracting PowerShell installer

$ErrorActionPreference = 'Stop'
$installDir = "$env:LOCALAPPDATA\\Takoyo"
$exeName = "Takoyo.exe"
$version = "{VERSION}"
$buildId = {BUILD_ID}

Write-Host "╔═══════════════════════════════════════════════╗"
Write-Host "║  Takoyo v$version ({VERSION_NAME}) Installer        ║"
Write-Host "║  Build $buildId                              ║"
Write-Host "╚═══════════════════════════════════════════════╝"
Write-Host ""

# Check for existing installation
if (Test-Path "$installDir\\$exeName") {{
    $existing = Get-Item "$installDir\\$exeName"
    Write-Host "Existing install found: $($existing.LastWriteTime) ($($existing.Length/1MB) MB)"
    $confirm = Read-Host "Overwrite? [Y/n]"
    if ($confirm -ine 'y' -and $confirm -ine 'yes' -and $confirm -ine '') {{
        Write-Host "Installation cancelled."
        exit 0
    }}
}}

# Create install directory
New-Item -ItemType Directory -Force -Path $installDir | Out-Null

# Extract embedded executable
$scriptDir = $PSScriptRoot
$sourceExe = "$scriptDir\\$exeName"
if (-not (Test-Path $sourceExe)) {{
    Write-Error "Embedded executable not found: $sourceExe"
    exit 1
}}

Write-Host "Installing to $installDir..."
Copy-Item -LiteralPath $sourceExe -Destination "$installDir\\$exeName" -Force

# Create shortcuts
$shell = New-Object -ComObject WScript.Shell
$startMenu = [Environment]::GetFolderPath('StartMenu')
$shortcut = $shell.CreateShortcut("$startMenu\\Programs\\Takoyo.lnk")
$shortcut.TargetPath = "$installDir\\$exeName"
$shortcut.WorkingDirectory = $installDir
$shortcut.Description = "Takoyo AI Agent v$version ({VERSION_NAME})"
$shortcut.Save()

$desktop = [Environment]::GetFolderPath('Desktop')
$shortcut2 = $shell.CreateShortcut("$desktop\\Takoyo.lnk")
$shortcut2.TargetPath = "$installDir\\$exeName"
$shortcut2.WorkingDirectory = $installDir
$shortcut2.Save()

# Register uninstaller
$uninstallKey = "HKCU:\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Takoyo"
New-Item -Path $uninstallKey -Force | Out-Null
Set-ItemProperty -Path $uninstallKey -Name "DisplayName" -Value "Takoyo v$version ({VERSION_NAME})"
Set-ItemProperty -Path $uninstallKey -Name "DisplayVersion" -Value $version
Set-ItemProperty -Path $uninstallKey -Name "Publisher" -Value "Takoyo"
Set-ItemProperty -Path $uninstallKey -Name "InstallLocation" -Value $installDir
Set-ItemProperty -Path $uninstallKey -Name "UninstallString" -Value "`"$installDir\\$exeName`" --uninstall"
Set-ItemProperty -Path $uninstallKey -Name "QuietUninstallString" -Value "`"$installDir\\$exeName`" --uninstall --quiet"
Set-ItemProperty -Path $uninstallKey -Name "NoModify" -Value 1
Set-ItemProperty -Path $uninstallKey -Name "NoRepair" -Value 1

Write-Host ""
Write-Host "✅ Installation complete!"
Write-Host "   Start Menu: Programs -> Takoyo"
Write-Host "   Desktop: Takoyo shortcut"
Write-Host ""
Write-Host "Run Takoyo from Start Menu or: $installDir\\$exeName"
Read-Host "Press Enter to exit"
''', encoding='utf-8')
    
    # Create a simple wrapper that extracts and runs the PS1
    # We'll use a 7z SFX if available, otherwise a batch wrapper
    sevenz = shutil.which('7z') or shutil.which('7za') or r'C:\Program Files\7-Zip\7z.exe'
    
    installer_output = SINGLE / f'Takoyo-Setup-{VERSION}-{VERSION_NAME}.exe'
    
    if Path(sevenz).exists():
        # Create 7z SFX installer
        config = installer_dir / 'config.txt'
        config.write_text(f''';!@Install@!UTF-8!
Title="Takoyo v{VERSION} ({VERSION_NAME})"
BeginPrompt="Install Takoyo v{VERSION} ({VERSION_NAME}) to %LOCALAPPDATA%\\Takoyo?"
RunProgram="powershell.exe -ExecutionPolicy Bypass -File install.ps1"
;!@InstallEnd@!''', encoding='utf-8')
        
        sfx = Path(sevenz).parent / '7zS.sfx'
        if not sfx.exists():
            sfx = Path(sevenz).parent / '7zsd.sfx'
        
        # Create 7z archive
        archive = installer_dir / 'payload.7z'
        run([sevenz, 'a', '-t7z', '-mx=9', str(archive), str(installer_exe), str(install_script)], cwd=installer_dir)
        
        # Combine SFX + config + archive
        with installer_output.open('wb') as out:
            out.write(sfx.read_bytes())
            out.write(config.read_bytes())
            out.write(archive.read_bytes())
        
        print(f'  -> 7z SFX installer: {installer_output} ({installer_output.stat().st_size / 1024 / 1024:.1f} MB)')
    else:
        # Fallback: batch wrapper that extracts to temp and runs PS1
        wrapper = installer_dir / 'install.bat'
        wrapper.write_text(f'''@echo off
setlocal
set "TMPDIR=%TEMP%\\TakoyoInstall_{BUILD_ID}"
mkdir "%TMPDIR%" 2>nul
powershell -command "Expand-Archive -Force -LiteralPath '%~dp0Takoyo.exe' -DestinationPath '%TMPDIR%'"
powershell -ExecutionPolicy Bypass -File "%TMPDIR%\\install.ps1"
rmdir /s /q "%TMPDIR%" 2>nul
''', encoding='utf-8')
        
        # Just package as zip for manual extraction
        import zipfile
        zip_path = SINGLE / f'Takoyo-{VERSION}-{VERSION_NAME}-portable.zip'
        with zipfile.ZipFile(zip_path, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
            zf.write(installer_exe, 'Takoyo.exe')
            zf.write(install_script, 'install.ps1')
            zf.write(wrapper, 'install.bat')
        
        installer_output = zip_path
        print(f'  -> Portable zip (no 7z SFX): {installer_output} ({installer_output.stat().st_size / 1024 / 1024:.1f} MB)')
    
    return installer_output


def install_exe(exe_path: Path) -> None:
    """Install an already-built Takoyo.exe."""
    print(f'=== Installing {exe_path} ===')
    
    INSTALL_DIR.mkdir(parents=True, exist_ok=True)
    shutil.copy2(exe_path, INSTALL_EXE)
    print(f'  -> Copied to {INSTALL_EXE}')
    
    # Shortcuts
    try:
        import win32com.client
        shell = win32com.client.Dispatch('WScript.Shell')
        
        start_menu = shell.SpecialFolders('Programs')
        shortcut = shell.CreateShortcut(f'{start_menu}\\Takoyo.lnk')
        shortcut.TargetPath = str(INSTALL_EXE)
        shortcut.WorkingDirectory = str(INSTALL_DIR)
        shortcut.Description = f'Takoyo AI Agent {VERSION_FULL}'
        shortcut.Save()
        
        desktop = shell.SpecialFolders('Desktop')
        shortcut2 = shell.CreateShortcut(f'{desktop}\\Takoyo.lnk')
        shortcut2.TargetPath = str(INSTALL_EXE)
        shortcut2.WorkingDirectory = str(INSTALL_DIR)
        shortcut2.Save()
        
        print('  -> Start Menu & Desktop shortcuts created')
    except ImportError:
        print('  ⚠ pywin32 not available, skipping shortcuts')
    
    # Uninstall registry
    try:
        import winreg
        key = winreg.CreateKey(winreg.HKEY_CURRENT_USER,
            r'Software\Microsoft\Windows\CurrentVersion\Uninstall\Takoyo')
        winreg.SetValueEx(key, 'DisplayName', 0, winreg.REG_SZ, f'Takoyo {VERSION_FULL}')
        winreg.SetValueEx(key, 'DisplayVersion', 0, winreg.REG_SZ, VERSION)
        winreg.SetValueEx(key, 'Publisher', 0, winreg.REG_SZ, 'Takoyo')
        winreg.SetValueEx(key, 'InstallLocation', 0, winreg.REG_SZ, str(INSTALL_DIR))
        winreg.SetValueEx(key, 'UninstallString', 0, winreg.REG_SZ, f'"{INSTALL_EXE}" --uninstall')
        winreg.SetValueEx(key, 'QuietUninstallString', 0, winreg.REG_SZ, f'"{INSTALL_EXE}" --uninstall --quiet')
        winreg.SetValueEx(key, 'NoModify', 0, winreg.REG_DWORD, 1)
        winreg.SetValueEx(key, 'NoRepair', 0, winreg.REG_DWORD, 1)
        winreg.CloseKey(key)
        print('  -> Uninstaller registered')
    except ImportError:
        print('  ⚠ winreg not available, skipping uninstaller registration')
    
    print(f'\n[OK] Takoyo {VERSION_FULL} installed to {INSTALL_DIR}')
    print(f'   Run from Start Menu or: {INSTALL_EXE}')


# ── Main ─────────────────────────────────────────────────────────────────────
def main() -> int:
    parser = argparse.ArgumentParser(description=f'Takoyo v{VERSION} "{VERSION_NAME}" Build & Installer')
    parser.add_argument('--build-only', action='store_true', help='Only build Takoyo.exe, no installer')
    parser.add_argument('--install', action='store_true', help='Install existing build')
    parser.add_argument('--skip-engine', action='store_true', help='Skip engine build (use existing)')
    parser.add_argument('--skip-native', action='store_true', help='Skip native build (use existing)')
    args = parser.parse_args()
    
    if args.install:
        if not OUTPUT_EXE.exists():
            print(f'Error: {OUTPUT_EXE} not found. Run without --install first.', file=sys.stderr)
            return 1
        install_exe(OUTPUT_EXE)
        return 0
    
    # Full build
    if not args.skip_native:
        build_native()
    else:
        print('=== Skipping native build ===')
    
    if not args.skip_engine:
        build_engine()
    else:
        print('=== Skipping engine build ===')
    
    payload = build_payload()
    resource = write_resource(payload)
    compile_launcher(resource)
    
    # Cleanup staging
    shutil.rmtree(STAGE, ignore_errors=True)
    shutil.rmtree(WORK, ignore_errors=True)
    
    if not args.build_only:
        installer = create_installer()
        print(f'\n[Installer ready]: {installer}')
    
    print(f'\n[OK] Build {BUILD_ID} complete: Takoyo {VERSION_FULL}')
    print(f'   Executable: {OUTPUT_EXE}')
    return 0


if __name__ == '__main__':
    sys.exit(main())