#!/usr/bin/env python3
"""Check that Windows accepts the patched PE32+ executable before publishing.
Starts the application suspended, then terminates it without running game logic.
"""
import argparse
import os
import subprocess
import tempfile
import zipfile
from pathlib import Path

p=argparse.ArgumentParser()
p.add_argument("--archive",type=Path,required=True)
args=p.parse_args()
with tempfile.TemporaryDirectory(prefix="cnd2-win64-loader-") as tmp:
    with zipfile.ZipFile(args.archive) as z:
        if z.testzip() is not None: raise RuntimeError("ZIP CRC check failed")
        z.extractall(tmp)
    exe=Path(tmp)/"condemned2recomp.exe"
    if not exe.is_file(): raise RuntimeError("EXE missing in archive")
    try:
        # CREATE_SUSPENDED (0x00000004) maps the image and verifies the loader
        # while keeping all user/game code stopped.
        proc=subprocess.Popen([str(exe)],cwd=tmp,creationflags=0x00000004)
    except OSError as exc:
        raise RuntimeError("CreateProcess rejected PE32+: "+str(exc)) from exc
    else:
        print("PASS: Windows CreateProcess successfully mapped PE32+ (PID:",proc.pid,")")
        try:
            proc.kill()
            proc.wait(timeout=15)
        except ProcessLookupError: pass
