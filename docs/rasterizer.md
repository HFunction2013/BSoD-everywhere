# The `Rasp*` rasterizer, and what this project does instead

## Call graph (ntoskrnl 10.0.18362.2274)

```
BgpDisplayCharacterEx            0x140348B34
 └─ BgpRasPrintGlyph             0x14018355C
     ├─ RaspGetCacheEntry        0x140183A70     (cache key: ch, size, style)
     ├─ RaspGetUnscaledGlyphData 0x140183CE0
     │   ├─ RaspMapCharacterCodeToGlyphIndex  (cmap)
     │   └─ RaspMapGlyphIndexToLocation       (loca)
     ├─ RaspLoadGlyphData        0x1401852B8     (glyf, simple glyphs)
     ├─ RaspLoadCompositeGlyphData 0x14034A878   (component recursion)
     ├─ RaspScaleCoordinates     0x1401853DC     (units -> 26.6 px)
     ├─ RaspRasterize            0x140183B00
     │   ├─ RaspCreateSegmentList / RaspCreatePointList
     │   ├─ RaspTestIntersection / RaspInterpolatePoint
     │   ├─ RaspScanConvert      0x140183DC0
     │   └─ RaspAntiAlias        0x1401854C0
     └─ RaspAddCacheEntry
```

`BgpRasInitializeRasterizer` (0x140A409A0) primes the font object (see
[process.md](process.md) §4) and sets the cache capacity
`dword_14042BF10 = 100`.

## The two formulas that matter

```c
/* BgpRasGetGlyphTextCellDimensions — what "size" means for layout */
cell = RoundUp( 96*size*(asc-desc)/(72*upem) ) + 2;

/* RaspScaleCoordinates — what "size" means for the outline */
scale = (size*96 << 6) / (72*upem);      /* 26.6 fixed point, applied per point */
```

so `em = 1.333*size`, `cell = 1.773*size` for the boot face, and therefore
**`em = 0.7519*cell`**. The table in `ntoskrnl` stores cells, not point sizes.

## Coverage format

`RaspRasterize` writes a bitmap whose header, read back as

```c
v17 = *(u32*)(v19 + 4) >> 2;      /* -> 8 */
```

means **8 bits per pixel of coverage** — an anti-aliased grey mask, never 1-bit.
`RaspAntiAlias` and `RaspScanConvert` were not fully linearised; the fill rule
and the supersampling weight are still open (see "Not decoded" below).

## What `src/ras.c` implements

An equivalent, dependency-free scanline rasterizer:

| step | ntoskrnl | here |
|---|---|---|
| outline source | `RaspLoadGlyphData`/`RaspGetUnscaledGlyphData` | `tt_path()` in `src/tt.c` (glyf + loca + cmap 4/12, composites with 2×2 transform) |
| curve | quadratic Béziers | flattened, segment count from control-polygon length (`quad()`) |
| scale | `RaspScaleCoordinates` | `s = em / upem`, `x*em/upem`, `y` flipped |
| fill | `RaspScanConvert` | nonzero winding, 4 sub-scanlines per row |
| AA | `RaspAntiAlias` | analytic horizontal coverage per span (area integral) |
| output | 8-bit coverage bitmap | `Bitmap.px`, 0..255 |
| cache | `RaspBitmapCache`, 100 entries | 256-entry table keyed by `(gid, em)` |

Horizontal coverage for a span `[a,b)` in pixel `i` is
`min(b, i+1) - max(a, i)`, accumulated over the 4 sub-scanlines and divided by 4.
That is the same integral the kernel computes; the difference is quantisation and
the absence of grid-fitting.

## Not decoded (and why it does not affect 1:1 layout)

1. **Grid-fitting / hinting.** `RaspInitializeGlyphData`, `RaspConvertDeltas`
   and the `prep`/`fpgm` interpretation were not fully traced. Missing it can
   move an edge by ~1 px at small sizes; it cannot move a baseline, a box or a
   wrap point.
2. **Fill rule and supersampling weight** of `RaspScanConvert` /
   `RaspAntiAlias`. Nonzero winding is the TrueType default and the coverage
   histogram matches; the exact sample count is unknown.

Both are listed as open items rather than guessed, because guessing here produces
"almost right" glyphs — the failure mode this project exists to avoid.

## Reproducing the reference frame

```bash
bsod --size 1920x1080 --render out.bmp --dump-layout \
     --font /path/to/segoe_slboot.ttf
```

`--dump-layout` prints every computed coordinate, which is what to diff when
something looks off:

```
font           : upem=2048 asc=2210 desc=-514  cell/em=1.3298
sad face       : (190,105) cell=280 em=210.5
body           : x=207 right=1187 cell=56 em=42.1 top=393 stride=59
progress       : (207,601) cell=56
qr             : (207,703) size=214   [d7=440 d12=70 are dead fields]
info           : (441,695) cell1=30 cell0=24 stride=34
```
