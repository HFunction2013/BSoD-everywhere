# `bsodrev/` — the raw reverse-engineering material

This directory holds the actual working material, not prose: the IDA scripts,
the decompiled functions distilled to pseudocode, the extracted tables and the
extracted assets. `docs/` explains *why*; this directory is *what the tooling
produced*, so a new person can rerun it and get the same numbers.

Target binary: `C:\Windows\System32\ntoskrnl.exe`, **10.0.18362.2274**
(Windows 10 1903), image base `0x140000000`. IDA 7.5 (`idat64.exe`), Hex-Rays.

## Layout

| path | contents |
|---|---|
| `scripts/ida_dump.py` | dumps Hex-Rays output for the ~20 functions this needs, in one run |
| `scripts/ida_xref_table.py` | walks `XrefsTo(qword_140429ED0)`, decompiles every owner, and prints which DWORDs are actually read (this is how `d7`/`d12` were proven dead) |
| `scripts/extract_boot_bitmaps.ps1` | carves the WIM out of `bootres.dll` and unpacks `qrcode1..5.bmp` / `winlogo1..5.bmp` |
| `scripts/parse_messagetable.py` | parses the PE resource directory and dumps the RT_MESSAGETABLE (Type 11, ID 1) bugcheck name pairs to JSON |
| `scripts/gen_bugcodes.py` | filters the JSON to displayable codes and regenerates `src/bugcodes.c` |
| `functions/layout_table.md` | `BcpGetDisplayType`, `BcpGetComponentOffsets` |
| `functions/text_engine.md` | `BcpDisplayCriticalString`, `BcpDisplayErrorInformation`, `BcpDisplayProgress` |
| `functions/font_metrics.md` | `BgpRasGetGlyphTextCellDimensions`, `BgpFoGetTextMetrics`, `BgpFoDetermineFontInformation` — the `cell`↔`em` relation |
| `functions/rasterizer.md` | `RaspScaleCoordinates`, `BgpRasPrintGlyph`, `RaspScanConvert`, `RaspBitmapCache` |
| `functions/entry_and_qr.md` | `BgpFwDisplayBugCheckScreen`, `BgpFwLibraryInitialize`, `BgpFoInitialize`, `BgpGx*` |
| `functions/bugcode_messages.md` | `KeGetBugMessageText`, `KiBugCodeMessages`, `InitBootProcessor` — stop-code name table and `0x%08x` fallback |
| `tables/display_table.csv` / `.json` | the 5×18 table with field names, ready to diff |
| `tables/bugcheck_names.json` | 483 `code → name` pairs extracted from the kernel message resource |
| `assets/qr_matrix.txt` | the 25×25 matrix and how it was sampled |
| `assets/texts.txt` | screen strings with their `ntoskrnl` file offsets |
| `assets/fonts.md` | which boot face, and the three-wrap acceptance test for any candidate |

## Running the scripts

```powershell
Stop-Process -Name idat64 -Force -ErrorAction SilentlyContinue   # it never exits
# IDA's bundled python must be first on PATH; clear PYTHONHOME / PYTHONPATH
idat64 -B -A ntoskrnl.exe                                       # pass 1
idat64 -A -S bsodrev\scripts\ida_dump.py      ntoskrnl.exe.i64   # pass 2
idat64 -A -S bsodrev\scripts\ida_xref_table.py ntoskrnl.exe.i64
```

Outputs land in this directory (`dump.txt`, `xref_table.txt`).

The stop-code name table does not need IDA:

```powershell
python scripts\parse_messagetable.py C:\Windows\System32\ntoskrnl.exe tables\bugcheck_names.json
python scripts\gen_bugcodes.py        # regenerates ../src/bugcodes.c
```

## Conventions in the pseudocode

* names follow the PDB (`Bcp*` = bugcheck, `Bgp*` = boot graphics, `Rasp*` =
  rasterizer, `Fop*` = font opening), so they can be matched against the real
  dumps line by line;
* `row_of(row)` means `qword_140429ED0 + 9*row`, addressed as DWORDs;
* `d4`, `d11`, … are the DWORD indices from `tables/display_table.csv`;
* arithmetic is kept exactly as Hex-Rays printed it (including `<< 6` /
  `>> 6` fixed-point steps) rather than "cleaned up", because rounding order is
  part of the result.

Anything marked **not decoded** in `functions/rasterizer.md` is genuinely
unknown: the fill-rule weight of `RaspAntiAlias` and the grid-fitting path.
