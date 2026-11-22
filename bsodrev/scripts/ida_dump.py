"""Dump Hex-Rays output for a list of (named) functions out of ntoskrnl.exe.

Usage (IDA 7.5, bundled python 3.8):

    idat64 -A -S ida_dump.py ntoskrnl.exe.i64

Two-pass workflow required — a single `-B -A -S` pass on ntoskrnl gives up
half way through auto-analysis:

    idat64 -B -A ntoskrnl.exe              # pass 1, builds ntoskrnl.exe.i64
    idat64 -A -S ida_dump.py ntoskrnl.exe.i64   # pass 2, this script

Before re-running:  Stop-Process -Name idat64 -Force   (it never exits and
keeps the .i64 locked).  Also put IDA's python38 first on PATH and clear
PYTHONHOME / PYTHONPATH, otherwise `import idc` fails.

Edit OUT_PATH / FUNCS below (or leave FUNCS empty: then every function named
in the FUNCTION_NAMES list of the task is resolved by name).
"""

import io
import os

import ida_hexrays
import ida_funcs
import ida_name

OUT_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "dump.txt")

FUNCS = [
    # bugcheck screen
    "BgpFwDisplayBugCheckScreen",
    "BcpGetDisplayType",
    "BcpGetComponentOffsets",
    "BcpDisplayCriticalString",
    "BcpDisplayErrorInformation",
    "BcpDisplayProgress",
    "BgpDisplaySafeToPowerOffScreen",
    # font / metrics
    "BgpRasGetGlyphTextCellDimensions",
    "BgpRasGetGlyphAdvanceWidth",
    "BgpFoDetermineFontInformation",
    "BgpFoGetTextMetrics",
    "BgpFoInitialize",
    "BgpFwLibraryInitialize",
    "BcpGetCharacterMaxResourceProfile",
    # rasterizer
    "BgpRasInitializeRasterizer",
    "BgpRasPrintGlyph",
    "RaspScaleCoordinates",
    "RaspScanConvert",
    "RaspBitmapCache",
    # qr
    "BgpGxProcessQrCodeBitmap",
    "BgpGxDrawRectangle",
]


def dump_one(out, name):
    ea = ida_name.get_name_ea(0, name)
    if ea == idaapi.BADADDR:                    # noqa: F821  (idc alias)
        out.write("######## %s not found\n\n" % name)
        return False
    out.write("######## %s @ 0x%X\n" % (name, ea))
    try:
        out.write(str(ida_hexrays.decompile(ea)) + "\n\n")
        return True
    except Exception as exc:                    # decompilation can refuse
        out.write("!! decompile failed: %s\n\n" % exc)
        return False


def main():
    if not ida_hexrays.init_hexrays_plugin():
        raise SystemExit("hexrays not available")
    with io.open(OUT_PATH, "w", encoding="utf-8") as out:
        ok = 0
        for name in FUNCS:
            ok += 1 if dump_one(out, name) else 0
        out.write("DONE %d/%d\n" % (ok, len(FUNCS)))
    print("wrote %s" % OUT_PATH)


main()
