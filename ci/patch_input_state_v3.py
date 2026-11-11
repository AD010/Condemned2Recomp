#!/usr/bin/env python3
"""Valid PE32+ input hotfix experiment; no game assets, no new PE section."""
import argparse
import hashlib
import struct
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED, ZipInfo
from patch_input_state_v2 import patch_exe, ORIGINAL_ZIP, ORIGINAL_EXE

EXPECTED_EXE_SHA256 = "443985e92d42f6f4aa2badc00802978f863e7b7bcf25fdb82316bd094d6e87de"
PATCH_RANGES = [
    (0x20567e0,5), (0x205a1d8,3), (0x205a390,3),
    (0x205a52d,3), (0x205a6b2,3), (0x205d53f,6),
    (0x205d859,3), (0x2330240,27), (0x2330280,358),
]
README = """Condemned 2 0.6.0 — Windows x64 Input State V3 EXPERIMENTAL

This is a binary hotfix of the official executable, NOT a full source recompile.
Unlike V2, it preserves the original 7-section PE32+ structure, image size,
import directory, relocation table, TLS, and resource directory.
The added code uses spare bytes in executable .text; native input state resides
in explicitly extended zero-initialized .data memory.

Windows loader validation does not prove game logic is correct. The weapon-pickup
and boss-QTE bug is NOT confirmed fixed. Back up your saves and test separately.
The user's copyrighted XEX/XEXP and game assets are NOT distributed.
"""

def digest(data): return hashlib.sha256(data).hexdigest()

def patch_v3(original):
    if digest(original) != ORIGINAL_EXE:
        raise ValueError("Unsupported official EXE SHA256")
    v2 = patch_exe(original)
    dst = bytearray(original)
    for begin, length in PATCH_RANGES:
        dst[begin:begin+length] = v2[begin:begin+length]
    pe = struct.unpack_from("<I",dst,0x3c)[0]
    sec = pe+24+struct.unpack_from("<H",dst,pe+20)[0]
    if struct.unpack_from("<H",dst,pe+6)[0]!=7:
        raise ValueError("Original PE must have exactly 7 sections")
    if dst[sec:sec+8].rstrip(b"\x00") != b".text":
        raise ValueError("Missing .text header")
    if dst[sec+80:sec+88].rstrip(b"\x00") != b".data":
        raise ValueError("Missing .data header")
    if struct.unpack_from("<I",dst,sec+8)[0] != 0x232fe06:
        raise ValueError("Unexpected .text VirtualSize")
    if struct.unpack_from("<I",dst,sec+80+8)[0] != 0xd02a8:
        raise ValueError("Unexpected .data VirtualSize")
    # The old sections have sufficient alignment slack; add NO section.
    struct.pack_into("<I",dst,sec+8,0x2330000)
    struct.pack_into("<I",dst,sec+80+8,0xd0300)
    state_va=0x140000000 + 0x257c2b0
    for sig in ("488d1d6e812c00","488d05db802c00"):
        pattern=bytes.fromhex(sig)
        off=dst.find(pattern,0x2330280,0x2330400)
        if off<0: raise ValueError("Missing state pointer")
        after=0x140000c00+off+7
        relative=state_va-after
        if not -(1<<31)<=relative<1<<31:
            raise ValueError("State pointer outside rel32 range")
        struct.pack_into("<i",dst,off+3,relative)
    if struct.unpack_from("<I",dst,pe+24+56)[0] != 0x2610000:
        raise ValueError("PE SizeOfImage changed")
    if len(dst)!=len(original) or b".cndfix" in dst[:0x400]:
        raise ValueError("PE structure corrupted")
    result=bytes(dst)
    if digest(result)!=EXPECTED_EXE_SHA256:
        raise ValueError("V3 EXE SHA256 mismatch; refusing to publish")
    return result

def main():
    p=argparse.ArgumentParser()
    p.add_argument("--input",required=True,type=Path)
    p.add_argument("--output",required=True,type=Path)
    a=p.parse_args()
    if digest(a.input.read_bytes())!=ORIGINAL_ZIP:
        raise ValueError("Official ZIP SHA256 mismatch")
    with ZipFile(a.input) as old:
        if old.testzip() is not None:
            raise ValueError("Official ZIP CRC failure")
        exe=patch_v3(old.read("condemned2recomp.exe"))
        with ZipFile(a.output,"w",ZIP_DEFLATED,compresslevel=6) as new:
            for info in old.infolist():
                dstinfo=ZipInfo(info.filename, (2026,11,11,13,0,0) if info.filename=="condemned2recomp.exe" else info.date_time)
                dstinfo.compress_type=ZIP_DEFLATED if not info.is_dir() else info.compress_type
                dstinfo.external_attr=info.external_attr
                new.writestr(dstinfo,exe if info.filename=="condemned2recomp.exe" else old.read(info))
            new.writestr("INPUT_STATE_V3_README.txt",README+"EXE SHA256: "+digest(exe)+"\n")
    with ZipFile(a.output) as verify:
        if verify.testzip() is not None or digest(verify.read("condemned2recomp.exe")) != EXPECTED_EXE_SHA256:
            raise ValueError("Invalid packaged ZIP")
    print("Verified input-state V3 x64 PE",digest(exe),a.output)

if __name__=="__main__":main()
