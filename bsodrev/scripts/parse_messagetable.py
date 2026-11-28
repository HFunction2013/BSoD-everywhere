#!/usr/bin/env python3
"""Extract the bugcheck code -> friendly name table from ntoskrnl.exe.

The table is the kernel's RT_MESSAGETABLE resource (Type 11, Name ID 1), loaded
by InitBootProcessor into the KiBugCodeMessages global and walked by
KeGetBugMessageText. This script parses the PE resource directory, locates the
data item, and dumps every {code, name} pair to JSON.

Usage:
    python3 parse_messagetable.py ntoskrnl.exe bugcheck_names.json

Layout (verified against 10.0.18362.2274):

  KiBugCodeMessages block header:
      DWORD block_count
      block_count x { DWORD LoId, DWORD HighId, DWORD Offset }

  Each string entry at base + Offset:
      WORD Length        ; whole entry length in bytes (header + text)
      WORD Flags
      BYTE text[Length - 4]   ; ASCII, NUL-terminated

A code in [LoId, HighId] maps to the (code - LoId)-th entry in that block.
"""
import json
import struct
import sys


def parse_pe_sections(data: bytes):
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    nsec = struct.unpack_from("<H", data, pe + 6)[0]
    sizeof_opt = struct.unpack_from("<H", data, pe + 20)[0]
    sec_off = pe + 24 + sizeof_opt
    secs = []
    for i in range(nsec):
        o = sec_off + i * 40
        name = data[o:o + 8].rstrip(b"\0").decode("ascii", "ignore")
        vsz, va, rsz, raw = struct.unpack_from("<IIII", data, o + 8)
        secs.append((name, va, vsz, raw, rsz))
    return secs


def main() -> int:
    exe = sys.argv[1] if len(sys.argv) > 1 else r"C:\Windows\System32\ntoskrnl.exe"
    out = sys.argv[2] if len(sys.argv) > 2 else "bugcheck_names.json"
    data = open(exe, "rb").read()
    secs = parse_pe_sections(data)

    rsrc = next(s for s in secs if s[0] == ".rsrc")
    rsrc_va, rsrc_raw = rsrc[1], rsrc[3]

    def rva_to_off(rva: int):
        for name, va, vsz, raw, rsz in secs:
            if va <= rva < va + max(vsz, rsz):
                return raw + (rva - va)
        return None

    def dir_entries(off: int):
        n_named, n_id = struct.unpack_from("<HH", data, off + 12)
        n = n_named + n_id
        for i in range(n):
            e = off + 16 + i * 8
            name, off2 = struct.unpack_from("<II", data, e)
            yield name, off2

    def walk(dir_off, want_type, want_name):
        for name, off2 in dir_entries(dir_off):
            if (name & 0x80000000) == 0 and name == want_type:
                sub = dir_off + (off2 & 0x7FFFFFFF)
                for n2, o2 in dir_entries(sub):
                    if (n2 & 0x80000000) == 0 and n2 == want_name:
                        return dir_off + (o2 & 0x7FFFFFFF)
        return None

    # RT_MESSAGETABLE = 11, resource name ID 1.
    lang_dir = walk(rsrc_raw, 11, 1)
    if lang_dir is None:
        print("RT_MESSAGETABLE type 11 / id 1 not found", file=sys.stderr)
        return 1

    pairs = {}
    for _n3, o3 in dir_entries(lang_dir):
        de = rsrc_raw + o3
        rva, size = struct.unpack_from("<II", data, de)
        base = rva_to_off(rva)
        count = struct.unpack_from("<I", data, base)[0]
        p = base + 4
        for _k in range(count):
            lo, hi, ent = struct.unpack_from("<III", data, p)
            p += 12
            block = base + ent
            v7 = block
            for code in range(lo, hi + 1):
                ln = struct.unpack_from("<H", data, v7)[0]
                # flags = struct.unpack_from("<H", data, v7 + 2)[0]
                text = data[v7 + 4:v7 + ln]
                text = text.split(b"\0")[0].decode("ascii", "ignore").strip("\r\n ")
                pairs[code] = text
                v7 += ln
        print("data rva 0x%x size %d: %d pairs" % (rva, size, len(pairs)))

    json.dump({("0x%X" % k): v for k, v in sorted(pairs.items())},
              open(out, "w", encoding="utf-8"), indent=1)
    print("wrote %s (%d entries)" % (out, len(pairs)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
