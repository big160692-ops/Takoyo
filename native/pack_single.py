"""Pack the native UI, Qt runtime, assets, and frozen engine into one Takoyo.exe."""

from __future__ import annotations

import os
import shutil
import struct
import subprocess
import sys
import time
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
NATIVE = ROOT / 'native'
DIST = NATIVE / 'dist'
ENGINE = NATIVE / 'engine-dist' / 'TakoyoEngine.exe'
SINGLE = NATIVE / 'single'
STAGE = SINGLE / 'stage'
WORK = SINGLE / 'work'
OUTPUT = SINGLE / 'Takoyo.exe'
LAUNCHER = NATIVE / 'launcher' / 'launcher.cpp'
ICON = ROOT / 'assets' / 'takoyo.ico'
VERSION = '2.1.4'
MINGW = Path(os.environ.get('MINGW_BIN', r'C:\Qt\Tools\mingw1310_64\bin'))

MAGIC = b'TKYOPK02'


def copy_tree(source: Path, target: Path) -> None:
    for item in source.iterdir():
        destination = target / item.name
        if item.is_dir():
            shutil.copytree(item, destination)
        else:
            shutil.copy2(item, destination)


def build_payload(build_id: int) -> Path:
    files = sorted(path for path in STAGE.rglob('*') if path.is_file())
    payload = WORK / 'payload.bin'
    with payload.open('wb') as handle:
        handle.write(MAGIC)
        handle.write(struct.pack('<II', build_id, len(files)))
        for path in files:
            relative = path.relative_to(STAGE).as_posix().encode('utf-8')
            data = path.read_bytes()
            packed = zlib.compress(data, 9)
            handle.write(struct.pack('<H', len(relative)))
            handle.write(relative)
            handle.write(struct.pack('<QQ', len(data), len(packed)))
            handle.write(packed)
    return payload


def write_resource(payload: Path) -> Path:
    resource = WORK / 'payload.rc'
    icon = str(ICON).replace('\\', '/')
    payload_path = str(payload).replace('\\', '/')
    resource.write_text(
        f'''#include <windows.h>

101 RCDATA "{payload_path}"
1 ICON "{icon}"

VS_VERSION_INFO VERSIONINFO
 FILEVERSION {VERSION.replace('.', ',')},0
 PRODUCTVERSION {VERSION.replace('.', ',')},0
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
        END
    END
    BLOCK "VarFileInfo"
    BEGIN
        VALUE "Translation", 0x409, 1200
    END
END
''',
        encoding='utf-8',
    )
    return resource


def run(command: list[str]) -> None:
    print('+', ' '.join(command))
    subprocess.run(command, check=True)


def main() -> int:
    if not (DIST / 'TakoyoNative.exe').exists():
        print('Build the Qt app and run windeployqt into native/dist first.', file=sys.stderr)
        return 1
    if not ENGINE.exists():
        print('Build the frozen engine into native/engine-dist first.', file=sys.stderr)
        return 1

    shutil.rmtree(STAGE, ignore_errors=True)
    shutil.rmtree(WORK, ignore_errors=True)
    STAGE.mkdir(parents=True)
    WORK.mkdir(parents=True)

    copy_tree(DIST, STAGE)
    shutil.copy2(ENGINE, STAGE / 'TakoyoEngine.exe')
    shutil.copytree(ROOT / 'assets', STAGE / 'assets')

    build_id = int(time.time())
    payload = build_payload(build_id)
    resource = write_resource(payload)

    windres = MINGW / 'windres.exe'
    compiler = MINGW / 'g++.exe'
    object_file = WORK / 'payload_res.o'
    run([str(windres), str(resource), '-O', 'coff', '-o', str(object_file)])
    run([
        str(compiler),
        '-O2',
        '-s',
        '-municode',
        '-mwindows',
        '-static',
        str(LAUNCHER),
        str(object_file),
        '-o',
        str(OUTPUT),
        '-lz',
        '-luser32',
    ])

    print(f'Build {build_id}: {OUTPUT} ({OUTPUT.stat().st_size / (1024 * 1024):.1f} MB)')
    shutil.rmtree(STAGE, ignore_errors=True)
    shutil.rmtree(WORK, ignore_errors=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
