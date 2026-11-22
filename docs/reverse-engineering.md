# How this was reverse engineered

Target: `C:\Windows\System32\ntoskrnl.exe`, version **10.0.18362.2274**
(Windows 10 1903), image base `0x140000000`. The binary ships with PDB names, so
IDA already labels internal routines such as `BgpFwDisplayBugCheckScreen` — that
saved most of the search work.

Goal: reproduce the bugcheck screen 1:1 — geometry **and** glyph placement —
without trusting a screenshot.

---

## 0. Result first

| thing | value |
|---|---|
| draw entry | `BgpFwDisplayBugCheckScreen` (0x140349DF0) |
| layout table | `qword_140429ED0` (0x140429ED0), 5 rows × 18 DWORD |
| row pick | `BcpGetDisplayType` (0x140349998) |
| component rects | `BcpGetComponentOffsets` (0x14034980C) |
| text engine | `BcpDisplayCriticalString` (0x140349024) |
| info block | `BcpDisplayErrorInformation` (0x140349380) |
| progress | `BcpDisplayProgress` (0x140349608) |
| font chain | `BgpFwLibraryInitialize` → `BgpFoInitialize` → `FopInitializeFonts` → `BgpRasInitializeRasterizer` |
| glyph scale | `RaspScaleCoordinates` (0x1401853DC) |
| **core relation** | `em = cell * upem / (asc - desc)`  (0.7519 for the boot face) |

---

## 1. Tooling setup that actually works

`idat64 -A -S script.py ntoskrnl.exe` **fails** on a fresh binary this big: a
single-pass auto-analysis gives up half way. Two passes are required:

```powershell
# pass 1: build the database only
idat64 -B -A ntoskrnl.exe
# pass 2: run Hex-Rays dumps against the .i64
idat64 -A -S dump.py ntoskrnl.exe.i64
```

Two pitfalls cost hours:

* `idat64` **does not exit** after `-S`. It keeps the `.i64` open and the next
  run dies with `Permission denied`. Always `Stop-Process -Name idat64 -Force`
  before the next script; the database recovers from the packed base.
* IDA's bundled Python needs to be first on `PATH`, and `PYTHONHOME` /
  `PYTHONPATH` must be cleared, otherwise the script cannot `import idc`.

The dump template used for every function here:

```python
import io, ida_hexrays, ida_funcs
ida_hexrays.init_hexrays_plugin()
out = io.open(r"dump.txt", "w", encoding="utf-8")
for name in ("BgpFwDisplayBugCheckScreen", "BcpGetComponentOffsets", ...):
    ea = ida_name.get_name_ea(0, name)
    out.write("######## %s @ 0x%X\n%s\n" % (name, ea, ida_hexrays.decompile(ea)))
out.write("DONE\n"); out.close()
```

For "is this field read anywhere", do not guess — walk the xrefs:

```python
import idautils, ida_funcs, ida_hexrays
for ref in idautils.XrefsTo(0x140429ED0):        # note: idautils, not ida_xref
    fn = ida_funcs.get_func(ref.frm)
    print(hex(ref.frm), ida_funcs.get_func_name(fn.start_ea))
```

Nine functions reference the table. Decompiling all nine and grepping for
`qword_140429ED0[... + k]` is what proved `d7`/`d12` dead (see
[`display_table.md`](display_table.md)).

---

## 2. The index trap in the layout table

The table is addressed as `qword_140429ED0[9 * row + k]`, i.e. as **9 QWORDs**
per row, not 18 DWORDs. A `qword[k]` therefore contains *two* fields:

```
qword[k] low  32 bits = dword[2k]
qword[k] high 32 bits = dword[2k+1]
```

Reading it as a flat DWORD array shifts everything after the first field by one
and produces plausible-but-wrong numbers. With the QWORD view, row 3
(1920×1080) is:

```
d0=24 d1=30 d2=56 d3=280 d4=190 d5=105 d6=980 d7=440(dead) d8=17
d9=8 d10=3 d11=34 d12=70(dead) d13=68 d14=20 d15=60 d16=1920 d17=1080
```

`BcpGetDisplayType` walks rows 1..4 and returns the count of satisfied
thresholds (`w >= d16 && h >= d17`), so 1920×1080 → 3.

Because the *same* `qword[9*row+2]` is used for both `":("` and the body, the
field semantics are: **`d3`, `d2`, `d1`, `d0` are line-box heights**, `d4/d5` are
margins, `d6` is the text box width, `d8` the indent, `d9` the gap after the
face, `d10` the extra line spacing, `d11` the component stride, `d13/d14/d15`
QR/info offsets.

