# The `Rasp*` rasterizer pipeline

Call graph (ntoskrnl 10.0.18362.2274):

```
BgpDisplayCharacterEx            0x140348B34
 └─ BgpRasPrintGlyph             0x14018355C
     ├─ RaspGetCacheEntry        0x140183A70     key: (char, size, style)
     ├─ RaspGetUnscaledGlyphData 0x140183CE0
     │   ├─ RaspMapCharacterCodeToGlyphIndex   (cmap 4 / 12)
     │   └─ RaspMapGlyphIndexToLocation        (loca, short + long)
     ├─ RaspLoadGlyphData        0x1401852B8     simple glyf
     ├─ RaspLoadCompositeGlyphData 0x14034A878   component recursion, 2x2
     ├─ RaspScaleCoordinates     0x1401853DC     units -> 26.6 px
     ├─ RaspRasterize            0x140183B00
     │   ├─ RaspCreateSegmentList / RaspCreatePointList
     │   ├─ RaspTestIntersection / RaspInterpolatePoint
     │   ├─ RaspScanConvert      0x140183DC0
     │   └─ RaspAntiAlias        0x1401854C0
     └─ RaspAddCacheEntry
```

## RaspScaleCoordinates — `0x1401853DC`

The only place a unit turns into a pixel; everything above it is integers.

```c
/* font+96 = 96 (dpi), font+120 = upem */
int32_t scale_26_6 = ((size * 96) << 6) / (72 * upem);   /* per font unit */

for (each point p) {
    p->x = ((p->x * scale_26_6) >> 6) + x_offset_26_6;    /* 26.6 fixed point */
    p->y = baseline_26_6 - ((p->y * scale_26_6) >> 6);    /* y grows downwards */
}
```

`x_offset_26_6` is the pen position, `baseline_26_6 = size*asc*96*64/(72*upem)`
i.e. ascender in pixels — **not** a horizontal compression.

## BgpRasPrintGlyph — `0x14018355C`

```c
if (cache hit) blit cached bitmap;                       /* RaspBitmapCache */
else {
    scale outline (above);
    RaspRasterize -> 8 bpp coverage bitmap;
    RaspAddCacheEntry;                                   /* capacity 100 */
}
advance_pen(bitmap->advanceWidth);                       /* hmtx */
```

The bitmap header exposes the coverage depth as `*(u32*)(hdr+4) >> 2 == 8`,
i.e. the kernel keeps an **anti-aliased grey mask**, never 1 bit.

## RaspScanConvert / RaspAntiAlias — `0x140183DC0` / `0x1401854C0`

Not fully linearised. What is established:

* segments are built from the outline, sorted by y, intersections solved with
  `RaspInterpolatePoint` (no floating point),
* the span fill is the TrueType default: **nonzero winding**,
* the result is horizontally anti-aliased coverage, accumulated into 8 bits.

What is **not** decoded: the exact sub-scanline count, the coverage weight, and
the grid-fitting path (`RaspInitializeGlyphData`, `RaspConvertDeltas`,
`prep`/`fpgm`). This is stated as an open item instead of guessed, because
guessing produces "almost right" glyphs.

## Mapping to `src/ras.c` / `src/gfx.c`

| step | ntoskrnl | this repo |
|---|---|---|
| outline | `RaspLoadGlyphData` / composites | `tt_path()` in `src/tt.c` (glyf, loca, cmap 4/12, 2×2 composite) |
| curves | quadratic Béziers | flattened, step count from control-polygon length |
| scale | `RaspScaleCoordinates` | `s = em / upem`, `y` flipped around the baseline |
| fill | `RaspScanConvert` | nonzero winding, 4 sub-scanlines per row |
| AA | `RaspAntiAlias` | analytic horizontal coverage `min(b,i+1)-max(a,i)` |
| output | 8-bit coverage | `Bitmap.px`, 0..255 |
| cache | `RaspBitmapCache` (100) | 256 entries keyed by `(gid, em)` |

Net effect at the sizes used here: identical placement and identical wrap
points, edges within ~1 px where the kernel's hinting applies.
