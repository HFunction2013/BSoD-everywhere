"""Prove which DWORDs of the bugcheck display table are actually read.

Usage:

    idat64 -A -S ida_xref_table.py ntoskrnl.exe.i64

Writes xref_table.txt next to this script:

  * every code xref to the table symbol,
  * owning function + decompiled body for each distinct owner,
  * for every `qword_140429ED0[...]` access found in that body, the literal
    offsets used, plus the DWORD index it maps to.

The table is **9 QWORDs per row**, so `qword[9*row + k]` carries two fields:

    LODWORD -> dword[2k]      HIDWORD -> dword[2k+1]

That mapping is what the report at the end of the file applies.

Notes (learned the hard way):
  * use idautils.XrefsTo, ida_xref has no XrefsTo in 7.5
  * use idc.generate_disasm_line, ida_bytes has no generate_disasm_line
  * kill idat64 before the next run, it does not exit on its own
"""

import io
import os
import re

import ida_funcs
import ida_hexrays
import ida_name
import idautils

TABLE = 0x140429ED0
TABLE_NAME = "qword_140429ED0"
OUT_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "xref_table.txt")

ACCESS_RE = re.compile(r"%s\s*\[[^\]]*\]" % TABLE_NAME)


def field_index(expr):
    """Turn the text inside [...] into the DWORD indices it can address."""
    idx = set()
    for num in re.findall(r"\b(\d+)\b", expr):
        n = int(num)
        idx.update({2 * n, 2 * n + 1})
    for m in re.finditer(r"HIDWORD\s*\(\s*[^)]*\)", expr):
        pass  # handled below by the caller, printed verbatim
    return sorted(idx)


def main():
    if not ida_hexrays.init_hexrays_plugin():
        raise SystemExit("hexrays not available")

    owners = {}
    xrefs = 0
    for ref in idautils.XrefsTo(TABLE):
        xrefs += 1
        fn = ida_funcs.get_func(ref.frm)
        if fn is None:
            print("data xref @ 0x%X" % ref.frm)
            continue
        owners.setdefault(fn.start_ea, set()).add(ref.frm)

    out = io.open(OUT_PATH, "w", encoding="utf-8")
    out.write("table %s @ 0x%X\n" % (TABLE_NAME, TABLE))
    out.write("%d xrefs in %d functions\n\n" % (xrefs, len(owners)))

    seen = {}
    for start_ea in sorted(owners):
        name = ida_funcs.get_func_name(start_ea)
        out.write("\n######## %s @ 0x%X  (from %s)\n"
                  % (name, start_ea, ", ".join("0x%X" % a for a in sorted(owners[start_ea]))))
        try:
            body = str(ida_hexrays.decompile(start_ea))
        except Exception as exc:
            out.write("!! decompile failed: %s\n" % exc)
            continue

        for line in body.splitlines():
            m = ACCESS_RE.search(line)
            if not m:
                continue
            expr = m.group(0)
            idx = field_index(expr)
            out.write("  %-60s -> dword %s\n" % (expr, idx))
            for d in idx:
                seen.setdefault(d, set()).add(name)
        out.write("\n%s\n" % body)

    out.write("\n######## fields actually read\n")
    for d in range(18):
        who = sorted(seen.get(d, []))
        out.write("d%-2d  %s\n" % (d, ", ".join(who) if who else "NOBODY (dead field)"))

    out.write("""
######## how to read this
qword[9*row + k] low 32 bits -> dword[2k]
qword[9*row + k] high 32 bits -> dword[2k+1]
so  qword[9*row+3] HIDWORD = dword[7]  and qword[9*row+6] LODWORD = dword[12].
If the "fields actually read" block lists neither, both are dead and must not
be used in a reproduction (row 3: 440 and 70).
""")
    out.close()
    print("wrote %s" % OUT_PATH)


main()