---

## 3. `cell` vs `em` — the actual root cause of "it doesn't look right"

Two functions describe the same font at two scales:

```c
// BgpRasGetGlyphTextCellDimensions(size = font index)
height = RoundUp( 96*size*(asc-desc)/(72*upem) ) + 2;      // line box = cell

// RaspScaleCoordinates  (points are scaled one by one, 26.6 fixed point)
v6 = (size * *(u32*)(font+96)) << 6;      // font+96 == 96 (DPI)
v7 = 72 * upem;
coord_26_6 = v6 * (int16)unit / v7;       // per-unit scale = em/upem
```

so `em = size*96/72 = 1.333*size` and `cell = em*(asc-desc)/upem`. Divide:

```
cell/em = (asc-desc)/upem = (2210 + 514)/2048 = 1.3298
=>  em = 0.7519 * cell
```

The table stores `cell`. Feeding it as `em` makes every glyph 33 % too big
(which is exactly what the first attempts did), and the visible symptom is
"the second sentence is on one line instead of two".

`BgpFoDetermineFontInformation` (0x140A411BC) is the missing link: it starts at
font index 5 and increments, calling `BgpFoGetTextMetrics`, until
`height >= target`, then steps back one. So the number that reaches the rasterizer
is `size ≈ cell / 1.773`, e.g. 31.6 for `cell = 56`, and `em = 1.333*31.6 = 42.1`.

Cross-check with measured advances (the boot face, em 42.1):

```
S1 "Your PC ran into a problem and needs to restart."      = 890 px  (limit 980 -> one line)
S2 "We're just collecting some error info, and then we'll
    restart for you."                                      = 1209 px (breaks after "we'll")
info "For more information ... visit https://..."          = 919 px  (limit 746 -> URL drops)
```

All three wrapping decisions match the real screen, which is the strongest
available proof for the font + scale pair.

---

## 4. `BgpRasPrintGlyph`: what the `+86` short really is

Early on, `v25 = *((__int16*)font + 43)` (byte offset +86) looked like a 0.75
horizontal squeeze factor. It is not: `BgpRasInitializeRasterizer`
(0x140A409A0) fills the font object from the TTC headers.

| offset | content | boot face |
|---|---|---|
| +76 | `head.indexToLocFormat` | 1 |
| +78 | `hhea.advanceWidthMax` | — |
| +80 | `hhea.xMaxExtent` | — |
| +82 | `hhea.ascender` | 2210 |
| +84 | `hhea.descender` | −514 |
| +86 | ascender again → this is `v25` | 2210 |
| +88 | `hhea.numberOfHMetrics` | — |
| +96 | DPI constant | 96 |
| +120 | `head.unitsPerEm` | 2048 |

So `v28 = size * ascender * 96` and `v29 = v28*64/(72*upem)` is simply
"ascender in pixels, 26.6 fixed point", i.e. the baseline offset inside the
glyph bitmap. There is no horizontal compression anywhere in the pipeline.

---

## 5. Where the font comes from

`ntoskrnl` contains no font name strings — but it never needed any.
`BgpFwLibraryInitialize` does:

```c
memmove(v21, *(a1 + 72), *(a1 + 80));   // the loader-provided blob
BgpFoInitialize(v16 + 26, 1);           // <- first 4 bytes are 'ttcf'
BgpGxProcessQrCodeBitmap(*(a1 + 184), &qword_14042C098);
```

`BgpFoInitialize` (0x140A402C8) reads `1953784678 = 'ttcf'`, validates version
`0x00010000`/`0x00020000`, and takes the sub-font count from offset 8 —
a real TrueType **Collection**, provided by the boot loader (i.e. `winload`
passing data produced from `Boot\Fonts`). The QR bitmap comes from the same blob,
which is why both appear/disappear together.

Measured on disk:

* `C:\Windows\Boot\Fonts\segoe_slboot.ttf` — family "Segoe Boot Semilight",
  upem 2048, asc 2210, desc −514, `A` advance 636
* `C:\Windows\Boot\Fonts\segoen_slboot.ttf` — same headers, every advance
  exactly 0.75× (`A` = 477)

`segoen` is the pre-condensed **console** face (`BgpConsoleGetFontName` picks a
name per locale); the bugcheck text uses the wide one plus `em = 0.7519 × cell`.
Using `segoen` with `em = cell` gives the same widths but the wrong line-box
height (74 instead of 56), so it is the wrong choice.

