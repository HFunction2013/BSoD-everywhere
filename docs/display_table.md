# The display table `qword_140429ED0`

`ntoskrnl!0x140429ED0`, five rows × 18 DWORD. Each row is stored as **9 QWORDs**,
so the code indexes `qword_140429ED0[9*row + k]`:

```
dword[2k]   = LODWORD(qword[9*row + k])
dword[2k+1] = HIDWORD(qword[9*row + k])
```

Row = display class returned by `BcpGetDisplayType` (0..4).

## Field map

| # | meaning | row3 | used by |
|---|---|---|---|
| d0 | small cell (info, after first paragraph) | 24 | `BcpDisplayErrorInformation`, `BcpGetCharacterMaxResourceProfile` |
| d1 | info first-paragraph cell | 30 | `BcpDisplayErrorInformation` |
| d2 | body cell | 56 | `BcpDisplayCriticalString`, `BcpDisplayProgress` |
| d3 | `":("` cell | 280 | `BgpFwDisplayBugCheckScreen` |
| d4 | left margin | 190 | `BcpGetComponentOffsets`, `BgpFwDisplayBugCheckScreen` |
| d5 | top margin | 105 | `BgpFwDisplayBugCheckScreen` |
| d6 | text box width | 980 | right edge = d4+d8+d6 |
| d7 | **unused** | 440 | nobody |
| d8 | text indent | 17 | left edge = d4+d8 |
| d9 | gap under `":("` | 8 | `BgpFwDisplayBugCheckScreen` |
| d10 | extra line spacing | 3 | `BcpDisplayCriticalString` |
| d11 | component stride | 34 | `BcpGetComponentOffsets`, `BcpDisplayErrorInformation` |
| d12 | **unused** | 70 | nobody |
| d13 | QR y offset | 68 | `BcpGetComponentOffsets` |
| d14 | QR→info gap (+ qr size) | 20 | `BcpGetComponentOffsets` |
| d15 | info y offset | 60 | `BcpGetComponentOffsets` |
| d16 | threshold width | 1920 | `BcpGetDisplayType` |
| d17 | threshold height | 1080 | `BcpGetDisplayType` |

`d3, d2, d1, d0` are **cell heights (line boxes)**, not font sizes:
`em = cell * upem / (asc - desc)`, which is `0.7519 * cell` for the boot face.

## Raw rows

```
row0 (<=640x480)  12 16 1A 078 050 02D 1D6 0B9 07 03 01 14 1E 28 0A 22 000 000
row1 (>=641x481)  12 16 20 0A0 069 03C 1FE 0FA 0A 05 02 16 28 2C 0F 26 281 1E1
row2 (>=1024x768) 12 16 28 0C8 087 04B 35C 136 0C 06 02 18 32 30 11 2A 400 300
row3 (>=1920x1080)18 1E 38 118 0BE 069 3D4 1B8 11 08 03 22 46 44 14 3C 780 438
row4 (>=2560x1440)22 28 48 168 0F5 087 4E2 262 16 0B 04 31 5A 62 19 56 A00 5A0
```

## Why d7 / d12 are dead

Every function that references the table was decompiled:

```
BcpGetCharacterMaxResourceProfile 0x140190294
BcpDisplayCriticalString          0x140349024
BcpDisplayErrorInformation        0x140349380
BcpDisplayProgress                0x140349608
BcpGetComponentOffsets            0x14034980C
BcpGetDisplayType                 0x140349998
BgpDisplaySafeToPowerOffScreen    0x140349B34
BgpFwDisplayBugCheckScreen        0x140349DF0
BgpBcInitializeCriticalMode       0x140A40E74
```

The only ones touching `qword[9*row+3]` and `qword[9*row+6]` are:

* `BcpDisplayCriticalString`: `LODWORD(qword[...+3])` = **d6** (right edge)
* `BcpGetComponentOffsets`: `HIDWORD(qword[...+6])` = **d13** (QR y)

`HIDWORD(qword+3)` (d7) and `LODWORD(qword+6)` (d12) appear nowhere.
`BcpGetCharacterMaxResourceProfile` walks every row but reads only its first
DWORD (`LODWORD(v19) = *v8`, `v8 += 18`), i.e. d0, to size the glyph cache.

So both values are leftovers — most likely an older QR area width and an older
gap — and a faithful reproduction must **not** use them.
