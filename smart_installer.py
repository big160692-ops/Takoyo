#!/usr/bin/env python3
"""
Takoyo v2.2.1 "Octo" - Smart Installer with Model Download

This installer:
1. Downloads/installs Takoyo.exe to %LOCALAPPDATA%\Takoyo
2. Optionally downloads local AI models (llama.cpp engine + GGUF models)
3. Creates shortcuts and registers uninstaller
4. Can run silently for automation
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import sys
import tempfile
import urllib.request
from pathlib import Path

# ── Configuration ────────────────────────────────────────────────────────────
VERSION = '2.2.1'
VERSION_NAME = 'Octo'
VERSION_FULL = f'{VERSION} ({VERSION_NAME})'
BUILD_ID = 1795577389

# GitHub release assets (update these when publishing)
RELEASE_BASE = 'https://github.com/big160692-ops/Takoyo/releases/download/v2.2.1'
EXE_URL = f'{RELEASE_BASE}/Takoyo.exe'
EXE_SHA256 = ''  # Fill after upload

# Model definitions (matching native/dialogs.cpp kLocalModels)
MODELS = {
    'qwen2.5-0.5b': {
        'name': 'Qwen2.5 0.5B',
        'filename': 'qwen2.5-0.5b-instruct-q4_k_m.gguf',
        'size_mb': 500,
        'url': 'https://huggingface.co/bartowski/Qwen2.5-0.5B-Instruct-GGUF/resolve/main/qwen2.5-0.5b-instruct-q4_k_m.gguf',
    },
    'qwen2.5-1.5b': {
        'name': 'Qwen2.5 1.5B',
        'filename': 'qwen2.5-1.5b-instruct-q4_k_m.gguf',
        'size_mb': 1100,
        'url': 'https://huggingface.co/bartowski/Qwen2.5-1.5B-Instruct-GGUF/resolve/main/qwen2.5-1.5b-instruct-q4_k_m.gguf',
    },
    'qwen2.5-coder-1.5b': {
        'name': 'Qwen2.5 Coder 1.5B',
        'filename': 'qwen2.5-coder-1.5b-instruct-q4_k_m.gguf',
        'size_mb': 1100,
        'url': 'https://huggingface.co/bartowski/Qwen2.5-Coder-1.5B-Instruct-GGUF/resolve/main/qwen2.5-coder-1.5b-instruct-q4_k_m.gguf',
    },
    'qwen2.5-3b': {
        'name': 'Qwen2.5 3B',
        'filename': 'qwen2.5-3b-instruct-q4_k_m.gguf',
        'size_mb': 2100,
        'url': 'https://huggingface.co/bartowski/Qwen2.5-3B-Instruct-GGUF/resolve/main/qwen2.5-3b-instruct-q4_k_m.gguf',
    },
}

DEFAULT_MODEL = 'qwen2.5-coder-1.5b'

# Install paths
INSTALL_DIR = Path(os.environ.get('LOCALAPPDATA', Path.home() / 'AppData' / 'Local')) / 'Takoyo'
INSTALL_EXE = INSTALL_DIR / 'Takoyo.exe'
MODELS_DIR = Path.home() / '.takoyo' / 'models'
CONFIG_FILE = Path.home() / '.takoyo' / 'config.json'

# ── Utilities ────────────────────────────────────────────────────────────────
def download_file(url: str, dest: Path, desc: str = '', progress: bool = True) -> bool:
    """Download with resume support and progress bar."""
    dest.parent.mkdir(parents=True, exist_ok=True)
    part = dest.with_suffix(dest.suffix + '.part')
    
    headers = {}
    downloaded = 0
    if part.exists():
        downloaded = part.stat().st_size
        headers['Range'] = f'bytes={downloaded}-'
    
    req = urllib.request.Request(url, headers=headers)
    try:
        with urllib.request.urlopen(req, timeout=300) as resp:
            total = downloaded + int(resp.headers.get('Content-Length', 0) or 0)
            mode = 'ab' if downloaded else 'wb'
            
            with part.open(mode) as f:
                while True:
                    chunk = resp.read(1024 * 1024)
                    if not chunk:
                        break
                    f.write(chunk)
                    downloaded += len(chunk)
                    if progress and total:
                        pct = downloaded * 100 // total
                        bar = '█' * (pct // 2) + '░' * (50 - pct // 2)
                        print(f'\r{desc} [{bar}] {pct}% ({downloaded // 1024 // 1024} MB / {total // 1024 // 1024} MB)', end='', flush=True)
    except Exception as e:
        print(f'\n✗ Download failed: {e}')
        return False
    
    if progress:
        print()
    
    part.rename(dest)
    return True


def verify_sha256(path: Path, expected: str) -> bool:
    if not expected:
        return True
    h = hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest().lower() == expected.lower()


def create_shortcuts() -> bool:
    try:
        import win32com.client
        shell = win32com.client.Dispatch('WScript.Shell')
        
        for folder_name, target_path in [
            ('Programs', INSTALL_EXE),
            ('Desktop', INSTALL_EXE),
        ]:
            folder = shell.SpecialFolders(folder_name)
            shortcut = shell.CreateShortcut(f'{folder}\\Takoyo.lnk')
            shortcut.TargetPath = str(target_path)
            shortcut.WorkingDirectory = str(INSTALL_DIR)
            shortcut.Description = f'Takoyo AI Agent {VERSION_FULL}'
            shortcut.Save()
        return True
    except Exception as e:
        print(f'⚠ Shortcuts: {e}')
        return False


def register_uninstaller() -> bool:
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
        return True
    except Exception as e:
        print(f'⚠ Uninstaller: {e}')
        return False


def write_config(model_key: str) -> None:
    CONFIG_FILE.parent.mkdir(parents=True, exist_ok=True)
    config = {}
    if CONFIG_FILE.exists():
        try:
            config = json.loads(CONFIG_FILE.read_text())
        except Exception:
            pass
    config.update({
        'provider': 'local',
        'local_model': model_key,
        'local_ai': {'model': model_key, 'context': 8192, 'threads': 0},
        'dark_mode': True,
        'theme': 'dark',
    })
    CONFIG_FILE.write_text(json.dumps(config, indent=4))


# ── Main Installer ───────────────────────────────────────────────────────────
def install(models_to_download: list[str] | None = None, silent: bool = False) -> int:
    print(f'╔═══════════════════════════════════════════════╗')
    print(f'║  Takoyo v{VERSION} ({VERSION_NAME}) Installer        ║')
    print(f'║  Build {BUILD_ID}                              ║')
    print(f'╚═══════════════════════════════════════════════╝')
    print()
    
    # Check existing
    if INSTALL_EXE.exists() and not silent:
        existing = INSTALL_EXE.stat()
        print(f'Existing install: {existing.st_size / 1024 / 1024:.1f} MB, {existing.st_mtime}')
        resp = input('Overwrite? [Y/n]: ').strip().lower()
        if resp not in ('', 'y', 'yes'):
            print('Cancelled.')
            return 0
    
    # Download main executable
    print(f'\n[1/3] Downloading Takoyo.exe...')
    with tempfile.NamedTemporaryFile(delete=False, suffix='.exe') as tmp:
        tmp_path = Path(tmp.name)
    
    if not download_file(EXE_URL, tmp_path, 'Takoyo.exe'):
        return 1
    
    if EXE_SHA256 and not verify_sha256(tmp_path, EXE_SHA256):
        print('✗ Checksum mismatch!')
        return 1
    
    # Install
    print(f'\n[2/3] Installing to {INSTALL_DIR}...')
    INSTALL_DIR.mkdir(parents=True, exist_ok=True)
    shutil.move(str(tmp_path), INSTALL_EXE)
    print(f'  ✓ Installed ({INSTALL_EXE.stat().st_size / 1024 / 1024:.1f} MB)')
    
    # Shortcuts & uninstaller
    create_shortcuts()
    register_uninstaller()
    
    # Download models
    if models_to_download is None and not silent:
        print('\nAvailable local models:')
        for k, m in MODELS.items():
            mark = ' ★' if k == DEFAULT_MODEL else ''
            installed = '✓' if (MODELS_DIR / m['filename']).exists() else ' '
            print(f'  [{installed}] {m["name"]} ({m["size_mb"]} MB){mark}')
        print(f'\nDefault: {MODELS[DEFAULT_MODEL]["name"]}')
        choice = input('Download models? (comma-separated keys, "all", "default", or Enter to skip): ').strip()
        if choice.lower() in ('all', 'a'):
            models_to_download = list(MODELS.keys())
        elif choice.lower() in ('default', 'd', ''):
            models_to_download = [DEFAULT_MODEL]
        elif choice:
            models_to_download = [k.strip() for k in choice.split(',') if k.strip() in MODELS]
        else:
            models_to_download = []
    
    if models_to_download:
        print(f'\n[3/3] Downloading {len(models_to_download)} model(s)...')
        MODELS_DIR.mkdir(parents=True, exist_ok=True)
        for key in models_to_download:
            m = MODELS[key]
            dest = MODELS_DIR / m['filename']
            if dest.exists():
                print(f'  ✓ {m["name"]} already present')
                continue
            print(f'  Downloading {m["name"]} ({m["size_mb"]} MB)...')
            if not download_file(m['url'], dest, m['name']):
                print(f'  ✗ Failed: {m["name"]}')
            else:
                print(f'  ✓ {m["name"]}')
    
    # Write config with selected/default model
    model_key = models_to_download[0] if models_to_download else DEFAULT_MODEL
    write_config(model_key)
    print(f'\n✅ Config written: provider=local, model={model_key}')
    
    print(f'\n╔═══════════════════════════════════════════════╗')
    print(f'║  Installation complete!                      ║')
    print(f'╚═══════════════════════════════════════════════╝')
    print(f'  Start Menu: Programs → Takoyo')
    print(f'  Desktop:    Takoyo shortcut')
    print(f'  Install:    {INSTALL_DIR}')
    print(f'  Models:     {MODELS_DIR}')
    print(f'  Config:     {CONFIG_FILE}')
    print(f'\nRun Takoyo from Start Menu or: {INSTALL_EXE}')
    
    if not silent:
        input('\nPress Enter to exit...')
    return 0


def uninstall(silent: bool = False) -> int:
    if not silent:
        confirm = input('Uninstall Takoyo? This removes the app, shortcuts, and registry entry. Models and config are kept. [y/N]: ')
        if confirm.lower() not in ('y', 'yes'):
            print('Cancelled.')
            return 0
    
    # Remove shortcuts
    try:
        import win32com.client
        shell = win32com.client.Dispatch('WScript.Shell')
        for folder_name in ('Programs', 'Desktop'):
            folder = shell.SpecialFolders(folder_name)
            lnk = Path(folder) / 'Takoyo.lnk'
            if lnk.exists():
                lnk.unlink()
    except Exception:
        pass
    
    # Remove install dir
    if INSTALL_DIR.exists():
        shutil.rmtree(INSTALL_DIR, ignore_errors=True)
    
    # Remove uninstaller registry
    try:
        import winreg
        winreg.DeleteKey(winreg.HKEY_CURRENT_USER,
            r'Software\Microsoft\Windows\CurrentVersion\Uninstall\Takoyo')
    except Exception:
        pass
    
    print('✅ Takoyo uninstalled. Models and config preserved in ~/.takoyo/')
    return 0


# ── CLI ──────────────────────────────────────────────────────────────────────
def main() -> int:
    parser = argparse.ArgumentParser(description=f'Takoyo v{VERSION} ({VERSION_NAME}) Installer')
    parser.add_argument('--models', help='Comma-separated model keys to download (or "all", "default")')
    parser.add_argument('--silent', '-s', action='store_true', help='Non-interactive (uses --models or default)')
    parser.add_argument('--uninstall', action='store_true', help='Uninstall Takoyo')
    parser.add_argument('--quiet', '-q', action='store_true', help='Quiet uninstall')
    args = parser.parse_args()
    
    if args.uninstall:
        return uninstall(silent=args.quiet)
    
    models = None
    if args.models:
        if args.models.lower() == 'all':
            models = list(MODELS.keys())
        elif args.models.lower() == 'default':
            models = [DEFAULT_MODEL]
        else:
            models = [k.strip() for k in args.models.split(',') if k.strip() in MODELS]
    elif args.silent:
        models = [DEFAULT_MODEL]
    
    return install(models_to_download=models, silent=args.silent)


if __name__ == '__main__':
    sys.exit(main())