Other boot faces (`wgl4_boot.ttf`, `chs/cht/jpn/kor_boot.ttf`, `segmono_boot.ttf`)
were measured and excluded: none reproduces the three wrap decisions.

---

## 6. Component offsets (`BcpGetComponentOffsets`)

```c
a1[0] = d4 + d8;                       // 207  progress x
a1[1] = bottom + d11;                  //      progress y
a1[2] = a1[1] + d11;                   //      = bottom + 2*d11  (component base)
a3[0] = 207;  a3[1] = base + d13;      //      QR  y = bottom + 2*d11 + 68 = bottom+136
a2[0] = 207 + qr_size + d14;           //      info x = 441
a2[1] = base + d15;                    //      info y = bottom + 2*d11 + 60 = bottom+128
a2[2] = d11 + a2[1];
```

`qr_size` is not a table field: `BgpFwDisplayBugCheckScreen` takes it from the
QR bitmap header (`v25 = *(u32*)(v27+4)`) and passes it in as `a5`. The five
boot bitmaps measure 72 / 90 / 115 / 214 / 284 px.

`d7 = 440` looks like an old QR-area width and `d12 = 70` like an old gap, but
nothing reads them any more.

---

## 7. Text: wrapping and strides

`BcpDisplayCriticalString` measures with `BgpRasGetGlyphAdvanceWidth`, breaks at
the last space before `cursor_x + width > right`, resets `x` to
`v6 = d4+d8`, and advances `y += v14 + d10`, where `v14` is the **max glyph
bitmap height of the line** (`if (v33 > v14) v14 = v33`), and `EC8 = v14 + y`.
The `:("` glyph fills its box, so its `v14 ≈ 279 ≈ cell 280`, giving body top
`105 + 280 + 8 = 393`.

`BcpDisplayErrorInformation` sets `BcpTextBoxLeftEdgeOverride` to the info rect,
prints "For more information … visit " and the URL (cell d1), then
`If you call a support person …` and `Stop Code: …` (cell d0), all with stride
`d11 = 34`.

Screen texts are UTF-16 literals at known file offsets:

```
0x3a03e0  We're just collecting some error info, and then we'll restart for you.
0x3a0470  We're just collecting some error info, and then you can restart.
0x3a0500  Your PC ran into a problem and needs to restart.
0x3a0570  If you call a support person, give them this info:
0x3a0650  For more information about this issue and possible fixes, visit
0x3a06e0  https://www.windows.com/stopcode
0x3a0728  What failed:
0x3a0748  Stop Code:          (capital C)
```

`":("` is a display-string resource, descriptor VA `0x140373770`
(`0x140376468` on the Insider branch, `a1 == 456`).

---

## 8. QR code

`BgpFwDisplayBugCheckScreen` calls `BgpGxDrawRectangle(qword_14042C098, &rect)` —
it **blits a bitmap**, it does not draw modules.

Extraction:

1. `bootres.dll` RCDATA `1/1033` (81,572 B) is an `MSWIM`
2. `dism /Apply-Image /Index:1` unpacks it
3. the result contains 24-bit BMPs with a 54-byte header: `qrcode1..5`, `winlogo1..5`

Sampling all five gives the same 25×25 matrix (md5 `b98ae53b…`), i.e. version 2,
ECC M, `https://www.windows.com/stopcode`, quiet zone ≈ 8 %.
That matrix is embedded in `src/layout.c` as 25 strings.

---

## 9. Rasterizer

See [`rasterizer.md`](rasterizer.md) for the `Rasp*` call graph and how the C
implementation maps onto it.

---

## 10. Pitfalls (in the order they were hit)

1. Single-pass IDA auto-analysis on `ntoskrnl` always fails → use `-B` first.
2. `idat64` does not exit → kill it before the next run.
3. `PYTHONHOME`/`PYTHONPATH` break IDA's embedded Python 3.8.
4. `idautils.XrefsTo`, not `ida_xref.XrefsTo`; `idc.generate_disasm_line`, not
   `ida_bytes.generate_disasm_line`.
5. Reading the layout table as DWORDs shifts every field.
6. Treating `cell` as `em` → 33 % oversized glyphs, wrong wrapping.
7. `.data` segments in a packed IDB can show as `ff` filler — verify with the
   `.asm` listing, not the database.
8. `qrcode` libraries overflow on this payload (32 chars, v2/M) → sample the
   bitmap instead.
9. `dism` is the only available WIM reader here (no 7z/wimlib) for `bootres`.
