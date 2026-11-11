#!/usr/bin/env python3
"""Repack the official Condemned2Recomp 0.6.0 Windows release with a narrowly
scoped native Input_KeyUp binary hotfix. No Xbox XEX / game data is involved.
The original release hash and every patched instruction are verified.
"""
import argparse
import hashlib
import struct
from pathlib import Path
from zipfile import ZipFile

OFFICIAL_ZIP_SHA256 = "d013f0d3cae6d02898a93e2ba3bb0f75c66e67fbbc3347750ae201036b3ac296"
OFFICIAL_EXE_SHA256 = "776609cdb5c3995e54a1d6f022f42b103c38047a029489f043c396200ff78d99"
IMAGE_BASE = 0x140000000
CALL_VA = 0x14205e458
CAVE_FILE_OFFSET = 0x2330240
DOWN_VA = 0x14205ec50
UP_VA = 0x14205eef0

def sha256(data):
    return hashlib.sha256(data).hexdigest()

def rel32(instr_va, destination):
    displacement = destination - (instr_va + 5)
    if not -(2**31) <= displacement < 2**31:
        raise ValueError("Relative jump out of range")
    return struct.pack("<i", displacement)

def patch_exe(data):
    if sha256(data) != OFFICIAL_EXE_SHA256:
        raise ValueError("Unknown executable version - refusing to patch")
    exe = bytearray(data)
    pe_header = struct.unpack_from("<I", exe, 0x3c)[0]
    if exe[pe_header:pe_header+4] != b"PE\\x00\\x00":
        raise ValueError("Not a PE file")
    if struct.unpack_from("<H",exe,pe_header+24)[0] != 0x20B:
        raise ValueError("Not PE32+")
    section_count = struct.unpack_from("<H",exe,pe_header+6)[0]
    section_headers = pe_header + 24 + struct.unpack_from("<H",exe,pe_header+20)[0]
    sections = {}
    for index in range(section_count):
        offset = section_headers + 40 * index
        name = bytes(exe[offset:offset+8]).rstrip(b"\\x00").decode("ascii")
        virtual_size, rva, raw_size, raw_offset = struct.unpack_from("<IIII",exe,offset+8)
        sections[name] = (offset,virtual_size,rva,raw_size,raw_offset)
    th,t_virtual,t_rva,t_raw,t_offset = sections[".text"]
    _,_,r_rva,_,_ = sections[".rdata"]
    if (t_virtual,t_rva,t_raw,t_offset,r_rva) != (0x232fe06,0x1000,0x2330000,0x400,0x2331000):
        raise ValueError("Unexpected PE section layout")
    def file_offset(va):
        return t_offset + (va - IMAGE_BASE) - t_rva
    location = file_offset(CALL_VA)
    if exe[location:location+5] != bytes.fromhex("e8 f3 07 00 00"):
        raise ValueError("NativeKeyPress call site changed")
    if exe[file_offset(0x14205e46b):file_offset(0x14205e46b)+10] != bytes.fromhex("80 7b 2f 00 0f 85 69 ff ff ff"):
        raise ValueError("NativeKeyPress release branch changed")
    if exe[CAVE_FILE_OFFSET:CAVE_FILE_OFFSET+64] != b"\\xcc"*64:
        raise ValueError("Required PE code padding is not empty")
    cave_va = IMAGE_BASE + t_rva + CAVE_FILE_OFFSET - t_offset
    code = bytearray.fromhex("48 83 ec 28 80 7b 2f 00 75 07")
    code += b"\\xe8" + rel32(cave_va+len(code), DOWN_VA)
    code += bytes.fromhex("eb 05")
    code += b"\\xe8" + rel32(cave_va+len(code), UP_VA)
    code += bytes.fromhex("48 83 c4 28 c3")
    if len(code) != 27:
        raise AssertionError("Unexpected trampoline length")
    exe[CAVE_FILE_OFFSET:CAVE_FILE_OFFSET+len(code)] = code
    exe[location:location+5] = b"\\xe8" + rel32(CALL_VA,cave_va)
    struct.pack_into("<I",exe,th+8,t_raw)
    if CALL_VA + 5 + struct.unpack_from("<i",exe,location+1)[0] != cave_va:
        raise AssertionError("Patched call does not point at trampoline")
    for at,target in [(10,DOWN_VA),(17,UP_VA)]:
        loc = CAVE_FILE_OFFSET + at
        if exe[loc] != 0xe8 or cave_va+at+5+struct.unpack_from("<i",exe,loc+1)[0] != target:
            raise AssertionError("Trampoline target verification failed")
    return bytes(exe)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", type=Path, required=True, help="Official v0.6.0 Windows ZIP")
    parser.add_argument("--output", type=Path, required=True, help="Patched ZIP destination")
    args = parser.parse_args()
    if sha256(args.input.read_bytes()) != OFFICIAL_ZIP_SHA256:
        raise ValueError("Archive digest mismatch")
    with ZipFile(args.input) as original, ZipFile(args.output, "w") as result:
        if original.testzip() is not None:
            raise ValueError("Official ZIP failed CRC test")
        patched = patch_exe(original.read("condemned2recomp.exe"))
        for member in original.infolist():
            result.writestr(member, patched if member.filename == "condemned2recomp.exe" else original.read(member.filename))
        result.writestr("INPUT_HOTFIX_README.txt", """EXPERIMENTAL BINARY HOTFIX for Condemned2Recomp v0.6.0.
No compiler required. This is a patch of the official compiled EXE, not a source recompilation.
On NativeKeyPress(release=true) the compiled executable called Input_KeyDown;
this build calls the existing Input_KeyUp hook instead.
No proprietary game content is included. No additional DLL is needed.
NOT TESTED in gameplay. Weapon pickup, QTE and forensic-tool issues may remain.
Extract into a NEW folder, use your own legally obtained game assets,
back up saves, disable auto-update and test the doll factory boss.
Revert by restoring the official v0.6.0 ZIP.
""")
    with ZipFile(args.output) as result:
        assert result.testzip() is None
        assert result.read("condemned2recomp.exe") == patched
    print("Patched EXE SHA256:",sha256(patched))
    print("Patched ZIP SHA256:",sha256(args.output.read_bytes()))
    print("Output:",args.output)

if __name__ == "__main__":
    main()
