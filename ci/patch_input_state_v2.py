#!/usr/bin/env python3
"""Rebuild the November 11 experimental Input State V2 hotfix from official 0.6.0.

All changes below are a small binary delta; NO proprietary XEX, XEXP or game
assets are included in this repository or in the resulting ZIP.
This does not compile the source changes, and has not been tested in gameplay.
"""
import argparse
import base64
import hashlib
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED, ZipInfo

ORIGINAL_ZIP = "d013f0d3cae6d02898a93e2ba3bb0f75c66e67fbbc3347750ae201036b3ac296"
ORIGINAL_EXE = "776609cdb5c3995e54a1d6f022f42b103c38047a029489f043c396200ff78d99"
PATCHED_EXE = "d9f505f2fbabf627cd23f3eb2b444aaca56de909e9e726fc8f638e1cfe91d693"
PATCHED_LENGTH = 39871488
# Exact bytes changed compared with the official EXE. The last 512 bytes are
# an allocated zero-initialized, writable PE section for temporary key state.
PATCHES = [
  (126,"08"),(128,"1432f4"),(201,"a05f"),(392,"000033"),
  (664,"2e636e64666978"),(673,"10"),(677,"905f02"),(681,"02"),
  (685,"626002"),(700,"40"),(703,"c0"),
  (33908704,"e9bb9b2d00"),(33923544,"24612d"),(33923984,"6c5f2d"),
  (33924397,"cf5d2d"),(33924786,"4a5c2d"),
  (33936703,"e97c2e2d0090"),(33937497,"e3292d"),
  (36897344,"4883ec28807b2f007507e801ded2ffeb05e89ae0d2ff4883c428c3"),
  (36897408,"b64:VVNWV0iD7ChIic5IjR1ugSwAizvHAwAAAAAx7Tn9cyCLVKsEuXCuBUCAvhQBAAAAdAW5wPwKIOg04NL//8Xr3EiDxChfXltdwwAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAABTVkiD7ChIidboUtTS/0iLFkiLRghIKdBIg/gQdRZIjQXbgCwA8w9vAvMPf0AExwAEAAAASIPEKF5bwwAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAUUiD7CDo1v7//0iDxCBZVUFXQVbpLGTS/wAAAAAAAABSSItV2EiLEkiF0nQFgzpXdAY0ASDB6wKEyVoPhYbR0v/pX9HS/w=="),
]
NOTES = """Condemned2Recomp 0.6.0 Windows x64 Input State V2 — EXPERIMENTAL
This is a binary hotfix of the official precompiled executable, NOT a full
source recompile and NOT a verified fix for weapon pickup or doll-factory QTE.
Official game assets are NOT included. Use your own legally acquired assets.
Modified EXE SHA256: d9f505f2fbabf627cd23f3eb2b444aaca56de909e9e726fc8f638e1cfe91d693
The unchanged DLLs and UI assets retain the ORIGINAL timestamps. This is
normal: the experimental patch modifies only the EXE.
Test from a separate directory, disable auto-updating and back up UserData.
"""

def digest(blob):
    return hashlib.sha256(blob).hexdigest()

def patch_exe(blob):
    if digest(blob) != ORIGINAL_EXE:
        raise RuntimeError("Executable checksum mismatch; refusing patch")
    result = bytearray(blob)
    if len(result) > PATCHED_LENGTH:
        raise RuntimeError("Unexpected EXE length")
    result.extend(bytes(PATCHED_LENGTH-len(result)))
    previous_end = 0
    for offset, encoded in PATCHES:
        part = base64.b64decode(encoded[4:], validate=True) if encoded.startswith('b64:') else bytes.fromhex(encoded)
        if offset < previous_end or offset+len(part)>len(result):
            raise RuntimeError("Overlapping/out-of-range patch")
        result[offset:offset+len(part)] = part
        previous_end = offset+len(part)
    if digest(result) != PATCHED_EXE:
        raise RuntimeError("Patched executable checksum mismatch")
    return bytes(result)

def build(source, output):
    if digest(source.read_bytes()) != ORIGINAL_ZIP:
        raise RuntimeError("Official release ZIP checksum mismatch")
    with ZipFile(source) as z:
        if z.testzip() is not None:
            raise RuntimeError("Corrupt original release ZIP")
        patched = patch_exe(z.read("condemned2recomp.exe"))
        with ZipFile(output, "w", ZIP_DEFLATED, compresslevel=6) as out:
            for info in z.infolist():
                new_info = ZipInfo(info.filename, info.date_time)
                new_info.compress_type = ZIP_DEFLATED if not info.is_dir() else info.compress_type
                new_info.external_attr = info.external_attr
                new_info.create_system = info.create_system
                new_info.flag_bits = info.flag_bits
                if info.filename == "condemned2recomp.exe":
                    new_info.date_time = (2026,11,11,11,15,0)
                out.writestr(new_info, patched if info.filename == "condemned2recomp.exe" else z.read(info))
            out.writestr("INPUT_STATE_V2_README.txt", NOTES)
    with ZipFile(output) as check:
        if check.testzip() is not None or digest(check.read("condemned2recomp.exe")) != PATCHED_EXE:
            raise RuntimeError("Output archive verification failed")
    print("SUCCESS: patched EXE SHA256", PATCHED_EXE)
    print("SUCCESS: packaged Windows ZIP", output, output.stat().st_size)

if __name__ == "__main__":
    p = argparse.ArgumentParser()
    p.add_argument("--input", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    a = p.parse_args()
    build(a.input, a.output)
