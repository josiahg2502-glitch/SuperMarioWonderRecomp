#!/usr/bin/env python3
"""Pack the Windows release: dist/SuperMarioWonderRecomp-<VERSION>-windows.zip (README.md, "Install").

The zip holds the program only, never game data:

    SuperMarioWonderRecomp/wonder.exe                 the build (private/wonder/build)
    SuperMarioWonderRecomp/msvcp140.dll, vcruntime*   the C++ runtime, app-local
    SuperMarioWonderRecomp/Install.bat                puts the player's game data in data\\ (from the data zip)
    SuperMarioWonderRecomp/README.txt                 the steps
    SuperMarioWonderRecomp/mods/hello/hello.dll       the example code mod, off ("-hello" until renamed)

The game data comes from the player's own copy of the game: tools/pack_pc_wonder.py makes the data zip that
Install.bat takes. The updater (runtime/src/updater_win.cpp) downloads this same zip from the GitHub release whose
tag is newer than the running build, so attach the zip to the release under this name.

Usage: pack_release.py [--build private/wonder/build] [--out dist]
"""
import argparse
import glob
import os
import sys
import zipfile

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOP = "SuperMarioWonderRecomp/"
REDIST_ROOTS = [r"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Redist\MSVC",
                r"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Redist\MSVC"]

INSTALL = r"""@echo off
rem Super Mario Bros. Wonder recomp: installs your game data next to the program.
rem Drag your data zip (made from your own copy of the game, see README.txt) onto this file, or run:
rem     Install.bat path\to\Wonder-pc-data.zip
setlocal
cd /d "%~dp0"
if "%~1"=="" (
  echo Drag your Wonder data zip onto Install.bat ^(see README.txt^).
  pause
  exit /b 1
)
echo Unpacking %~nx1 ...
if exist "_install" rmdir /s /q "_install"
mkdir "_install"
rem Windows's own tar (bsdtar) reads zips; another tar on PATH may not take drive letters.
"%SystemRoot%\System32\tar.exe" -xf "%~1" -C "_install" || (echo Could not unpack %~nx1. & pause & exit /b 1)
if not exist "data" mkdir "data"
rem The zip keeps the folders under private\wonder\ (tools/pack_pc_wonder.py).
for /d %%D in ("_install\private\wonder\*") do (
  if exist "data\%%~nxD" rmdir /s /q "data\%%~nxD"
  move "%%D" "data\" >nul
)
rmdir /s /q "_install"
if not exist "data\extract\romfs" (echo The zip had no game files in it. & pause & exit /b 1)
echo Done. Start wonder.exe to play.
pause
"""

README = """Super Mario Bros. Wonder - native PC recomp (Windows)

Install
  1. Unzip this folder somewhere with a short path (for example C:\\Games\\SuperMarioWonderRecomp).
  2. Drag your Wonder data zip onto Install.bat. The data comes from your own copy of the game
     (version 1.2.0): the project's tools/pack_pc_wonder.py makes it from a dump (README.md, "Windows").
  3. Start wonder.exe.

Settings are in the game itself: "Recomp Settings" in the pause menu or the world map's menu (render resolution,
fullscreen, fast-forward, faster loads, save before quitting, Quit Game button, automatic updates).
F11 or Alt+Enter also switch fullscreen. Fast-forward: Tab or the right stick click.

Updates install themselves (Recomp Settings, Automatic Updates): a new version starts the next time the
game does.

Mods go in data\\mods\\<name>\\: RomFS mods (romfs\\ in the mod's folder, as Switch mods ship),
exefs .ips patches, and code mods (a .dll; mods\\hello is an example, off while its folder starts with "-").

The log is data\\nx.log; the save is in data\\user\\save.
"""


def redist():
    for root in REDIST_ROOTS:
        hits = sorted(glob.glob(os.path.join(root, "*", "x64", "Microsoft.VC14*.CRT")))
        if hits:
            for name in ("msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll"):
                p = os.path.join(hits[-1], name)
                if os.path.isfile(p):
                    yield p, name
            return
    sys.exit("no Visual C++ redistributable found")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build", default=os.path.join(REPO, "private", "wonder", "build"))
    ap.add_argument("--out", default=os.path.join(REPO, "dist"))
    a = ap.parse_args()
    version = open(os.path.join(REPO, "VERSION")).read().strip()
    exe = os.path.join(a.build, "wonder.exe")
    if not os.path.isfile(exe):
        sys.exit(f"missing {exe} (build it first)")
    files = [(exe, "wonder.exe"), *redist()]
    hello = os.path.join(a.build, "mods", "hello", "hello.dll")
    if os.path.isfile(hello):
        files.append((hello, "mods/-hello/hello.dll"))
    os.makedirs(a.out, exist_ok=True)
    out = os.path.join(a.out, f"SuperMarioWonderRecomp-{version}-windows.zip")
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr(TOP + "Install.bat", INSTALL.replace("\n", "\r\n"))
        z.writestr(TOP + "README.txt", README.replace("\n", "\r\n"))
        for p, name in files:
            z.write(p, TOP + name)
    print(f"wrote {out} ({os.path.getsize(out) / 2**20:.1f} MB)")


if __name__ == "__main__":
    main()
